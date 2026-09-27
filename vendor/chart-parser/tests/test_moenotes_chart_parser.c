#include "moenotes_chart_parser.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static const char *fixture = "{\"score\":{\"events\":{\"bpm\":[{\"t\":0,\"bpm\":120}],\"sig\":[{"
                             "\"t\":0,\"numerator\":4,\"denominator\":4}]},\"notes\":["
                             "{\"type\":\"tap\",\"t\":0,\"pos\":2,\"size\":1,\"crit\":true},"
                             "{\"type\":\"flick\",\"t\":480,\"pos\":4,\"size\":2,\"dir\":\"left\"},"
                             "{\"type\":\"long\",\"t\":960,\"pos\":8,\"size\":2,\"node\":[{\"t\":"
                             "1200,\"pos\":10,\"size\":2},{\"t\":1440,\"pos\":12,\"size\":1}]},"
                             "{\"type\":\"guide\",\"t\":1920,\"pos\":3,\"size\":1,\"node\":[{\"t\":"
                             "2160,\"pos\":5,\"size\":1}]}]}}";

static size_t alloc_count;
static void *count_malloc(void *ctx, size_t n) {
    (void)ctx;
    alloc_count++;
    return malloc(n);
}
static void *count_realloc(void *ctx, void *p, size_t n) {
    (void)ctx;
    if (!p)
        alloc_count++;
    return realloc(p, n);
}
static void count_free(void *ctx, void *p) {
    (void)ctx;
    free(p);
}

int main(void) {
    moenotes_parse_options_t options;
    moenotes_default_parse_options(&options);
    moenotes_score_t *score = NULL;
    char error[128];
    moenotes_result_t parse_result = moenotes_score_parse(fixture, strlen(fixture), &options, NULL,
                                                          &score, error, sizeof(error));
    if (parse_result != MOENOTES_OK)
        fprintf(stderr, "parse: %s (%s)\n", moenotes_result_string(parse_result), error);
    assert(parse_result == MOENOTES_OK);
    assert(moenotes_score_note_count(score) == 7);
    moenotes_note_view_t n;
    assert(moenotes_score_note_at(score, 0, &n) == MOENOTES_OK &&
           n.operate_type == MOENOTES_OP_NORMAL && n.position.time_ms == 0);
    assert(moenotes_score_note_at(score, 2, &n) == MOENOTES_OK &&
           n.operate_type == MOENOTES_OP_SLIDE_BEGIN);
    assert(moenotes_score_note_at(score, 6, &n) == MOENOTES_OK &&
           n.operate_type == MOENOTES_OP_GUIDE_END && n.position.time_ms == 2250);
    assert(moenotes_operate_type_is_judgement(MOENOTES_OP_GUIDE_BEGIN) == 0);
    assert(moenotes_operate_type_is_judgement(MOENOTES_OP_GUIDE_BEGIN_FLICK) == 1);
    assert(moenotes_score_full_combo_count(score, 1, 0) == 5);
    assert(moenotes_score_command_count(score) == 5);
    moenotes_score_free(score);

    moenotes_allocator_t allocator = {NULL, count_malloc, count_realloc, count_free};
    alloc_count = 0;
    moenotes_default_parse_options(&options);
    assert(moenotes_score_parse(fixture, strlen(fixture), &options, &allocator, &score, error,
                                sizeof(error)) == MOENOTES_OK);
    assert(alloc_count > 0);
    moenotes_score_free(score);

    assert(moenotes_score_parse("{", 1, &options, NULL, &score, error, sizeof(error)) ==
           MOENOTES_ERR_JSON);
    assert(moenotes_score_parse("[]", 2, &options, NULL, &score, error, sizeof(error)) ==
           MOENOTES_ERR_SCHEMA);

    moenotes_default_parse_options(&options);
    options.add_flick_hidden = 1;
    assert(moenotes_score_parse(fixture, strlen(fixture), &options, NULL, &score, error,
                                sizeof(error)) == MOENOTES_OK);
    assert(moenotes_score_note_count(score) == 7);
    assert(moenotes_score_line_member_count(score, 0) == 3);
    moenotes_score_free(score);

    /* gzip input is accepted by the library, matching MusicScoreLoader.Load. */
    uLongf compressed_size = compressBound(strlen(fixture));
    unsigned char *compressed = malloc(compressed_size);
    assert(compressed);
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    assert(deflateInit2(&zs, Z_BEST_SPEED, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY) ==
           Z_OK);
    zs.next_in = (Bytef *)fixture;
    zs.avail_in = (uInt)strlen(fixture);
    zs.next_out = compressed;
    zs.avail_out = (uInt)compressed_size;
    assert(deflate(&zs, Z_FINISH) == Z_STREAM_END);
    compressed_size = zs.total_out;
    deflateEnd(&zs);
    moenotes_default_parse_options(&options);
    moenotes_result_t gz_result = moenotes_score_parse(compressed, compressed_size, &options, NULL,
                                                       &score, error, sizeof(error));
    if (gz_result != MOENOTES_OK)
        fprintf(stderr, "gzip parse: %s (%s)\n", moenotes_result_string(gz_result), error);
    assert(gz_result == MOENOTES_OK);
    assert(moenotes_score_note_count(score) == 7);
    moenotes_score_free(score);
    free(compressed);

    /* Mirror preserves width and reflects the inclusive lane interval. */
    moenotes_default_parse_options(&options);
    options.mirror = 1;
    assert(moenotes_score_parse(fixture, strlen(fixture), &options, NULL, &score, error,
                                sizeof(error)) == MOENOTES_OK);
    assert(moenotes_score_note_at(score, 0, &n) == MOENOTES_OK && n.lane_start == 21 &&
           n.lane_end == 21);
    moenotes_score_free(score);
    puts("music-score C regression passed");
    return 0;
}
