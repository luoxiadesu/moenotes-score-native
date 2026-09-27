#include "moenotes_chart_parser.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static moenotes_score_t *parse(const char *json, int combo, int mirror, int hidden) {
    moenotes_score_t *s = NULL;
    moenotes_parse_options_t o;
    moenotes_default_parse_options(&o);
    o.slide_combo_unit = combo ? 8 : 0;
    o.mirror = (uint8_t)mirror;
    o.add_flick_hidden = (uint8_t)hidden;
    char error[128];
    moenotes_result_t r =
        moenotes_score_parse(json, strlen(json), &o, NULL, &s, error, sizeof(error));
    if (r != MOENOTES_OK)
        fprintf(stderr, "%s\n", error);
    assert(r == MOENOTES_OK && s);
    return s;
}
static moenotes_note_view_t at(moenotes_score_t *s, size_t i) {
    moenotes_note_view_t v;
    assert(moenotes_score_note_at(s, i, &v) == MOENOTES_OK);
    return v;
}
static void near(double a, double b) { assert(fabs(a - b) < 0.0001); }
static void test_auto(void) {
    const char *json = "{\"events\":{},\"notes\":[{\"type\":\"long\",\"node\":["
                       "{\"t\":100,\"pos\":2,\"size\":4,\"ease\":[\"in\",\"out\"]},"
                       "{\"t\":280,\"pos\":\"auto\",\"crit\":true},"
                       "{\"t\":550,\"pos\":\"auto\"},"
                       "{\"t\":1000,\"pos\":12,\"size\":6}]}]}";
    moenotes_score_t *s = parse(json, 0, 0, 0), *m = parse(json, 0, 1, 0);
    moenotes_note_view_t n = at(s, 1), b = at(s, 2), r = at(m, 1);
    /* Source geometry remains independently accessible after final processing. */
    assert(moenotes_score_source_note_at(s, 1, &n) == MOENOTES_OK);
    assert(moenotes_score_source_note_at(s, 2, &b) == MOENOTES_OK);
    assert(moenotes_score_source_note_at(m, 1, &r) == MOENOTES_OK);
    near(n.lane_start_float, 2.4);
    near(n.width, 7.92);
    near(b.lane_start_float, 4.5);
    near(b.width, 10.5);
    assert(b.lane_start == 4 && b.lane_end == 13);
    assert(n.pos_auto && n.slide_along && n.critical && !at(s, 0).critical);
    near(r.lane_start_float, 24 - n.lane_start_float - n.width);
    moenotes_line_sample_t sample, mirrored;
    assert(moenotes_score_sample_line(s, 0, 280, &sample) == MOENOTES_OK);
    assert(moenotes_score_sample_line(m, 0, 280, &mirrored) == MOENOTES_OK);
    near(sample.lane_start, n.lane_start_float);
    near(sample.width, n.width);
    near(mirrored.lane_start, 24 - sample.lane_start - sample.width);
    assert(moenotes_score_sample_line(s, 0, 99, &sample) == MOENOTES_ERR_RANGE);
    moenotes_score_free(s);
    moenotes_score_free(m);
}
static void test_timeline(void) {
    const char *json = "{\"events\":{\"bpm\":[{\"t\":480,\"bpm\":240}],\"sig\":["
                       "{\"t\":2880,\"sig\":[4,4]},{\"t\":0,\"sig\":[3,4]}],"
                       "\"skill\":[480],\"fever\":[[0,960]],\"call\":[{\"t\":0,\"timing\":[1,0,1]}]"
                       "},\"notes\":[]}";
    moenotes_score_t *s = parse(json, 0, 0, 0);
    moenotes_position_t p;
    assert(moenotes_score_bpm_count(s) == 2);
    assert(moenotes_score_position_at_tick(s, 1440, &p) == MOENOTES_OK);
    assert(p.bar == 1 && p.rhythm == 0 && p.time_ms == 1000);
    assert(moenotes_score_position_at_tick(s, 2880, &p) == MOENOTES_OK && p.bar == 2 &&
           p.rhythmic_unit == 1920);
    assert(moenotes_score_event_count(s) == 3);
    moenotes_event_t e;
    int32_t value;
    assert(moenotes_score_event_at(s, 1, &e) == MOENOTES_OK && e.type == MOENOTES_EVENT_FEVER &&
           e.end_position.time_ms == 750);
    assert(moenotes_score_event_value_at(s, 2, 2, &value) == MOENOTES_OK && value == 1);
    assert(moenotes_score_event_value_at(s, 2, 3, &value) == MOENOTES_ERR_RANGE);
    moenotes_score_free(s);
}
static void test_relative_combos(void) {
    const char *json =
        "{\"events\":{\"sig\":[{\"t\":0,\"sig\":[3,4]}]},\"notes\":["
        "{\"type\":\"long\",\"node\":[{\"t\":100,\"pos\":0},{\"t\":500,\"pos\":4,\"visible\":false}"
        ",{\"t\":850,\"pos\":8}]},"
        "{\"type\":\"long\",\"node\":[{\"t\":100,\"pos\":12},{\"t\":850,\"pos\":18}]}]}";
    moenotes_score_t *s = parse(json, 1, 0, 0);
    size_t combo = 0, skip = 0;
    for (size_t i = 0; i < moenotes_score_note_count(s); i++) {
        moenotes_note_view_t n = at(s, i);
        if (!n.generated)
            continue;
        assert(n.tick == 340 || n.tick == 580 || n.tick == 820);
        if (n.operate_type == MOENOTES_OP_COMBO)
            combo++;
        if (n.operate_type == MOENOTES_OP_COMBO_SKIP) {
            skip++;
            assert(n.tick == 820);
        }
        assert(n.parent_note_id >= 0 && n.hidden_for_note_id == -1);
    }
    assert(combo == 4 && skip == 2);
    assert(moenotes_score_full_combo_count(s, 1, 0) == 8);
    assert(moenotes_score_full_combo_count(s, 0, 0) == 4);
    moenotes_score_free(s);
}
static void test_hidden(void) {
    const char *json =
        "{\"events\":{},\"notes\":["
        "{\"type\":\"long\",\"node\":[{\"t\":0,\"pos\":0},{\"t\":480,\"pos\":0}]},"
        "{\"type\":\"flick\",\"t\":240,\"pos\":12},"
        "{\"type\":\"long\",\"node\":[{\"t\":960,\"pos\":0},{\"t\":1440,\"pos\":0}]},"
        "{\"type\":\"flick\",\"t\":1200,\"pos\":12,\"line_indices\":[0]}]}";
    moenotes_score_t *s = parse(json, 0, 0, 1);
    size_t hidden = 0;
    assert(moenotes_score_note_count(s) == 7);
    for (size_t i = 0; i < 7; i++) {
        moenotes_note_view_t n = at(s, i);
        if (n.hidden_for_note_id < 0)
            continue;
        hidden++;
        assert(n.tick == 1200 && n.line_id == 1 && n.line_index == 0 && !n.visible);
        assert(n.operate_type == MOENOTES_OP_HIDDEN &&
               !moenotes_operate_type_is_judgement(n.operate_type));
    }
    assert(hidden == 1 && moenotes_score_full_combo_count(s, 1, 0) == 6);
    moenotes_score_free(s);
}
static void test_shared_endpoint(void) {
    const char *json =
        "{\"events\":{},\"notes\":["
        "{\"type\":\"long\",\"node\":[{\"t\":0,\"pos\":0},{\"t\":960,\"pos\":6}]},"
        "{\"type\":\"long\",\"node\":[{\"t\":0,\"pos\":12},{\"t\":960,\"pos\":6}]}]}";
    moenotes_score_t *s = parse(json, 0, 0, 0);
    assert(moenotes_score_note_count(s) == 3);
    moenotes_note_view_t end = at(s, 2), other;
    assert(moenotes_score_note_line_count(s, end.id) == 2);
    assert(moenotes_score_line_member_at(s, 1, 1, &other) == MOENOTES_OK && other.id == end.id);
    int32_t line;
    assert(moenotes_score_note_line_at(s, end.id, 1, &line) == MOENOTES_OK && line == 1);
    assert(moenotes_score_warnings(s) & MOENOTES_WARNING_SHARED_ENDPOINT);
    moenotes_score_free(s);
}
static void test_guide(void) {
    const char *json =
        "{\"events\":{},\"notes\":[{\"type\":\"flick\",\"t\":0,\"pos\":0,\"dir\":\"left\"},"
        "{\"type\":\"guide\",\"node\":[{\"t\":0,\"pos\":0},{\"t\":480,\"pos\":4,\"visible\":false},"
        "{\"t\":960,\"pos\":6,\"type\":\"trace\"}]}]}";
    moenotes_score_t *s = parse(json, 0, 0, 0);
    assert(moenotes_score_note_count(s) == 3);
    assert(at(s, 0).operate_type == MOENOTES_OP_GUIDE_BEGIN_FLICK &&
           at(s, 0).direction == MOENOTES_DIRECTION_LEFT);
    assert(at(s, 1).operate_type == MOENOTES_OP_HIDDEN);
    assert(at(s, 2).operate_type == MOENOTES_OP_GUIDE_END_TRACE);
    moenotes_score_free(s);
}
typedef struct failures {
    size_t calls, fail_at, live;
} failures_t;
static void *fm(void *ctx, size_t n) {
    failures_t *f = ctx;
    if (f->calls++ == f->fail_at)
        return NULL;
    void *p = malloc(n);
    if (p)
        f->live++;
    return p;
}
static void *fr(void *ctx, void *p, size_t n) {
    failures_t *f = ctx;
    if (f->calls++ == f->fail_at)
        return NULL;
    int fresh = p == NULL;
    void *q = realloc(p, n);
    if (q && fresh)
        f->live++;
    return q;
}
static void ff(void *ctx, void *p) {
    failures_t *f = ctx;
    if (p) {
        assert(f->live);
        f->live--;
        free(p);
    }
}
static void test_allocators(void) {
    const char *j = "{\"events\":{\"call\":[{\"t\":0,\"timing\":[1,0]}]},\"notes\":[{\"type\":"
                    "\"long\",\"node\":[{\"t\":0},{\"t\":4800}]}]}";
    unsigned char gz[512];
    z_stream z = {0};
    assert(deflateInit2(&z, 1, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) == Z_OK);
    z.next_in = (Bytef *)j;
    z.avail_in = (uInt)strlen(j);
    z.next_out = gz;
    z.avail_out = sizeof(gz);
    assert(deflate(&z, Z_FINISH) == Z_STREAM_END);
    size_t len = z.total_out;
    deflateEnd(&z);
    for (int zipped = 0; zipped < 2; zipped++) {
        for (size_t at = 0; at < 128; at++) {
            failures_t f = {0, at, 0};
            moenotes_allocator_t a = {&f, fm, fr, ff};
            moenotes_score_t *s = NULL;
            moenotes_parse_options_t o;
            moenotes_default_parse_options(&o);
            o.slide_combo_unit = 8;
            moenotes_result_t r = moenotes_score_parse(
                zipped ? (void *)gz : (void *)j, zipped ? len : strlen(j), &o, &a, &s, NULL, 0);
            assert(r == MOENOTES_OK || r == MOENOTES_ERR_OUT_OF_MEMORY);
            if (r == MOENOTES_OK)
                moenotes_score_free(s);
            else
                assert(!s);
            assert(!f.live);
            if (r == MOENOTES_OK)
                break;
            assert(at < 127);
        }
    }
    moenotes_score_t *s = NULL;
    assert(moenotes_score_parse(gz, len - 4, NULL, NULL, &s, NULL, 0) == MOENOTES_ERR_GZIP && !s);
}
static void test_errors(void) {
    const char *bad[] = {"{\"events\":{},\"notes\":[{\"t\":1.5}]}",
                         "{\"events\":{},\"notes\":[{\"t\":2147483648}]}",
                         "{\"events\":{},\"notes\":[{\"pos\":1e99}]}",
                         "{\"events\":{},\"notes\":[{\"type\":\"long\",\"node\":[{}]}]}",
                         "{\"events\":{\"sig\":[{\"sig\":[3,0]}]},\"notes\":[]}",
                         "{\"events\":{},\"notes\":[{\"ease\":[]}]}"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
        moenotes_score_t *s = (void *)1;
        assert(moenotes_score_parse(bad[i], strlen(bad[i]), NULL, NULL, &s, NULL, 0) !=
                   MOENOTES_OK &&
               !s);
    }
    moenotes_score_t *s = NULL;
    assert(moenotes_score_parse("#TITLE", 6, NULL, NULL, &s, NULL, 0) == MOENOTES_ERR_UNSUPPORTED);
    s = parse("{\"events\":{},\"notes\":[{\"pos\":0.5,\"size\":0,\"visible\":false,\"alpha\":"
              "\"fadeOut\"}]}",
              0, 0, 0);
    assert(at(s, 0).lane_start == 0 && at(s, 0).lane_end == 0 && at(s, 0).width == 0 &&
           at(s, 0).alpha == MOENOTES_ALPHA_FADE_OUT);
    moenotes_score_free(s);
}
int main(void) {
    test_auto();
    test_timeline();
    test_relative_combos();
    test_hidden();
    test_shared_endpoint();
    test_guide();
    test_allocators();
    test_errors();
    puts("feature regressions passed");
    return 0;
}
