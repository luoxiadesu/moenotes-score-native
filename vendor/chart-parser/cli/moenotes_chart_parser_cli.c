#include "moenotes_chart_parser.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long n = ftell(f);
    if (n < 0 || n > 64L * 1024 * 1024 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    unsigned char *p = malloc((size_t)n + 1);
    if (!p) {
        fclose(f);
        return NULL;
    }
    if ((size_t)n && fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return p;
}

static void usage(const char *name) {
    fprintf(stderr, "usage: %s parse FILE [--mirror] [--combo-unit 0|8] [--flick-hidden]\n", name);
    fprintf(stderr, "       %s --help | --version\n", name);
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--version")) {
        printf("moenotes-chart-parser %s\n", moenotes_version_string());
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        usage(argv[0]);
        return 0;
    }
    if (argc < 3 || strcmp(argv[1], "parse") != 0) {
        usage(argv[0]);
        return 2;
    }
    moenotes_parse_options_t options;
    moenotes_default_parse_options(&options);
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--mirror"))
            options.mirror = 1;
        else if (!strcmp(argv[i], "--flick-hidden"))
            options.add_flick_hidden = 1;
        else if (!strcmp(argv[i], "--combo-unit") && i + 1 < argc) {
            const char *value = argv[++i];
            if (strcmp(value, "0") && strcmp(value, "8")) {
                usage(argv[0]);
                return 2;
            }
            options.slide_combo_unit = (uint32_t)strtoul(value, NULL, 10);
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    size_t size = 0;
    unsigned char *data = read_file(argv[2], &size);
    if (!data) {
        fprintf(stderr, "cannot read %s\n", argv[2]);
        return 1;
    }
    moenotes_score_t *score = NULL;
    char error[256] = {0};
    moenotes_result_t result =
        moenotes_score_parse(data, size, &options, NULL, &score, error, sizeof(error));
    free(data);
    if (result != MOENOTES_OK) {
        fprintf(stderr, "parse failed: %s (%s)\n", moenotes_result_string(result), error);
        return 1;
    }
    printf("{\n  \"lane_count\": %d,\n  \"note_count\": %zu,\n  \"full_combo_count\": %u,\n  "
           "\"warnings\": %u,\n  \"notes\": [\n",
           moenotes_score_lane_count(score), moenotes_score_note_count(score),
           moenotes_score_full_combo_count(score, 1, 0), moenotes_score_warnings(score));
    for (size_t i = 0, n = moenotes_score_note_count(score); i < n; i++) {
        moenotes_note_view_t note, source;
        moenotes_score_note_at(score, i, &note);
        moenotes_score_source_note_at(score, i, &source);
        int32_t fever = -1;
        moenotes_score_note_fever_event(score, note.id, &fever);
        printf("    "
               "{\"id\":%d,\"type\":%d,\"type_name\":\"%s\",\"tick\":%d,\"time_ms\":%d,\"lane_"
               "start\":%d,\"lane_end\":%d,\"line_id\":%d,\"parent_note_id\":%d,\"pair_note_id\":%"
               "d,\"judgement\":%s,",
               note.id, (int)note.operate_type, moenotes_operate_type_name(note.operate_type),
               note.tick, note.position.time_ms, note.lane_start, note.lane_end, note.line_id,
               note.parent_note_id, note.pair_note_id,
               moenotes_operate_type_is_judgement(note.operate_type) ? "true" : "false");
        printf("\"lane_start_float\":%.9g,\"lane_end_float\":%.9g,\"width\":%.9g,\"direction\":%d,"
               "\"ease_left\":%d,\"ease_right\":%d,\"alpha\":%d,\"critical\":%u,\"visible\":%u,"
               "\"pos_auto\":%u,\"generated\":%u,\"line_index\":%d,\"hidden_for_note_id\":%d,"
               "\"source_index\":%d,\"bar\":%d,\"rhythm\":%d,\"rhythmic_unit\":%d,"
               "\"bar_progress\":%.9g,\"slide_along\":%u,\"fever_event_index\":%d,"
               "\"source_geometry\":{\"lane_start\":%d,\"lane_end\":%d,"
               "\"lane_start_float\":%.9g,\"lane_end_float\":%.9g,\"width\":%.9g}}%s\n",
               note.lane_start_float, note.lane_end_float, note.width, note.direction,
               note.ease_left, note.ease_right, note.alpha, note.critical, note.visible,
               note.pos_auto, note.generated, note.line_index, note.hidden_for_note_id,
               note.source_index, note.position.bar, note.position.rhythm,
               note.position.rhythmic_unit, note.position.bar_progress, note.slide_along,
               fever, source.lane_start, source.lane_end,
               source.lane_start_float, source.lane_end_float, source.width,
               i + 1 == n ? "" : ",");
    }
    printf("  ],\n  \"lines\": [");
    for (size_t i = 0, count = moenotes_score_line_count(score); i < count; i++) {
        moenotes_line_view_t line;
        moenotes_score_line_at(score, i, &line);
        printf("%s{\"id\":%d,\"line_index\":%d,\"source_index\":%d,\"begin_note_id\":%d,"
               "\"end_note_id\":%d,\"guide\":%s,\"members\":[", i ? "," : "", line.id,
               line.line_index, line.source_index, line.begin_note_id, line.end_note_id,
               line.guide ? "true" : "false");
        for (size_t j = 0, n = moenotes_score_line_member_count(score, line.id); j < n; j++) {
            moenotes_note_view_t note;
            moenotes_score_line_member_at(score, line.id, j, &note);
            printf("%s%d", j ? "," : "", note.id);
        }
        printf("]}");
    }
    printf("],\n  \"events\": [");
    for (size_t i = 0, n = moenotes_score_event_count(score); i < n; i++) {
        moenotes_event_t e;
        moenotes_score_event_at(score, i, &e);
        printf("%s{\"type\":%d,\"tick\":%d,\"end_tick\":%d,\"time_ms\":%d,\"end_time_ms\":%d,"
               "\"values\":[",
               i ? "," : "", e.type, e.tick, e.end_tick, e.position.time_ms,
               e.end_position.time_ms);
        for (size_t j = 0; j < e.value_count; j++) {
            int32_t v;
            moenotes_score_event_value_at(score, i, j, &v);
            printf("%s%d", j ? "," : "", v);
        }
        printf("],\"rhythms\":[");
        for (size_t j = 0, count = moenotes_score_call_rhythm_count(score, i); j < count; j++) {
            double value;
            moenotes_score_call_rhythm_at(score, i, j, &value);
            printf("%s%.9g", j ? "," : "", value);
        }
        printf("]}");
    }
    printf("],\n  \"signatures\": [");
    for (size_t i = 0, n = moenotes_score_signature_count(score); i < n; i++) {
        moenotes_signature_event_t e;
        moenotes_score_signature_at(score, i, &e);
        printf("%s{\"tick\":%d,\"numerator\":%d,\"denominator\":%d,\"bar\":%d}", i ? "," : "",
               e.tick, e.numerator, e.denominator, e.position.bar);
    }
    printf("],\n  \"bpms\": [");
    for (size_t i = 0, n = moenotes_score_bpm_count(score); i < n; i++) {
        moenotes_bpm_event_t e;
        moenotes_score_bpm_at(score, i, &e);
        printf("%s{\"tick\":%d,\"bpm\":%.9g,\"time_ms\":%d}", i ? "," : "", e.tick, e.bpm,
               e.position.time_ms);
    }
    printf("],\n  \"bar_line_times_ms\": [");
    for (size_t i = 0, count = moenotes_score_bar_line_count(score); i < count; i++) {
        moenotes_position_t p;
        if (moenotes_score_bar_line_at(score, i, &p) == MOENOTES_OK)
            printf("%s%d", i ? "," : "", p.time_ms);
        else
            printf("%snull", i ? "," : "");
    }
    printf("],\n  \"last_timing_note_ids\": [");
    for (size_t i = 0, count = moenotes_score_last_timing_note_count(score); i < count; i++) {
        moenotes_note_view_t n;
        moenotes_score_last_timing_note_at(score, i, &n);
        printf("%s%d", i ? "," : "", n.id);
    }
    printf("]\n}\n");
    fprintf(stderr, "parsed %zu notes, full combo count %u\n", moenotes_score_note_count(score),
            moenotes_score_full_combo_count(score, 1, 0));
    moenotes_score_free(score);
    return 0;
}
