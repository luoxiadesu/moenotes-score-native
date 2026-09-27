#include "moenotes_chart_parser.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static moenotes_score_t *parse(const char *json, int combo, int mirror) {
    moenotes_parse_options_t options;
    moenotes_default_parse_options(&options);
    options.slide_combo_unit = combo ? 8 : 0;
    options.mirror = (uint8_t)mirror;
    moenotes_score_t *s = NULL;
    assert(moenotes_score_parse(json, strlen(json), &options, NULL, &s, NULL, 0) == MOENOTES_OK);
    return s;
}
static moenotes_note_view_t at(const moenotes_score_t *s, size_t i) {
    moenotes_note_view_t n;
    assert(moenotes_score_note_at(s, i, &n) == MOENOTES_OK);
    return n;
}
static moenotes_note_view_t generated_at(const moenotes_score_t *s, int tick) {
    for (size_t i = 0; i < moenotes_score_note_count(s); i++) {
        moenotes_note_view_t n = at(s, i);
        if (n.generated && n.tick == tick)
            return n;
    }
    assert(!"missing generated note");
    return (moenotes_note_view_t){0};
}
static void near(double a, double b) { assert(fabs(a - b) < 0.0001); }

static void test_clocks_and_thresholds(void) {
    /* Native ARM64 fixtures: 1 ms changes the strict end guard both ways. */
    const char *positive = "{\"events\":{\"bpm\":[{\"t\":0,\"bpm\":170}],\"sig\":["
        "{\"t\":0,\"sig\":[3,4]},{\"t\":10080,\"sig\":[4,4]},"
        "{\"t\":17760,\"sig\":[3,4]},{\"t\":29280,\"sig\":[4,4]},"
        "{\"t\":33120,\"sig\":[3,4]},{\"t\":36000,\"sig\":[4,4]}]},"
        "\"notes\":[{\"type\":\"long\",\"node\":[{\"t\":36000},{\"t\":36360}]}]}";
    moenotes_score_t *s = parse(positive, 1, 0);
    assert(at(s, 0).position.time_ms == 26469);
    assert(at(s, 2).position.time_ms == 26734);
    assert(generated_at(s, 36240).operate_type == MOENOTES_OP_COMBO_SKIP);
    assert(moenotes_score_full_combo_count(s, 1, 0) == 2);
    moenotes_position_t tick, note;
    assert(moenotes_score_position_at_tick(s, 36360, &tick) == MOENOTES_OK && tick.time_ms == 26735);
    assert(moenotes_score_note_position_at_tick(s, 36360, &note) == MOENOTES_OK && note.time_ms == 26734);
    moenotes_signature_event_t sig;
    assert(moenotes_score_signature_at(s, 5, &sig) == MOENOTES_OK && sig.position.time_ms == 26470);
    moenotes_score_free(s);
    const char *negative = "{\"events\":{\"bpm\":[{\"t\":0,\"bpm\":137}]},\"notes\":["
        "{\"type\":\"long\",\"node\":[{\"t\":108960},{\"t\":109320},{\"t\":109680}]}]}";
    s = parse(negative, 1, 0);
    assert(generated_at(s, 109560).operate_type == MOENOTES_OP_COMBO);
    assert(moenotes_score_full_combo_count(s, 1, 0) == 4);
    assert(moenotes_score_note_position_at_tick(s, 109680, &note) == MOENOTES_OK && note.time_ms == 100073);
    moenotes_score_free(s);
    /* A quarter-offset start retains its phase across a meter change. */
    s = parse("{\"events\":{\"sig\":[{\"t\":0,\"sig\":[4,4]},"
              "{\"t\":1920,\"sig\":[3,4]}]},\"notes\":[{\"type\":\"long\","
              "\"node\":[{\"t\":1800},{\"t\":2640}]}]}", 1, 0);
    assert(generated_at(s, 2010).position.rhythmic_unit == 6);
    assert(generated_at(s, 2250).position.time_ms == 2343);
    moenotes_score_free(s);
}
static void test_mirror_and_sampling(void) {
    const char *json = "{\"events\":{},\"notes\":[{\"type\":\"long\",\"node\":["
        "{\"t\":0,\"pos\":0,\"size\":6,\"ease\":[\"out\",\"in\"]},"
        "{\"t\":960,\"pos\":0,\"size\":12}]}]}";
    moenotes_score_t *s = parse(json, 1, 0), *m = parse(json, 1, 1);
    moenotes_note_view_t n = generated_at(s, 480), mirrored = generated_at(m, 480);
    near(n.width, 10.5);
    near(mirrored.lane_start_float, 13.5);
    near(mirrored.width, 10.5);
    assert(at(m, 0).ease_left == MOENOTES_EASE_OUT && at(m, 0).ease_right == MOENOTES_EASE_IN);
    moenotes_line_sample_t render, judge, mr;
    assert(moenotes_score_sample_line(s, 0, 480, &render) == MOENOTES_OK);
    assert(moenotes_score_sample_line(m, 0, 480, &mr) == MOENOTES_OK);
    near(render.width, 7.5);
    near(mr.lane_start, 16.5);
    assert(moenotes_score_sample_judgement_line(m, 0, 480, &judge) == MOENOTES_OK);
    near(judge.lane_start, 13.5);
    near(judge.width, 10.5);
    moenotes_score_free(s);
    moenotes_score_free(m);
    s = parse("{\"events\":{\"bpm\":[{\"t\":0,\"bpm\":100000}]},\"notes\":["
              "{\"type\":\"long\",\"node\":[{\"t\":0,\"pos\":0,\"size\":6},"
              "{\"t\":2,\"pos\":10,\"size\":6}]}]}", 0, 0);
    assert(at(s, 0).position.time_ms == at(s, 1).position.time_ms);
    assert(moenotes_score_sample_judgement_line(s, 0, 1, &judge) == MOENOTES_OK);
    near(judge.lane_start, 5);
    near(judge.lane_end, 10);
    moenotes_score_free(s);
}
static void test_pairing(void) {
    moenotes_score_t *s = parse("{\"events\":{},\"notes\":[{\"t\":480,\"pos\":12},"
        "{\"t\":480,\"pos\":0},{\"t\":480,\"pos\":6}]}", 0, 0);
    moenotes_note_view_t a = at(s, 0), b = at(s, 1), c = at(s, 2);
    assert(a.pair_note_id == b.id && b.pair_note_id == c.id && c.pair_note_id == b.id);
    moenotes_score_free(s);
    /* Singles are inserted first; begin priority precedes them even if later in source. */
    s = parse("{\"events\":{},\"notes\":[{\"t\":0,\"pos\":6},{\"t\":0,\"pos\":12},"
              "{\"type\":\"long\",\"node\":[{\"t\":0},{\"t\":480}]},"
              "{\"t\":480,\"pos\":12}]}", 0, 0);
    a = at(s, 0); b = at(s, 1); c = at(s, 2);
    assert(c.operate_type == MOENOTES_OP_SLIDE_BEGIN && c.pair_note_id == a.id);
    assert(a.pair_note_id == b.id && b.pair_note_id == a.id);
    a = at(s, 3); b = at(s, 4);
    assert(a.operate_type == MOENOTES_OP_SLIDE_END);
    assert(a.pair_note_id == b.id && b.pair_note_id == a.id);
    moenotes_score_free(s);
}
static void test_shared_branches_and_indices(void) {
    const char *json = "{\"events\":{},\"notes\":["
        "{\"type\":\"long\",\"node\":[{\"t\":0},{\"t\":480,\"pos\":4},{\"t\":960,\"pos\":8}]},"
        "{\"type\":\"long\",\"node\":[{\"t\":0},{\"t\":720,\"pos\":12},{\"t\":1440,\"pos\":16}]}]}";
    moenotes_score_t *s = parse(json, 1, 0);
    moenotes_line_view_t l0, l1;
    assert(moenotes_score_line_at(s, 0, &l0) == MOENOTES_OK);
    assert(moenotes_score_line_at(s, 1, &l1) == MOENOTES_OK);
    assert(l0.begin_note_id == l1.begin_note_id && l0.end_note_id != l1.end_note_id);
    assert(l0.source_index == 0 && l1.source_index == 1);
    assert(moenotes_score_note_line_count(s, l0.begin_note_id) == 2);
    for (int lid = 0; lid < 2; lid++) {
        size_t count = moenotes_score_line_member_count(s, lid);
        assert(count > 3);
        for (size_t i = 0; i < count; i++) {
            moenotes_note_view_t n;
            assert(moenotes_score_line_member_at(s, lid, i, &n) == MOENOTES_OK);
            if (n.generated)
                assert(n.source_index == lid);
            int found = 0;
            for (size_t j = 0; j < moenotes_score_note_line_count(s, n.id); j++) {
                int32_t line;
                assert(moenotes_score_note_line_at(s, n.id, j, &line) == MOENOTES_OK);
                found |= line == lid;
            }
            assert(found);
        }
    }
    assert(moenotes_score_line_at(s, 2, &l0) == MOENOTES_ERR_RANGE);
    moenotes_score_free(s);
    /* Coincident guide branches retain each source line's canonical endpoints. */
    s = parse("{\"events\":{},\"notes\":[{\"type\":\"guide\",\"node\":["
              "{\"t\":0},{\"t\":480}]},{\"type\":\"guide\",\"node\":["
              "{\"t\":0},{\"t\":480}]}]}", 0, 0);
    assert(moenotes_score_line_member_count(s, 0) == 2);
    assert(moenotes_score_line_member_count(s, 1) == 2);
    moenotes_score_free(s);
}
static void test_ranges(void) {
    moenotes_position_t p;
    moenotes_line_sample_t sample;
    moenotes_line_view_t line;
    assert(moenotes_score_note_position_at_tick(NULL, 0, &p) == MOENOTES_ERR_INVALID_ARGUMENT);
    assert(moenotes_score_line_at(NULL, 0, &line) == MOENOTES_ERR_INVALID_ARGUMENT);
    assert(moenotes_score_sample_judgement_line(NULL, 0, 0, &sample) == MOENOTES_ERR_INVALID_ARGUMENT);
    moenotes_score_t *s = parse("{\"events\":{\"bpm\":[{\"t\":0,\"bpm\":0.001}]},\"notes\":[]}", 0, 0);
    assert(moenotes_score_note_position_at_tick(s, INT32_MAX, &p) == MOENOTES_ERR_RANGE);
    assert(moenotes_score_note_position_at_tick(s, -1, &p) == MOENOTES_ERR_RANGE);
    assert(moenotes_score_note_position_at_tick(s, 0, NULL) == MOENOTES_ERR_INVALID_ARGUMENT);
    assert(moenotes_score_sample_judgement_line(s, 0, 0, &sample) == MOENOTES_ERR_RANGE);
    moenotes_score_free(s);
    /* Tick conversion fits, float32 creator time rounds to 2^31: reject before cast. */
    const char *large = "{\"events\":{\"bpm\":[{\"t\":0,\"bpm\":125}]},"
                        "\"notes\":[{\"t\":2147483647}]}";
    assert(moenotes_score_parse(large, strlen(large), NULL, NULL, &s, NULL, 0) == MOENOTES_ERR_RANGE);
    assert(!s);
}
static void test_indexed_ids(void) {
    const char *json = "{\"events\":{},\"notes\":["
        "{\"type\":\"long\",\"node\":[{\"t\":0},{\"t\":480}]},"
        "{\"type\":\"long\",\"node\":[{\"t\":0},{\"t\":960}]}]}";
    moenotes_parse_options_t options;
    moenotes_default_parse_options(&options);
    options.start_note_id = INT32_MAX - 100;
    options.slide_combo_unit = 8;
    moenotes_score_t *s = NULL;
    assert(moenotes_score_parse(json, strlen(json), &options, NULL, &s, NULL, 0) == MOENOTES_OK);
    moenotes_line_view_t first, second;
    assert(moenotes_score_line_at(s, 0, &first) == MOENOTES_OK);
    assert(moenotes_score_line_at(s, 1, &second) == MOENOTES_OK);
    assert(first.begin_note_id == options.start_note_id);
    assert(first.begin_note_id == second.begin_note_id);
    assert(moenotes_score_note_line_count(s, first.begin_note_id) == 2);
    int32_t line_id;
    assert(moenotes_score_note_line_at(s, first.begin_note_id, 1, &line_id) == MOENOTES_OK);
    assert(line_id == 1);
    /* The consumed source ID is absent even though its canonical note is shared. */
    assert(moenotes_score_note_line_count(s, options.start_note_id + 2) == 0);
    assert(moenotes_score_note_line_at(s, options.start_note_id + 2, 0, &line_id) == MOENOTES_ERR_RANGE);
    assert(moenotes_score_note_line_at(s, first.begin_note_id, 2, &line_id) == MOENOTES_ERR_RANGE);
    assert(moenotes_score_note_line_count(s, options.start_note_id - 1) == 0);
    moenotes_command_t command;
    size_t count = moenotes_score_command_count(s);
    assert(moenotes_score_command_at(s, count - 1, 3, 100, 5, &command) == MOENOTES_OK);
    assert(command.note_id >= options.start_note_id && command.current_combo == 5 + (int32_t)count - 1);
    assert(moenotes_score_command_at(s, count, 0, 0, 0, &command) == MOENOTES_ERR_RANGE);
    moenotes_score_free(s);
}
int main(void) {
    test_clocks_and_thresholds();
    test_mirror_and_sampling();
    test_pairing();
    test_shared_branches_and_indices();
    test_ranges();
    test_indexed_ids();
    puts("conversion semantics regressions passed");
    return 0;
}
