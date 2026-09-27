#include <moenotes_chart_parser.h>
#include <inttypes.h>
#include <stdio.h>

int main(void) {
    const char json[] = "{\"score\":{\"events\":{},\"notes\":["
                        "{\"type\":\"tap\",\"t\":480,\"pos\":2,\"size\":4}]}}";
    moenotes_score_t *score = NULL;
    char error[128];
    moenotes_result_t result = moenotes_score_parse(
        json, sizeof(json) - 1, NULL, NULL, &score, error, sizeof(error));
    if (result != MOENOTES_OK) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }

    moenotes_note_view_t note;
    result = moenotes_score_note_at(score, 0, &note);
    if (result == MOENOTES_OK) {
        printf("tick=%" PRId32 " time=%" PRId32 "ms left=%.1f width=%.1f\n",
               note.tick, note.position.time_ms, note.lane_start_float, note.width);
    }
    moenotes_score_free(score);
    return result == MOENOTES_OK ? 0 : 1;
}
