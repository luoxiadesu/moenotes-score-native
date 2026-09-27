#ifndef MOENOTES_CHART_PARSER_H
#define MOENOTES_CHART_PARSER_H

#include <stddef.h>
#include <stdint.h>

#define MOENOTES_VERSION_MAJOR 0
#define MOENOTES_VERSION_MINOR 3
#define MOENOTES_VERSION_PATCH 0
#define MOENOTES_VERSION_STRING "0.3.0"

/* Public v0.3.0 contract: see docs/api.md for ownership and migration. */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct moenotes_score moenotes_score_t;

typedef enum moenotes_result {
    MOENOTES_OK = 0,
    MOENOTES_ERR_INVALID_ARGUMENT = 1,
    MOENOTES_ERR_OUT_OF_MEMORY = 2,
    MOENOTES_ERR_GZIP = 3,
    MOENOTES_ERR_JSON = 4,
    MOENOTES_ERR_SCHEMA = 5,
    MOENOTES_ERR_RANGE = 6,
    MOENOTES_ERR_INTERNAL = 7,
    MOENOTES_ERR_UNSUPPORTED = 8
} moenotes_result_t;

typedef void *(*moenotes_malloc_fn)(void *ctx, size_t size);
typedef void *(*moenotes_realloc_fn)(void *ctx, void *ptr, size_t size);
typedef void (*moenotes_free_fn)(void *ctx, void *ptr);

typedef struct moenotes_allocator {
    void *ctx;
    moenotes_malloc_fn malloc_fn;
    moenotes_realloc_fn realloc_fn;
    moenotes_free_fn free_fn;
} moenotes_allocator_t;

typedef struct moenotes_parse_options {
    int32_t start_note_id;
    uint32_t slide_combo_unit; /* 0 disables, 8 enables relative eighth notes. */
    uint8_t mirror;
    uint8_t add_flick_hidden;
} moenotes_parse_options_t;

typedef struct moenotes_position {
    int32_t bar;
    int32_t rhythm;
    int32_t rhythmic_unit;
    double bar_progress;
    int32_t time_ms;
} moenotes_position_t;

typedef enum moenotes_operate_type {
    MOENOTES_OP_NONE = 0,
    MOENOTES_OP_NORMAL = 1,
    MOENOTES_OP_SLIDE_BEGIN = 20,
    MOENOTES_OP_SLIDE_CONNECTION = 21,
    MOENOTES_OP_SLIDE_END = 22,
    MOENOTES_OP_FLICK = 40,
    MOENOTES_OP_SLIDE_BEGIN_FLICK = 41,
    MOENOTES_OP_SLIDE_END_FLICK = 42,
    MOENOTES_OP_TRACE = 60,
    MOENOTES_OP_SLIDE_BEGIN_TRACE = 61,
    MOENOTES_OP_SLIDE_END_TRACE = 62,
    MOENOTES_OP_SLIDE_CONNECTION_TRACE = 63,
    MOENOTES_OP_HIDDEN_SLIDE_BEGIN = 80,
    MOENOTES_OP_HIDDEN_SLIDE_END = 82,
    MOENOTES_OP_GUIDE_BEGIN = 100,
    MOENOTES_OP_GUIDE_BEGIN_NORMAL = 101,
    MOENOTES_OP_GUIDE_BEGIN_FLICK = 102,
    MOENOTES_OP_GUIDE_END = 103,
    MOENOTES_OP_GUIDE_BEGIN_TRACE = 104,
    MOENOTES_OP_GUIDE_END_TRACE = 105,
    MOENOTES_OP_COMBO = 120,
    MOENOTES_OP_COMBO_SKIP = 121,
    MOENOTES_OP_HIDDEN = 122,
    MOENOTES_OP_INVALID_HIDDEN = 123
} moenotes_operate_type_t;

typedef enum moenotes_ease {
    MOENOTES_EASE_LINEAR = 0,
    MOENOTES_EASE_OUT = 1,
    MOENOTES_EASE_IN = 2
} moenotes_ease_t;

typedef enum moenotes_direction {
    MOENOTES_DIRECTION_NORMAL = 0,
    MOENOTES_DIRECTION_LEFT = 1,
    MOENOTES_DIRECTION_RIGHT = 2
} moenotes_direction_t;

typedef enum moenotes_alpha {
    MOENOTES_ALPHA_NONE = 0,
    MOENOTES_ALPHA_FADE_IN = 1,
    MOENOTES_ALPHA_FADE_OUT = 2
} moenotes_alpha_t;

typedef struct moenotes_note_view {
    int32_t id;
    moenotes_operate_type_t operate_type;
    int32_t tick;
    moenotes_position_t position;
    int32_t lane_count;
    int32_t lane_start;
    int32_t lane_end;
    double lane_start_float;
    double lane_end_float;
    double width;
    uint8_t critical;
    uint8_t visible;
    uint8_t slide_along;
    uint8_t pos_auto;
    int32_t pair_note_id;
    int32_t parent_note_id;
    int32_t hidden_for_note_id;
    int32_t line_id;
    int32_t source_index;
    moenotes_direction_t direction;
    moenotes_ease_t ease_left;
    moenotes_ease_t ease_right;
    moenotes_alpha_t alpha;
    int32_t line_index; /* Reusable line slot; -1 for standalone notes. */
    uint8_t generated;
} moenotes_note_view_t;

typedef enum moenotes_event_type {
    MOENOTES_EVENT_SKILL = 0,
    MOENOTES_EVENT_FEVER = 1,
    MOENOTES_EVENT_CALL = 2
} moenotes_event_type_t;

typedef struct moenotes_event {
    moenotes_event_type_t type;
    int32_t tick;
    int32_t end_tick; /* Fever end, otherwise equal to tick. */
    moenotes_position_t position;
    moenotes_position_t end_position;
    size_t value_count; /* Call timing entries, accessed separately. */
} moenotes_event_t;

typedef struct moenotes_line_sample {
    double lane_start;
    double lane_end; /* Inclusive, matching note lane_end_float. */
    double width;
} moenotes_line_sample_t;

/* Source-branch endpoints; shared canonical notes can have several branches. */
typedef struct moenotes_line_view {
    int32_t id;
    int32_t line_index;
    int32_t source_index;
    int32_t begin_note_id;
    int32_t end_note_id;
    uint8_t guide;
} moenotes_line_view_t;

typedef enum moenotes_warning {
    MOENOTES_WARNING_NONE = 0,
    MOENOTES_WARNING_NONMONOTONIC_LINE = 1u << 0,
    MOENOTES_WARNING_SHARED_ENDPOINT = 1u << 1
} moenotes_warning_t;

typedef struct moenotes_bpm_event {
    int32_t tick;
    double bpm;
    moenotes_position_t position;
} moenotes_bpm_event_t;

typedef struct moenotes_signature_event {
    int32_t tick;
    int32_t numerator;
    int32_t denominator;
    moenotes_position_t position;
} moenotes_signature_event_t;

typedef struct moenotes_command {
    int32_t time_ms;
    int32_t current_combo;
    int32_t current_life;
    int32_t note_id;
    moenotes_operate_type_t operate_type;
    int32_t score_type;
} moenotes_command_t;

/* Static library-version string; never free the returned pointer. */
const char *moenotes_version_string(void);
void moenotes_default_parse_options(moenotes_parse_options_t *options);
const char *moenotes_result_string(moenotes_result_t result);
const char *moenotes_operate_type_name(moenotes_operate_type_t type);
uint8_t moenotes_operate_type_is_judgement(moenotes_operate_type_t type);

/* Input is borrowed only during this call. On failure, *out_score is NULL. */
moenotes_result_t moenotes_score_parse(const void *data, size_t size,
                                       const moenotes_parse_options_t *options,
                                       const moenotes_allocator_t *allocator,
                                       moenotes_score_t **out_score, char *error_message,
                                       size_t error_message_size);
/* Accepts NULL. No access to the score is valid after this call. */
void moenotes_score_free(moenotes_score_t *score);

size_t moenotes_score_note_count(const moenotes_score_t *score);
moenotes_result_t moenotes_score_note_at(const moenotes_score_t *score, size_t index,
                                         moenotes_note_view_t *out_note);
/* Same enumeration and IDs, with geometry before final-line processing. */
moenotes_result_t moenotes_score_source_note_at(const moenotes_score_t *score, size_t index,
                                                moenotes_note_view_t *out_note);
int32_t moenotes_score_lane_count(const moenotes_score_t *score);
/* Nonzero flags identify inputs that need additional native graph validation. */
uint32_t moenotes_score_warnings(const moenotes_score_t *score);
size_t moenotes_score_bpm_count(const moenotes_score_t *score);
moenotes_result_t moenotes_score_bpm_at(const moenotes_score_t *score, size_t index,
                                        moenotes_bpm_event_t *out_event);
size_t moenotes_score_signature_count(const moenotes_score_t *score);
moenotes_result_t moenotes_score_signature_at(const moenotes_score_t *score, size_t index,
                                              moenotes_signature_event_t *out_event);
moenotes_result_t moenotes_score_position_at_tick(const moenotes_score_t *score, int32_t tick,
                                                  moenotes_position_t *out_position);
/* Source-note clock; unlike position_at_tick, uses the creator's bar-time path. */
moenotes_result_t moenotes_score_note_position_at_tick(const moenotes_score_t *score, int32_t tick,
                                                       moenotes_position_t *out_position);
size_t moenotes_score_event_count(const moenotes_score_t *score);
moenotes_result_t moenotes_score_event_at(const moenotes_score_t *score, size_t index,
                                          moenotes_event_t *out_event);
moenotes_result_t moenotes_score_event_value_at(const moenotes_score_t *score, size_t event_index,
                                                size_t value_index, int32_t *out_value);
/* Call entries equal to 1 become float32 (index + 1) / timing_count. */
size_t moenotes_score_call_rhythm_count(const moenotes_score_t *score, size_t event_index);
moenotes_result_t moenotes_score_call_rhythm_at(const moenotes_score_t *score, size_t event_index,
                                               size_t rhythm_index, double *out_progress);
/* Tick-clock bar heads through the last source-note bar, inclusive. */
size_t moenotes_score_bar_line_count(const moenotes_score_t *score);
moenotes_result_t moenotes_score_bar_line_at(const moenotes_score_t *score, size_t index,
                                            moenotes_position_t *out_position);
/* Final notes sharing the greatest float32 (bar + progress) position key. */
size_t moenotes_score_last_timing_note_count(const moenotes_score_t *score);
moenotes_result_t moenotes_score_last_timing_note_at(const moenotes_score_t *score, size_t index,
                                                    moenotes_note_view_t *out_note);
/* First Fever event containing this note's time (inclusive), or -1. */
moenotes_result_t moenotes_score_note_fever_event(const moenotes_score_t *score, int32_t note_id,
                                                 int32_t *out_event_index);
size_t moenotes_score_line_count(const moenotes_score_t *score);
moenotes_result_t moenotes_score_line_at(const moenotes_score_t *score, size_t index,
                                         moenotes_line_view_t *out_line);
size_t moenotes_score_note_line_count(const moenotes_score_t *score, int32_t note_id);
moenotes_result_t moenotes_score_note_line_at(const moenotes_score_t *score, int32_t note_id,
                                              size_t index, int32_t *out_line_id);
/* Samples source geometry in tick space, not gameplay judgement geometry. */
moenotes_result_t moenotes_score_sample_line(const moenotes_score_t *score, int32_t line_id,
                                             int32_t tick, moenotes_line_sample_t *out_sample);
/* Experimental creator geometry: note time and a single source-left easing.
 * This is interpolation only, not a full gameplay judgement-area evaluator. */
moenotes_result_t moenotes_score_sample_judgement_line(const moenotes_score_t *score, int32_t line_id,
                                                       int32_t tick, moenotes_line_sample_t *out_sample);
size_t moenotes_score_line_member_count(const moenotes_score_t *score, int32_t line_id);
moenotes_result_t moenotes_score_line_member_at(const moenotes_score_t *score, int32_t line_id,
                                                size_t index, moenotes_note_view_t *out_note);
uint32_t moenotes_score_full_combo_count(const moenotes_score_t *score,
                                         uint8_t include_slide_combos,
                                         uint8_t include_hidden_flick_nodes);
size_t moenotes_score_command_count(const moenotes_score_t *score);
moenotes_result_t moenotes_score_command_at(const moenotes_score_t *score, size_t index,
                                            int32_t score_type, int32_t current_life,
                                            int32_t current_combo, moenotes_command_t *out_command);

#ifdef __cplusplus
}
#endif
#endif
