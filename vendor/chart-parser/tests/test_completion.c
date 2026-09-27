#include "moenotes_chart_parser.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static moenotes_score_t *parse(const char *json, int mirror) {
    moenotes_parse_options_t o;
    moenotes_default_parse_options(&o);
    o.slide_combo_unit = 8;
    o.mirror = (uint8_t)mirror;
    moenotes_score_t *s = NULL;
    assert(moenotes_score_parse(json, strlen(json), &o, NULL, &s, NULL, 0) == MOENOTES_OK);
    return s;
}
static moenotes_note_view_t at(const moenotes_score_t *s, size_t i) {
    moenotes_note_view_t n;
    assert(moenotes_score_note_at(s, i, &n) == MOENOTES_OK);
    return n;
}
static void test_final_geometry(void) {
    const char *json = "{\"events\":{},\"notes\":[{\"type\":\"long\",\"node\":["
        "{\"t\":0,\"size\":6,\"ease\":[\"out\",\"in\"]},"
        "{\"t\":480,\"pos\":\"auto\"},{\"t\":960,\"size\":12}]}]}";
    for (int mirror = 0; mirror < 2; mirror++) {
        moenotes_score_t *s = parse(json, mirror);
        moenotes_note_view_t final = at(s, 2), source, member;
        assert(final.tick == 480 && final.pos_auto);
        assert(moenotes_score_source_note_at(s, 2, &source) == MOENOTES_OK);
        assert(final.width == 10.5 && source.width == 7.5);
        assert(final.lane_start_float == (mirror ? 13.5 : 0));
        assert(final.lane_start == (mirror ? 13 : 0) && final.lane_end == (mirror ? 23 : 10));
        assert(moenotes_score_line_member_at(s, 0, 2, &member) == MOENOTES_OK);
        assert(member.width == final.width && member.id == final.id);
        moenotes_line_sample_t render;
        assert(moenotes_score_sample_line(s, 0, 480, &render) == MOENOTES_OK);
        assert(render.width == source.width && render.lane_start == source.lane_start_float);
        moenotes_score_free(s);
    }
}
static void test_shared_guide_absorption(void) {
    const char *json = "{\"events\":{},\"notes\":["
        "{\"t\":0,\"pos\":4,\"type\":\"flick\",\"dir\":\"left\",\"crit\":true},"
        "{\"type\":\"guide\",\"node\":[{\"t\":0,\"pos\":4},{\"t\":960,\"pos\":12}]},"
        "{\"type\":\"guide\",\"node\":[{\"t\":0,\"pos\":4},{\"t\":720,\"pos\":6},"
        "{\"t\":960,\"pos\":12}]}]}";
    for (int mirror = 0; mirror < 2; mirror++) {
        moenotes_score_t *s = parse(json, mirror);
        moenotes_line_view_t a, b;
        assert(moenotes_score_note_count(s) == 3);
        assert(moenotes_score_line_at(s, 0, &a) == MOENOTES_OK);
        assert(moenotes_score_line_at(s, 1, &b) == MOENOTES_OK);
        assert(a.begin_note_id == b.begin_note_id && a.end_note_id == b.end_note_id);
        moenotes_note_view_t n = at(s, 0);
        assert(n.operate_type == MOENOTES_OP_GUIDE_BEGIN_FLICK && !n.critical);
        assert(n.direction == (mirror ? MOENOTES_DIRECTION_RIGHT : MOENOTES_DIRECTION_LEFT));
        assert(moenotes_score_note_line_count(s, n.id) == 2);
        moenotes_score_free(s);
    }
    /* Absorption uses pre-mirror rounded raw position/width, without the
     * minimum-width clamp used by exposed integer geometry. */
    const char *fractional = "{\"events\":{},\"notes\":["
        "{\"t\":0,\"pos\":0.4,\"size\":0.1,\"type\":\"flick\",\"dir\":\"left\"},"
        "{\"type\":\"guide\",\"node\":[{\"t\":0,\"pos\":0.49,\"size\":0.4},"
        "{\"t\":960,\"pos\":6,\"size\":2}]}]}";
    for (int mirror = 0; mirror < 2; mirror++) {
        moenotes_score_t *s = parse(fractional, mirror);
        assert(moenotes_score_note_count(s) == 2);
        assert(at(s, 0).operate_type == MOENOTES_OP_GUIDE_BEGIN_FLICK);
        moenotes_score_free(s);
    }
}
static void test_bpm_accumulation(void) {
    /* Native BuildBpmSegments keeps a double accumulator and rounds each anchor
     * nearest-even. TickToTimeMs then floors only the local interval. */
    moenotes_score_t *s = parse("{\"events\":{\"bpm\":[{\"t\":0,\"bpm\":181},"
        "{\"t\":17280,\"bpm\":178},{\"t\":48000,\"bpm\":179},{\"t\":67200,\"bpm\":181}]},"
        "\"notes\":[{\"t\":70000}]}", 0);
    const int expected[] = {0, 11934, 33507, 46915};
    for (size_t i = 0; i < 4; i++) {
        moenotes_bpm_event_t b;
        assert(moenotes_score_bpm_at(s, i, &b) == MOENOTES_OK);
        assert(b.position.time_ms == expected[i]);
    }
    moenotes_score_free(s);
    s = parse("{\"events\":{\"bpm\":[{\"t\":0,\"bpm\":100},{\"t\":2,\"bpm\":100},"
              "{\"t\":4,\"bpm\":100},{\"t\":6,\"bpm\":100}]},\"notes\":[]}", 0);
    const int ties[] = {0, 2, 5, 8};
    for (size_t i = 0; i < 4; i++) {
        moenotes_bpm_event_t b;
        assert(moenotes_score_bpm_at(s, i, &b) == MOENOTES_OK && b.position.time_ms == ties[i]);
    }
    moenotes_score_free(s);
}
static void test_combo_stored_edge(void) {
    moenotes_score_t *s = parse("{\"events\":{},\"notes\":["
        "{\"type\":\"long\",\"node\":[{\"t\":0},{\"t\":480,\"pos\":4},{\"t\":960,\"pos\":8}]},"
        "{\"type\":\"long\",\"node\":[{\"t\":0,\"pos\":12},{\"t\":720,\"pos\":10},"
        "{\"t\":960,\"pos\":8}]}]}", 0);
    int found = 0;
    for (size_t i = 0; i < moenotes_score_note_count(s); i++) {
        moenotes_note_view_t n = at(s, i);
        if (n.generated && n.source_index == 1 && n.tick == 480) {
            float right = (float)n.lane_end_float;
            uint32_t bits;
            memcpy(&bits, &right, sizeof(bits));
            assert(bits == UINT32_C(0x417aaaab));
            found = 1;
        }
    }
    assert(found);
    moenotes_score_free(s);
}
static void test_same_tick_line_order(void) {
    moenotes_score_t *s = parse("{\"events\":{},\"notes\":[{\"type\":\"long\",\"node\":["
        "{\"t\":0},{\"t\":480,\"pos\":\"auto\"},"
        "{\"t\":480,\"pos\":4,\"visible\":false},{\"t\":960,\"pos\":8}]}]}", 0);
    int found = 0;
    for (size_t i = 0; i < moenotes_score_line_member_count(s, 0); i++) {
        moenotes_note_view_t n;
        assert(moenotes_score_line_member_at(s, 0, i, &n) == MOENOTES_OK);
        if (n.tick == 480) {
            assert(n.operate_type == (found ? MOENOTES_OP_SLIDE_CONNECTION : MOENOTES_OP_HIDDEN));
            found++;
        }
    }
    assert(found == 2);
    moenotes_score_free(s);
}
static void test_events_and_tail(void) {
    moenotes_score_t *s = parse("{\"events\":{\"skill\":[960,0],\"fever\":[[240,720],[0,480]],"
        "\"call\":[{\"t\":960,\"timing\":[0,1]},{\"t\":0,\"timing\":[1,0,1,2,-1]}]},"
        "\"notes\":[{\"t\":0},{\"t\":480},{\"t\":720},{\"t\":1920,\"pos\":0},"
        "{\"t\":1920,\"pos\":12}]}", 0);
    moenotes_event_t e;
    assert(moenotes_score_event_at(s, 0, &e) == MOENOTES_OK && e.tick == 960);
    assert(moenotes_score_event_at(s, 2, &e) == MOENOTES_OK && e.tick == 0);
    assert(moenotes_score_event_at(s, 4, &e) == MOENOTES_OK && e.tick == 0);
    double r;
    assert(moenotes_score_call_rhythm_count(s, 4) == 2);
    assert(moenotes_score_call_rhythm_at(s, 4, 0, &r) == MOENOTES_OK && r == (double)0.2f);
    assert(moenotes_score_call_rhythm_at(s, 4, 1, &r) == MOENOTES_OK && r == (double)0.6f);
    assert(moenotes_score_call_rhythm_at(s, 4, 2, &r) == MOENOTES_ERR_RANGE);
    int32_t fever;
    assert(moenotes_score_note_fever_event(s, at(s, 1).id, &fever) == MOENOTES_OK && fever == 2);
    assert(moenotes_score_note_fever_event(s, at(s, 2).id, &fever) == MOENOTES_OK && fever == 3);
    assert(moenotes_score_note_fever_event(s, at(s, 3).id, &fever) == MOENOTES_OK && fever == -1);
    moenotes_position_t p;
    assert(moenotes_score_bar_line_count(s) == 2);
    assert(moenotes_score_bar_line_at(s, 1, &p) == MOENOTES_OK && p.time_ms == 2000 && p.bar == 1);
    assert(moenotes_score_last_timing_note_count(s) == 2);
    moenotes_note_view_t n;
    assert(moenotes_score_last_timing_note_at(s, 1, &n) == MOENOTES_OK && n.tick == 1920);
    assert(moenotes_score_last_timing_note_at(s, 2, &n) == MOENOTES_ERR_RANGE);
    assert(moenotes_score_source_note_at(NULL, 0, &n) == MOENOTES_ERR_INVALID_ARGUMENT);
    assert(moenotes_score_note_fever_event(s, -1, &fever) == MOENOTES_ERR_RANGE);
    assert(moenotes_score_call_rhythm_count(NULL, 0) == 0);
    assert(moenotes_score_bar_line_count(NULL) == 0);
    assert(moenotes_score_last_timing_note_count(NULL) == 0);
    moenotes_score_free(s);
}
int main(void) {
    test_final_geometry();
    test_shared_guide_absorption();
    test_bpm_accumulation();
    test_combo_stored_edge();
    test_same_tick_line_order();
    test_events_and_tail();
    puts("Native completion regressions passed");
    return 0;
}
