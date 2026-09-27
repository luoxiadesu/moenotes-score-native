#include "moenotes_chart_parser.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* v0.3.0 keeps the existing declaration layouts; behavioral migration is documented. */
#define ENUM_VALUE(name, value) _Static_assert(name == value, #name " changed")
ENUM_VALUE(MOENOTES_OK, 0);
ENUM_VALUE(MOENOTES_ERR_INVALID_ARGUMENT, 1);
ENUM_VALUE(MOENOTES_ERR_OUT_OF_MEMORY, 2);
ENUM_VALUE(MOENOTES_ERR_GZIP, 3);
ENUM_VALUE(MOENOTES_ERR_JSON, 4);
ENUM_VALUE(MOENOTES_ERR_SCHEMA, 5);
ENUM_VALUE(MOENOTES_ERR_RANGE, 6);
ENUM_VALUE(MOENOTES_ERR_INTERNAL, 7);
ENUM_VALUE(MOENOTES_ERR_UNSUPPORTED, 8);
ENUM_VALUE(MOENOTES_OP_NONE, 0);
ENUM_VALUE(MOENOTES_OP_NORMAL, 1);
ENUM_VALUE(MOENOTES_OP_SLIDE_BEGIN, 20);
ENUM_VALUE(MOENOTES_OP_SLIDE_CONNECTION, 21);
ENUM_VALUE(MOENOTES_OP_SLIDE_END, 22);
ENUM_VALUE(MOENOTES_OP_FLICK, 40);
ENUM_VALUE(MOENOTES_OP_SLIDE_BEGIN_FLICK, 41);
ENUM_VALUE(MOENOTES_OP_SLIDE_END_FLICK, 42);
ENUM_VALUE(MOENOTES_OP_TRACE, 60);
ENUM_VALUE(MOENOTES_OP_SLIDE_BEGIN_TRACE, 61);
ENUM_VALUE(MOENOTES_OP_SLIDE_END_TRACE, 62);
ENUM_VALUE(MOENOTES_OP_SLIDE_CONNECTION_TRACE, 63);
ENUM_VALUE(MOENOTES_OP_HIDDEN_SLIDE_BEGIN, 80);
ENUM_VALUE(MOENOTES_OP_HIDDEN_SLIDE_END, 82);
ENUM_VALUE(MOENOTES_OP_GUIDE_BEGIN, 100);
ENUM_VALUE(MOENOTES_OP_GUIDE_BEGIN_NORMAL, 101);
ENUM_VALUE(MOENOTES_OP_GUIDE_BEGIN_FLICK, 102);
ENUM_VALUE(MOENOTES_OP_GUIDE_END, 103);
ENUM_VALUE(MOENOTES_OP_GUIDE_BEGIN_TRACE, 104);
ENUM_VALUE(MOENOTES_OP_GUIDE_END_TRACE, 105);
ENUM_VALUE(MOENOTES_OP_COMBO, 120);
ENUM_VALUE(MOENOTES_OP_COMBO_SKIP, 121);
ENUM_VALUE(MOENOTES_OP_HIDDEN, 122);
ENUM_VALUE(MOENOTES_OP_INVALID_HIDDEN, 123);
ENUM_VALUE(MOENOTES_EASE_LINEAR, 0);
ENUM_VALUE(MOENOTES_EASE_OUT, 1);
ENUM_VALUE(MOENOTES_EASE_IN, 2);
ENUM_VALUE(MOENOTES_DIRECTION_NORMAL, 0);
ENUM_VALUE(MOENOTES_DIRECTION_LEFT, 1);
ENUM_VALUE(MOENOTES_DIRECTION_RIGHT, 2);
ENUM_VALUE(MOENOTES_ALPHA_NONE, 0);
ENUM_VALUE(MOENOTES_ALPHA_FADE_IN, 1);
ENUM_VALUE(MOENOTES_ALPHA_FADE_OUT, 2);
ENUM_VALUE(MOENOTES_EVENT_SKILL, 0);
ENUM_VALUE(MOENOTES_EVENT_FEVER, 1);
ENUM_VALUE(MOENOTES_EVENT_CALL, 2);
ENUM_VALUE(MOENOTES_WARNING_NONE, 0);
ENUM_VALUE(MOENOTES_WARNING_NONMONOTONIC_LINE, 1);
ENUM_VALUE(MOENOTES_WARNING_SHARED_ENDPOINT, 2);

#define ALLOCATOR(X, S) \
    X(S, void *, ctx) \
    X(S, moenotes_malloc_fn, malloc_fn) \
    X(S, moenotes_realloc_fn, realloc_fn) \
    X(S, moenotes_free_fn, free_fn)
#define OPTIONS(X, S) \
    X(S, int32_t, start_note_id) \
    X(S, uint32_t, slide_combo_unit) \
    X(S, uint8_t, mirror) \
    X(S, uint8_t, add_flick_hidden)
#define POSITION(X, S) \
    X(S, int32_t, bar) \
    X(S, int32_t, rhythm) \
    X(S, int32_t, rhythmic_unit) \
    X(S, double, bar_progress) \
    X(S, int32_t, time_ms)
#define NOTE(X, S) \
    X(S, int32_t, id) \
    X(S, moenotes_operate_type_t, operate_type) \
    X(S, int32_t, tick) \
    X(S, moenotes_position_t, position) \
    X(S, int32_t, lane_count) \
    X(S, int32_t, lane_start) \
    X(S, int32_t, lane_end) \
    X(S, double, lane_start_float) \
    X(S, double, lane_end_float) \
    X(S, double, width) \
    X(S, uint8_t, critical) \
    X(S, uint8_t, visible) \
    X(S, uint8_t, slide_along) \
    X(S, uint8_t, pos_auto) \
    X(S, int32_t, pair_note_id) \
    X(S, int32_t, parent_note_id) \
    X(S, int32_t, hidden_for_note_id) \
    X(S, int32_t, line_id) \
    X(S, int32_t, source_index) \
    X(S, moenotes_direction_t, direction) \
    X(S, moenotes_ease_t, ease_left) \
    X(S, moenotes_ease_t, ease_right) \
    X(S, moenotes_alpha_t, alpha) \
    X(S, int32_t, line_index) \
    X(S, uint8_t, generated)
#define EVENT(X, S) \
    X(S, moenotes_event_type_t, type) \
    X(S, int32_t, tick) \
    X(S, int32_t, end_tick) \
    X(S, moenotes_position_t, position) \
    X(S, moenotes_position_t, end_position) \
    X(S, size_t, value_count)
#define SAMPLE(X, S) \
    X(S, double, lane_start) \
    X(S, double, lane_end) \
    X(S, double, width)
#define LINE(X, S) \
    X(S, int32_t, id) \
    X(S, int32_t, line_index) \
    X(S, int32_t, source_index) \
    X(S, int32_t, begin_note_id) \
    X(S, int32_t, end_note_id) \
    X(S, uint8_t, guide)
#define BPM(X, S) \
    X(S, int32_t, tick) \
    X(S, double, bpm) \
    X(S, moenotes_position_t, position)
#define SIGNATURE(X, S) \
    X(S, int32_t, tick) \
    X(S, int32_t, numerator) \
    X(S, int32_t, denominator) \
    X(S, moenotes_position_t, position)
#define COMMAND(X, S) \
    X(S, int32_t, time_ms) \
    X(S, int32_t, current_combo) \
    X(S, int32_t, current_life) \
    X(S, int32_t, note_id) \
    X(S, moenotes_operate_type_t, operate_type) \
    X(S, int32_t, score_type)

#define DECLARE_FIELD(S, T, F) T F;
#define CHECK_FIELD(S, T, F) \
    _Static_assert(offsetof(S, F) == offsetof(struct baseline_##S, F), #S "." #F " offset"); \
    _Static_assert(_Generic(&((S *)0)->F, T *: 1, default: 0), #S "." #F " type");
#define CHECK_LAYOUT(S, FIELDS) \
    struct baseline_##S { FIELDS(DECLARE_FIELD, S) }; \
    _Static_assert(sizeof(S) == sizeof(struct baseline_##S), #S " size"); \
    _Static_assert(_Alignof(S) == _Alignof(struct baseline_##S), #S " alignment"); \
    FIELDS(CHECK_FIELD, S)
CHECK_LAYOUT(moenotes_allocator_t, ALLOCATOR)
CHECK_LAYOUT(moenotes_parse_options_t, OPTIONS)
CHECK_LAYOUT(moenotes_position_t, POSITION)
CHECK_LAYOUT(moenotes_note_view_t, NOTE)
CHECK_LAYOUT(moenotes_event_t, EVENT)
CHECK_LAYOUT(moenotes_line_sample_t, SAMPLE)
CHECK_LAYOUT(moenotes_line_view_t, LINE)
CHECK_LAYOUT(moenotes_bpm_event_t, BPM)
CHECK_LAYOUT(moenotes_signature_event_t, SIGNATURE)
CHECK_LAYOUT(moenotes_command_t, COMMAND)

#define FUNCTION(name, ret, ...) \
    _Static_assert(_Generic(&(name), ret (*)(__VA_ARGS__): 1, default: 0), #name " signature")
FUNCTION(moenotes_version_string, const char *, void);
FUNCTION(moenotes_default_parse_options, void, moenotes_parse_options_t *);
FUNCTION(moenotes_result_string, const char *, moenotes_result_t);
FUNCTION(moenotes_operate_type_name, const char *, moenotes_operate_type_t);
FUNCTION(moenotes_operate_type_is_judgement, uint8_t, moenotes_operate_type_t);
FUNCTION(moenotes_score_parse, moenotes_result_t, const void *, size_t,
         const moenotes_parse_options_t *, const moenotes_allocator_t *,
         moenotes_score_t **, char *, size_t);
FUNCTION(moenotes_score_free, void, moenotes_score_t *);
FUNCTION(moenotes_score_note_count, size_t, const moenotes_score_t *);
FUNCTION(moenotes_score_note_at, moenotes_result_t, const moenotes_score_t *, size_t,
         moenotes_note_view_t *);
FUNCTION(moenotes_score_lane_count, int32_t, const moenotes_score_t *);
FUNCTION(moenotes_score_warnings, uint32_t, const moenotes_score_t *);
FUNCTION(moenotes_score_bpm_count, size_t, const moenotes_score_t *);
FUNCTION(moenotes_score_bpm_at, moenotes_result_t, const moenotes_score_t *, size_t,
         moenotes_bpm_event_t *);
FUNCTION(moenotes_score_signature_count, size_t, const moenotes_score_t *);
FUNCTION(moenotes_score_signature_at, moenotes_result_t, const moenotes_score_t *, size_t,
         moenotes_signature_event_t *);
FUNCTION(moenotes_score_position_at_tick, moenotes_result_t, const moenotes_score_t *, int32_t,
         moenotes_position_t *);
FUNCTION(moenotes_score_note_position_at_tick, moenotes_result_t, const moenotes_score_t *, int32_t,
         moenotes_position_t *);
FUNCTION(moenotes_score_event_count, size_t, const moenotes_score_t *);
FUNCTION(moenotes_score_source_note_at, moenotes_result_t, const moenotes_score_t *, size_t,
         moenotes_note_view_t *);
FUNCTION(moenotes_score_call_rhythm_count, size_t, const moenotes_score_t *, size_t);
FUNCTION(moenotes_score_call_rhythm_at, moenotes_result_t, const moenotes_score_t *, size_t,
         size_t, double *);
FUNCTION(moenotes_score_bar_line_count, size_t, const moenotes_score_t *);
FUNCTION(moenotes_score_bar_line_at, moenotes_result_t, const moenotes_score_t *, size_t,
         moenotes_position_t *);
FUNCTION(moenotes_score_last_timing_note_count, size_t, const moenotes_score_t *);
FUNCTION(moenotes_score_last_timing_note_at, moenotes_result_t, const moenotes_score_t *, size_t,
         moenotes_note_view_t *);
FUNCTION(moenotes_score_note_fever_event, moenotes_result_t, const moenotes_score_t *, int32_t,
         int32_t *);
FUNCTION(moenotes_score_event_at, moenotes_result_t, const moenotes_score_t *, size_t,
         moenotes_event_t *);
FUNCTION(moenotes_score_event_value_at, moenotes_result_t, const moenotes_score_t *, size_t,
         size_t, int32_t *);
FUNCTION(moenotes_score_line_count, size_t, const moenotes_score_t *);
FUNCTION(moenotes_score_line_at, moenotes_result_t, const moenotes_score_t *, size_t,
         moenotes_line_view_t *);
FUNCTION(moenotes_score_note_line_count, size_t, const moenotes_score_t *, int32_t);
FUNCTION(moenotes_score_note_line_at, moenotes_result_t, const moenotes_score_t *, int32_t,
         size_t, int32_t *);
FUNCTION(moenotes_score_sample_line, moenotes_result_t, const moenotes_score_t *, int32_t,
         int32_t, moenotes_line_sample_t *);
FUNCTION(moenotes_score_sample_judgement_line, moenotes_result_t, const moenotes_score_t *, int32_t,
         int32_t, moenotes_line_sample_t *);
FUNCTION(moenotes_score_line_member_count, size_t, const moenotes_score_t *, int32_t);
FUNCTION(moenotes_score_line_member_at, moenotes_result_t, const moenotes_score_t *, int32_t,
         size_t, moenotes_note_view_t *);
FUNCTION(moenotes_score_full_combo_count, uint32_t, const moenotes_score_t *, uint8_t, uint8_t);
FUNCTION(moenotes_score_command_count, size_t, const moenotes_score_t *);
FUNCTION(moenotes_score_command_at, moenotes_result_t, const moenotes_score_t *, size_t,
         int32_t, int32_t, int32_t, moenotes_command_t *);
_Static_assert(_Generic((moenotes_malloc_fn)0, void *(*)(void *, size_t): 1, default: 0),
               "malloc callback");
_Static_assert(_Generic((moenotes_realloc_fn)0, void *(*)(void *, void *, size_t): 1, default: 0),
               "realloc callback");
_Static_assert(_Generic((moenotes_free_fn)0, void (*)(void *, void *): 1, default: 0),
               "free callback");

static void test_null_accessors(void) {
    moenotes_note_view_t note;
    moenotes_position_t pos;
    moenotes_bpm_event_t bpm;
    moenotes_signature_event_t sig;
    moenotes_event_t event;
    moenotes_line_sample_t sample;
    moenotes_command_t command;
    int32_t value;
    assert(moenotes_score_note_count(NULL) == 0);
    assert(moenotes_score_lane_count(NULL) == 0);
    assert(moenotes_score_warnings(NULL) == 0);
    assert(moenotes_score_bpm_count(NULL) == 0);
    assert(moenotes_score_signature_count(NULL) == 0);
    assert(moenotes_score_event_count(NULL) == 0);
    assert(moenotes_score_line_count(NULL) == 0);
    assert(moenotes_score_note_line_count(NULL, 0) == 0);
    assert(moenotes_score_line_member_count(NULL, 0) == 0);
    assert(moenotes_score_full_combo_count(NULL, 1, 1) == 0);
    assert(moenotes_score_command_count(NULL) == 0);
    assert(moenotes_score_note_at(NULL, 0, &note) == MOENOTES_ERR_INVALID_ARGUMENT);
    assert(moenotes_score_bpm_at(NULL, 0, &bpm) == MOENOTES_ERR_INVALID_ARGUMENT);
    assert(moenotes_score_signature_at(NULL, 0, &sig) == MOENOTES_ERR_INVALID_ARGUMENT);
    assert(moenotes_score_position_at_tick(NULL, 0, &pos) == MOENOTES_ERR_INVALID_ARGUMENT);
    assert(moenotes_score_event_at(NULL, 0, &event) == MOENOTES_ERR_INVALID_ARGUMENT);
    assert(moenotes_score_event_value_at(NULL, 0, 0, &value) == MOENOTES_ERR_INVALID_ARGUMENT);
    assert(moenotes_score_note_line_at(NULL, 0, 0, &value) == MOENOTES_ERR_INVALID_ARGUMENT);
    assert(moenotes_score_line_member_at(NULL, 0, 0, &note) == MOENOTES_ERR_INVALID_ARGUMENT);
    assert(moenotes_score_sample_line(NULL, 0, 0, &sample) == MOENOTES_ERR_INVALID_ARGUMENT);
    assert(moenotes_score_command_at(NULL, 0, 0, 0, 0, &command) == MOENOTES_ERR_INVALID_ARGUMENT);
    moenotes_score_free(NULL);
    moenotes_default_parse_options(NULL);
}

static void test_contract(void) {
    char input[] = "{\"events\":{},\"notes\":[{\"t\":960},{\"t\":480,\"pos\":2}]}";
    moenotes_parse_options_t options;
    memset(&options, 0xff, sizeof(options));
    moenotes_default_parse_options(&options);
    assert(options.start_note_id == 0 && options.slide_combo_unit == 0 &&
           options.mirror == 0 && options.add_flick_hidden == 0);
    options.start_note_id = 42;
    moenotes_score_t *score = NULL;
    char error[32] = "previous error";
    assert(moenotes_score_parse(input, sizeof(input) - 1, &options, NULL,
                               &score, error, sizeof(error)) == MOENOTES_OK);
    assert(error[0] == '\0');
    memset(input, 0, sizeof(input));
    assert(moenotes_score_note_count(score) == 2 && moenotes_score_lane_count(score) == 24);
    assert(moenotes_score_warnings(score) == MOENOTES_WARNING_NONE);
    moenotes_note_view_t note;
    assert(moenotes_score_note_at(score, 0, &note) == MOENOTES_OK);
    assert(note.id == 42 && note.tick == 480 && note.position.time_ms == 500);
    assert(note.lane_start_float == 2 && note.width == 6 && note.lane_end_float == 7);
    assert(note.parent_note_id == -1 && note.line_id == -1 && note.line_index == -1);
    assert(note.source_index == 1 && !note.generated && note.visible);
    assert(moenotes_score_note_at(score, 2, &note) == MOENOTES_ERR_RANGE);
    assert(moenotes_score_note_at(score, 0, NULL) == MOENOTES_ERR_INVALID_ARGUMENT);
    moenotes_bpm_event_t bpm;
    moenotes_signature_event_t sig;
    assert(moenotes_score_bpm_count(score) == 1);
    assert(moenotes_score_bpm_at(score, 0, &bpm) == MOENOTES_OK && bpm.bpm == 120);
    assert(moenotes_score_signature_count(score) == 1);
    assert(moenotes_score_signature_at(score, 0, &sig) == MOENOTES_OK &&
           sig.numerator == 4 && sig.denominator == 4);
    moenotes_position_t position;
    assert(moenotes_score_position_at_tick(score, 1920, &position) == MOENOTES_OK);
    assert(position.bar == 1 && position.rhythm == 0 && position.rhythmic_unit == 1920 &&
           position.time_ms == 2000);
    assert(moenotes_score_position_at_tick(score, -1, &position) == MOENOTES_ERR_RANGE);
    assert(moenotes_score_note_line_count(score, -1) == 0);
    assert(moenotes_score_line_member_count(score, -1) == 0);
    assert(moenotes_score_command_count(score) == 2);
    moenotes_command_t command;
    assert(moenotes_score_command_at(score, 1, 7, 100, 10, &command) == MOENOTES_OK);
    assert(command.current_combo == 11 && command.current_life == 100 && command.score_type == 7);
    assert(command.time_ms == 1000 && command.note_id == 43);
    assert(moenotes_score_command_at(score, 1, 0, 0, INT32_MAX, &command) == MOENOTES_ERR_RANGE);
    assert(moenotes_score_note_at(score, 0, &note) == MOENOTES_OK);
    moenotes_score_free(score);
    assert(note.tick == 480 && note.width == 6);

    score = (moenotes_score_t *)&options;
    error[0] = 'x';
    assert(moenotes_score_parse(NULL, 0, NULL, NULL, &score, error, 1) ==
           MOENOTES_ERR_INVALID_ARGUMENT);
    assert(score == NULL && error[0] == '\0');
    options.slide_combo_unit = 4;
    assert(moenotes_score_parse("{}", 2, &options, NULL, &score, NULL, 0) ==
           MOENOTES_ERR_UNSUPPORTED);
    assert(score == NULL);
    assert(moenotes_operate_type_is_judgement(MOENOTES_OP_NORMAL) == 1);
    assert(moenotes_operate_type_is_judgement(MOENOTES_OP_COMBO) == 1);
    assert(moenotes_operate_type_is_judgement(MOENOTES_OP_COMBO_SKIP) == 0);
    assert(moenotes_operate_type_is_judgement((moenotes_operate_type_t)999) == 0);
    assert(moenotes_operate_type_is_judgement((moenotes_operate_type_t)-1) == 0);
    assert(strcmp(moenotes_operate_type_name((moenotes_operate_type_t)999), "none") == 0);
}

int main(void) {
    char version[64];
    snprintf(version, sizeof(version), "%d.%d.%d", MOENOTES_VERSION_MAJOR,
             MOENOTES_VERSION_MINOR, MOENOTES_VERSION_PATCH);
    assert(strcmp(version, MOENOTES_VERSION_STRING) == 0);
    assert(strcmp(version, EXPECTED_VERSION) == 0);
    assert(strcmp(version, moenotes_version_string()) == 0);
    test_null_accessors();
    test_contract();
    puts("v0.3.0 API contract passed");
    return 0;
}
