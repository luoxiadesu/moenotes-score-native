#include "moenotes_chart_parser.h"
#include "yyjson.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define MAX_CHART_INPUT (64u * 1024u * 1024u)
#define MAX_RECORDS 1000000u
#define PPQ 480
#define LANES 24
#define SLOTS 20

typedef struct bpm {
    int32_t tick, time_ms;
    float value;
    moenotes_position_t position;
} bpm_t;
typedef struct sig {
    int32_t tick, bar, length, num, den;
    moenotes_position_t position;
} sig_t;
typedef struct note {
    moenotes_note_view_t v;
    moenotes_line_sample_t final_geometry;
    int32_t final_lane_start, final_lane_end;
    uint8_t has_final_geometry;
    int32_t overlap_lane, overlap_width;
    uint32_t slots;
    size_t alias;
    size_t creation_order;
    size_t membership_offset, membership_count;
} note_t;
typedef struct line {
    size_t first, count;
    int32_t slot;
    int guide;
    size_t member_offset, member_count;
} line_t;
typedef struct event {
    moenotes_event_t v;
    int32_t *values;
} event_t;
struct moenotes_score {
    moenotes_allocator_t alc;
    note_t *notes;
    size_t note_count, note_cap;
    size_t *order;
    size_t order_count;
    size_t *line_members;
    int32_t *note_lines;
    size_t *commands;
    size_t command_count;
    line_t *lines;
    size_t line_count, line_cap;
    bpm_t *bpms;
    size_t bpm_count, bpm_cap;
    sig_t *sigs;
    size_t sig_count, sig_cap;
    event_t *events;
    size_t event_count, event_cap;
    int32_t next_id;
    int32_t first_id;
    int mirror;
    uint32_t warnings;
    moenotes_result_t error;
};

static void *std_malloc(void *ctx, size_t n) {
    (void)ctx;
    return malloc(n);
}
static void *std_realloc(void *ctx, void *p, size_t n) {
    (void)ctx;
    return realloc(p, n);
}
static void std_free(void *ctx, void *p) {
    (void)ctx;
    free(p);
}
static const moenotes_allocator_t default_alc = {NULL, std_malloc, std_realloc, std_free};
static void *alloc(const moenotes_allocator_t *a, size_t n) { return a->malloc_fn(a->ctx, n); }
static void release(const moenotes_allocator_t *a, void *p) {
    if (p)
        a->free_fn(a->ctx, p);
}
static void *yy_alloc(void *ctx, size_t n) { return alloc(ctx, n); }
static void *yy_realloc(void *ctx, void *p, size_t old, size_t n) {
    const moenotes_allocator_t *a = ctx;
    (void)old;
    return a->realloc_fn(a->ctx, p, n);
}
static void yy_free(void *ctx, void *p) { release(ctx, p); }
static voidpf z_alloc(voidpf ctx, uInt n, uInt size) {
    if (size && n > SIZE_MAX / size)
        return NULL;
    return alloc(ctx, (size_t)n * size);
}
static void z_free(voidpf ctx, voidpf p) { release(ctx, p); }
static int fail(moenotes_score_t *s, moenotes_result_t code) {
    s->error = code;
    return 0;
}
static int reserve(moenotes_score_t *s, void **p, size_t *cap, size_t n, size_t unit) {
    if (n > MAX_RECORDS || n > SIZE_MAX / unit)
        return fail(s, MOENOTES_ERR_RANGE);
    if (n <= *cap)
        return 1;
    size_t next = *cap ? *cap * 2 : 16;
    if (next < n)
        next = n;
    if (next > MAX_RECORDS)
        next = MAX_RECORDS;
    void *q = s->alc.realloc_fn(s->alc.ctx, *p, next * unit);
    if (!q)
        return fail(s, MOENOTES_ERR_OUT_OF_MEMORY);
    *p = q;
    *cap = next;
    return 1;
}
#define RESERVE(s, field, n)                                                                       \
    reserve(s, (void **)&(s)->field, &(s)->field##_cap, n, sizeof(*(s)->field))
/* Singular capacity names keep the storage layout readable. */
#define notes_cap note_cap
#define lines_cap line_cap
#define bpms_cap bpm_cap
#define sigs_cap sig_cap
#define events_cap event_cap

const char *moenotes_version_string(void) { return MOENOTES_VERSION_STRING; }

void moenotes_default_parse_options(moenotes_parse_options_t *o) {
    if (o)
        memset(o, 0, sizeof(*o));
}
const char *moenotes_result_string(moenotes_result_t r) {
    switch (r) {
    case MOENOTES_OK:
        return "ok";
    case MOENOTES_ERR_INVALID_ARGUMENT:
        return "invalid argument";
    case MOENOTES_ERR_OUT_OF_MEMORY:
        return "out of memory";
    case MOENOTES_ERR_GZIP:
        return "invalid or truncated gzip";
    case MOENOTES_ERR_JSON:
        return "invalid JSON";
    case MOENOTES_ERR_SCHEMA:
        return "invalid chart schema";
    case MOENOTES_ERR_RANGE:
        return "chart value or resource limit exceeded";
    case MOENOTES_ERR_UNSUPPORTED:
        return "unsupported format or option";
    default:
        return "internal error";
    }
}
const char *moenotes_operate_type_name(moenotes_operate_type_t t) {
    switch (t) {
#define OP(x, text)                                                                                \
    case MOENOTES_OP_##x:                                                                          \
        return text
        OP(NORMAL, "normal");
        OP(SLIDE_BEGIN, "slide_begin");
        OP(SLIDE_CONNECTION, "slide_connection");
        OP(SLIDE_END, "slide_end");
        OP(FLICK, "flick");
        OP(SLIDE_BEGIN_FLICK, "slide_begin_flick");
        OP(SLIDE_END_FLICK, "slide_end_flick");
        OP(TRACE, "trace");
        OP(SLIDE_BEGIN_TRACE, "slide_begin_trace");
        OP(SLIDE_END_TRACE, "slide_end_trace");
        OP(SLIDE_CONNECTION_TRACE, "slide_connection_trace");
        OP(HIDDEN_SLIDE_BEGIN, "hidden_slide_begin");
        OP(HIDDEN_SLIDE_END, "hidden_slide_end");
        OP(GUIDE_BEGIN, "guide_begin");
        OP(GUIDE_BEGIN_NORMAL, "guide_begin_normal");
        OP(GUIDE_BEGIN_FLICK, "guide_begin_flick");
        OP(GUIDE_END, "guide_end");
        OP(GUIDE_BEGIN_TRACE, "guide_begin_trace");
        OP(GUIDE_END_TRACE, "guide_end_trace");
        OP(COMBO, "combo");
        OP(COMBO_SKIP, "combo_skip");
        OP(HIDDEN, "hidden");
        OP(INVALID_HIDDEN, "invalid_hidden");
#undef OP
    default:
        return "none";
    }
}
uint8_t moenotes_operate_type_is_judgement(moenotes_operate_type_t t) {
    switch (t) {
    case MOENOTES_OP_NORMAL:
    case MOENOTES_OP_SLIDE_BEGIN:
    case MOENOTES_OP_SLIDE_CONNECTION:
    case MOENOTES_OP_SLIDE_END:
    case MOENOTES_OP_FLICK:
    case MOENOTES_OP_SLIDE_BEGIN_FLICK:
    case MOENOTES_OP_SLIDE_END_FLICK:
    case MOENOTES_OP_TRACE:
    case MOENOTES_OP_SLIDE_BEGIN_TRACE:
    case MOENOTES_OP_SLIDE_END_TRACE:
    case MOENOTES_OP_SLIDE_CONNECTION_TRACE:
    case MOENOTES_OP_GUIDE_BEGIN_NORMAL:
    case MOENOTES_OP_GUIDE_BEGIN_FLICK:
    case MOENOTES_OP_GUIDE_BEGIN_TRACE:
    case MOENOTES_OP_GUIDE_END_TRACE:
    case MOENOTES_OP_COMBO:
        return 1;
    default:
        return 0;
    }
}
static yyjson_val *get(yyjson_val *v, const char *k) { return yyjson_obj_get(v, k); }
static const char *str(yyjson_val *v, const char *d) {
    const char *p = yyjson_get_str(v);
    return p ? p : d;
}
static int number(moenotes_score_t *s, yyjson_val *v, double def, double low, double high,
                  double *out) {
    if (v && !yyjson_is_num(v))
        return fail(s, MOENOTES_ERR_SCHEMA);
    double n = v ? yyjson_get_num(v) : def;
    if (!isfinite(n) || n < low || n > high)
        return fail(s, MOENOTES_ERR_RANGE);
    *out = n;
    return 1;
}
static int integer(moenotes_score_t *s, yyjson_val *v, int32_t def, int32_t low, int32_t high,
                   int32_t *out) {
    double n;
    if (!number(s, v, def, low, high, &n))
        return 0;
    if (trunc(n) != n)
        return fail(s, MOENOTES_ERR_SCHEMA);
    *out = (int32_t)n;
    return 1;
}
static int boolean(moenotes_score_t *s, yyjson_val *v, int def, uint8_t *out) {
    if (v && !yyjson_is_bool(v))
        return fail(s, MOENOTES_ERR_SCHEMA);
    *out = v ? yyjson_get_bool(v) : def;
    return 1;
}
static int32_t even_round(double x) {
    double lo = floor(x), frac = x - lo;
    return (int32_t)(lo + (frac > 0.5 || (frac == 0.5 && fmod(lo, 2) != 0)));
}
static float eased(float t, moenotes_ease_t e) {
    return e == MOENOTES_EASE_IN ? t * t : e == MOENOTES_EASE_OUT ? t * (2.0f - t) : t;
}
static int ease_value(moenotes_score_t *s, yyjson_val *v, moenotes_ease_t *out) {
    const char *p = str(v, "linear");
    if (v && !yyjson_is_str(v))
        return fail(s, MOENOTES_ERR_SCHEMA);
    if (!strcmp(p, "linear"))
        *out = MOENOTES_EASE_LINEAR;
    else if (!strcmp(p, "in"))
        *out = MOENOTES_EASE_IN;
    else if (!strcmp(p, "out"))
        *out = MOENOTES_EASE_OUT;
    else
        return fail(s, MOENOTES_ERR_SCHEMA);
    return 1;
}
static const sig_t *sig_at(const moenotes_score_t *s, int32_t tick) {
    const sig_t *p = s->sigs;
    for (size_t i = 1; i < s->sig_count && s->sigs[i].tick <= tick; i++)
        p = &s->sigs[i];
    return p;
}
static const bpm_t *bpm_at(const moenotes_score_t *s, int32_t tick) {
    const bpm_t *p = s->bpms;
    for (size_t i = 1; i < s->bpm_count && s->bpms[i].tick <= tick; i++)
        p = &s->bpms[i];
    return p;
}
static int position(const moenotes_score_t *s, int32_t tick, moenotes_position_t *out) {
    if (tick < 0)
        return 0;
    const sig_t *sg = sig_at(s, tick);
    const bpm_t *bp = bpm_at(s, tick);
    int32_t delta = tick - sg->tick;
    double time =
        floor(bp->time_ms + (double)(tick - bp->tick) * 60000.0 / (double)(bp->value * 480.0f));
    if (!isfinite(time) || time < 0 || time > INT32_MAX)
        return 0;
    out->bar = sg->bar + delta / sg->length;
    out->rhythm = delta % sg->length;
    out->rhythmic_unit = sg->length;
    out->bar_progress = (float)out->rhythm / (float)sg->length;
    out->time_ms = (int32_t)time;
    return 1;
}
static int build_timeline(moenotes_score_t *s, yyjson_val *ev) {
    yyjson_val *v, *a = get(ev, "bpm");
    size_t i, n;
    if (a && !yyjson_is_arr(a))
        return fail(s, MOENOTES_ERR_SCHEMA);
    yyjson_arr_foreach(a, i, n, v) {
        bpm_t b = {0};
        double value;
        if (!yyjson_is_obj(v))
            return fail(s, MOENOTES_ERR_SCHEMA);
        if (!integer(s, get(v, "t"), 0, 0, INT32_MAX, &b.tick) ||
            !number(s, get(v, "bpm"), 120, 0.001, 100000, &value))
            return 0;
        b.value = (float)value;
        if (!RESERVE(s, bpms, s->bpm_count + 1))
            return 0;
        s->bpms[s->bpm_count++] = b;
    }
    for (i = 1; i < s->bpm_count; i++) {
        bpm_t b = s->bpms[i];
        size_t j = i;
        while (j && s->bpms[j - 1].tick > b.tick) {
            s->bpms[j] = s->bpms[j - 1];
            j--;
        }
        s->bpms[j] = b;
    }
    if (!s->bpm_count || s->bpms[0].tick > 0) {
        if (!RESERVE(s, bpms, s->bpm_count + 1))
            return 0;
        if (s->bpm_count)
            memmove(s->bpms + 1, s->bpms, s->bpm_count * sizeof(*s->bpms));
        s->bpms[0] = (bpm_t){.value = 120};
        s->bpm_count++;
    }
    double cumulative_ms = 0;
    for (i = 1; i < s->bpm_count; i++) {
        bpm_t *b = &s->bpms[i], *p = b - 1;
        cumulative_ms += (double)(b->tick - p->tick) * 60000.0 /
                         (double)(p->value * 480.0f);
        if (!isfinite(cumulative_ms) || cumulative_ms > INT32_MAX)
            return fail(s, MOENOTES_ERR_RANGE);
        b->time_ms = even_round(cumulative_ms);
    }
    a = get(ev, "sig");
    if (a && !yyjson_is_arr(a))
        return fail(s, MOENOTES_ERR_SCHEMA);
    yyjson_arr_foreach(a, i, n, v) {
        sig_t g = {0};
        yyjson_val *compact = get(v, "sig");
        if (!yyjson_is_obj(v) ||
            (compact && (!yyjson_is_arr(compact) || yyjson_arr_size(compact) != 2)))
            return fail(s, MOENOTES_ERR_SCHEMA);
        if (!integer(s, get(v, "t"), 0, 0, INT32_MAX, &g.tick) ||
            !integer(s, compact ? yyjson_arr_get(compact, 0) : get(v, "numerator"), 4, 1, 1024,
                     &g.num) ||
            !integer(s, compact ? yyjson_arr_get(compact, 1) : get(v, "denominator"), 4, 1, 1920,
                     &g.den))
            return 0;
        g.length = 1920 * g.num / g.den;
        if (!RESERVE(s, sigs, s->sig_count + 1))
            return 0;
        s->sigs[s->sig_count++] = g;
    }
    for (i = 1; i < s->sig_count; i++) {
        sig_t g = s->sigs[i];
        size_t j = i;
        while (j && s->sigs[j - 1].tick > g.tick) {
            s->sigs[j] = s->sigs[j - 1];
            j--;
        }
        s->sigs[j] = g;
    }
    if (!s->sig_count || s->sigs[0].tick > 0) {
        if (!RESERVE(s, sigs, s->sig_count + 1))
            return 0;
        if (s->sig_count)
            memmove(s->sigs + 1, s->sigs, s->sig_count * sizeof(*s->sigs));
        s->sigs[0] = (sig_t){.length = 1920, .num = 4, .den = 4};
        s->sig_count++;
    }
    for (i = 1; i < s->sig_count; i++) {
        sig_t *g = &s->sigs[i], *p = g - 1;
        g->bar = p->bar + (g->tick - p->tick) / p->length;
    }
    for (i = 0; i < s->sig_count; i++)
        if (!position(s, s->sigs[i].tick, &s->sigs[i].position))
            return fail(s, MOENOTES_ERR_RANGE);
    for (i = 0; i < s->bpm_count; i++)
        if (!position(s, s->bpms[i].tick, &s->bpms[i].position))
            return fail(s, MOENOTES_ERR_RANGE);
    return 1;
}

static int before_position(const moenotes_position_t *p, int32_t bar, float progress) {
    return p->bar < bar || (p->bar == bar && (float)p->bar_progress < progress);
}
/* Creator time differs from the tick clock used by event anchors. Keep every
 * intermediate float32 operation and the strict event-position comparison. */
static int note_time(const moenotes_score_t *s, int32_t bar, float progress, int32_t *out) {
    float rhythm = 4.0f, bpm = 160.0f;
    moenotes_position_t anchor = {0};
    if (bar < 0 || !isfinite(progress) || progress < 0 || progress > 1)
        return 0;
    for (size_t i = 0; i < s->sig_count; i++) {
        const sig_t *g = &s->sigs[i];
        if (before_position(&g->position, bar, progress)) {
            rhythm = (float)g->num * 4.0f / (float)g->den;
            if (anchor.time_ms < g->position.time_ms)
                anchor = g->position;
        }
    }
    for (size_t i = 0; i < s->bpm_count; i++) {
        const bpm_t *b = &s->bpms[i];
        if (before_position(&b->position, bar, progress)) {
            bpm = b->value;
            if (anchor.time_ms < b->position.time_ms)
                anchor = b->position;
        }
    }
    float seconds_per_bar = rhythm * 60.0f / bpm;
    float seconds = seconds_per_bar * (float)(bar - anchor.bar) +
                    seconds_per_bar * (progress - (float)anchor.bar_progress);
    double ms = (double)anchor.time_ms + (double)floorf(seconds * 1000.0f);
    if (!isfinite(ms) || ms < 0 || ms > INT32_MAX)
        return 0;
    *out = (int32_t)ms;
    return 1;
}
static int note_position(const moenotes_score_t *s, int32_t tick, moenotes_position_t *out) {
    return position(s, tick, out) &&
           note_time(s, out->bar, (float)out->bar_progress, &out->time_ms);
}
static float bpm_at_time(const moenotes_score_t *s, int32_t time_ms) {
    float bpm = 160.0f;
    for (size_t i = 0; i < s->bpm_count && s->bpms[i].time_ms <= time_ms; i++)
        bpm = s->bpms[i].value;
    return bpm;
}

static int build_extra_events(moenotes_score_t *s, yyjson_val *ev) {
    const char *keys[] = {"skill", "fever", "call"};
    for (int kind = 0; kind < 3; kind++) {
        size_t begin = s->event_count;
        yyjson_val *a = get(ev, keys[kind]), *v;
        size_t i, n;
        if (a && !yyjson_is_arr(a))
            return fail(s, MOENOTES_ERR_SCHEMA);
        yyjson_arr_foreach(a, i, n, v) {
            if (!RESERVE(s, events, s->event_count + 1))
                return 0;
            event_t *e = &s->events[s->event_count++];
            memset(e, 0, sizeof(*e));
            e->v.type = (moenotes_event_type_t)kind;
            if (kind == 1 && (!yyjson_is_arr(v) || yyjson_arr_size(v) != 2))
                return fail(s, MOENOTES_ERR_SCHEMA);
            if (kind == 2 && !yyjson_is_obj(v))
                return fail(s, MOENOTES_ERR_SCHEMA);
            yyjson_val *t = kind == 0 ? v : kind == 1 ? yyjson_arr_get(v, 0) : get(v, "t");
            if (!integer(s, t, 0, 0, INT32_MAX, &e->v.tick))
                return 0;
            e->v.end_tick = e->v.tick;
            if (kind == 1 &&
                !integer(s, yyjson_arr_get(v, 1), 0, e->v.tick, INT32_MAX, &e->v.end_tick))
                return 0;
            if (!position(s, e->v.tick, &e->v.position) ||
                !position(s, e->v.end_tick, &e->v.end_position))
                return fail(s, MOENOTES_ERR_RANGE);
            if (kind == 2) {
                yyjson_val *timing = get(v, "timing");
                if (!yyjson_is_arr(timing))
                    return fail(s, MOENOTES_ERR_SCHEMA);
                size_t count = yyjson_arr_size(timing);
                if (count > MAX_RECORDS)
                    return fail(s, MOENOTES_ERR_RANGE);
                if (count) {
                    e->values = alloc(&s->alc, count * sizeof(*e->values));
                    if (!e->values)
                        return fail(s, MOENOTES_ERR_OUT_OF_MEMORY);
                }
                e->v.value_count = count;
                for (size_t k = 0; k < count; k++)
                    if (!integer(s, yyjson_arr_get(timing, k), 0, INT32_MIN, INT32_MAX,
                                 &e->values[k]))
                        return 0;
            }
        }
        /* Native preserves skill order, but sorts Fever and Call by start tick. */
        if (kind != 0)
            for (size_t j = begin + 1; j < s->event_count; j++) {
                event_t entry = s->events[j];
                size_t k = j;
                while (k > begin && s->events[k - 1].v.tick > entry.v.tick) {
                    s->events[k] = s->events[k - 1];
                    k--;
                }
                s->events[k] = entry;
            }
    }
    return 1;
}

static int note_type(moenotes_score_t *s, yyjson_val *raw, int *out) {
    yyjson_val *v = get(raw, "type");
    const char *p = str(v, "tap");
    if (v && !yyjson_is_str(v))
        return fail(s, MOENOTES_ERR_SCHEMA);
    const char *names[] = {"tap", "flick", "trace", "long", "guide", "node"};
    for (int i = 0; i < 6; i++)
        if (!strcmp(p, names[i])) {
            *out = i;
            return 1;
        }
    return fail(s, MOENOTES_ERR_SCHEMA);
}
static void geometry(moenotes_note_view_t *v, float pos, float width) {
    v->lane_start_float = pos;
    v->width = width;
    v->lane_end_float = (float)(pos + width) - 1.0f;
    v->lane_start = even_round(pos);
    int32_t w = even_round(width);
    if (w < 1)
        w = 1;
    v->lane_end = v->lane_start + w - 1;
}
static int read_note(moenotes_score_t *s, yyjson_val *raw, int source, note_t *out) {
    memset(out, 0, sizeof(*out));
    out->alias = SIZE_MAX;
    moenotes_note_view_t *v = &out->v;
    if (!yyjson_is_obj(raw))
        return fail(s, MOENOTES_ERR_SCHEMA);
    v->pair_note_id = v->parent_note_id = v->hidden_for_note_id = v->line_id = v->line_index = -1;
    v->source_index = source;
    v->lane_count = LANES;
    if (!integer(s, get(raw, "t"), 0, 0, INT32_MAX, &v->tick) ||
        !boolean(s, get(raw, "crit"), 0, &v->critical) ||
        !boolean(s, get(raw, "visible"), 1, &v->visible))
        return 0;
    if (!note_position(s, v->tick, &v->position))
        return fail(s, MOENOTES_ERR_RANGE);
    yyjson_val *p = get(raw, "pos");
    double pos = 0, width;
    if (yyjson_is_str(p) && !strcmp(yyjson_get_str(p), "auto"))
        v->pos_auto = 1;
    else if (!number(s, p, 0, -100000, 100000, &pos))
        return 0;
    if (!number(s, get(raw, "size"), 6, 0, 100000, &width))
        return 0;
    geometry(v, (float)pos, (float)width);
    out->overlap_lane = even_round((float)pos);
    out->overlap_width = even_round((float)width);
    yyjson_val *ease = get(raw, "ease");
    if (yyjson_is_arr(ease)) {
        if (yyjson_arr_size(ease) != 2)
            return fail(s, MOENOTES_ERR_SCHEMA);
        if (!ease_value(s, yyjson_arr_get(ease, 0), &v->ease_left) ||
            !ease_value(s, yyjson_arr_get(ease, 1), &v->ease_right))
            return 0;
    } else {
        if (!ease_value(s, ease, &v->ease_left))
            return 0;
        v->ease_right = v->ease_left;
    }
    const char *dir = str(get(raw, "dir"), "up");
    if (get(raw, "dir") && !yyjson_is_str(get(raw, "dir")))
        return fail(s, MOENOTES_ERR_SCHEMA);
    if (strcmp(dir, "up") && strcmp(dir, "left") && strcmp(dir, "right"))
        return fail(s, MOENOTES_ERR_SCHEMA);
    v->direction = !strcmp(dir, "left")    ? MOENOTES_DIRECTION_LEFT
                   : !strcmp(dir, "right") ? MOENOTES_DIRECTION_RIGHT
                                           : MOENOTES_DIRECTION_NORMAL;
    const char *alpha = str(get(raw, "alpha"), "none");
    if (get(raw, "alpha") && !yyjson_is_str(get(raw, "alpha")))
        return fail(s, MOENOTES_ERR_SCHEMA);
    if (!strcmp(alpha, "fadeIn"))
        v->alpha = MOENOTES_ALPHA_FADE_IN;
    else if (!strcmp(alpha, "fadeOut"))
        v->alpha = MOENOTES_ALPHA_FADE_OUT;
    else if (strcmp(alpha, "none"))
        return fail(s, MOENOTES_ERR_SCHEMA);
    /* Optional infrastructure extension; native SS JSON singles have no slots. */
    yyjson_val *slots = get(raw, "line_indices"), *entry;
    size_t i, n;
    if (slots && !yyjson_is_arr(slots))
        return fail(s, MOENOTES_ERR_SCHEMA);
    yyjson_arr_foreach(slots, i, n, entry) {
        int32_t slot;
        if (!integer(s, entry, 0, 0, SLOTS - 1, &slot))
            return 0;
        out->slots |= 1u << slot;
    }
    return 1;
}
static int append_note(moenotes_score_t *s, note_t n) {
    if (s->next_id == INT32_MAX)
        return fail(s, MOENOTES_ERR_RANGE);
    if (!RESERVE(s, notes, s->note_count + 1))
        return 0;
    n.v.id = s->next_id++;
    s->notes[s->note_count++] = n;
    return 1;
}
static size_t canonical(const moenotes_score_t *s, size_t i) {
    while (s->notes[i].alias != SIZE_MAX)
        i = s->notes[i].alias;
    return i;
}
static moenotes_operate_type_t line_op(int guide, int type, size_t k, size_t count,
                                       const moenotes_note_view_t *v) {
    if (guide) {
        if (!k)
            return MOENOTES_OP_GUIDE_BEGIN;
        if (k + 1 == count)
            return v->visible && type == 2 ? MOENOTES_OP_GUIDE_END_TRACE : MOENOTES_OP_GUIDE_END;
        return v->pos_auto || v->visible ? MOENOTES_OP_SLIDE_CONNECTION_TRACE : MOENOTES_OP_HIDDEN;
    }
    if (!k)
        return !v->visible ? MOENOTES_OP_HIDDEN_SLIDE_BEGIN
               : type == 1 ? MOENOTES_OP_SLIDE_BEGIN_FLICK
               : type == 2 ? MOENOTES_OP_SLIDE_BEGIN_TRACE
                           : MOENOTES_OP_SLIDE_BEGIN;
    if (k + 1 == count)
        return !v->visible ? MOENOTES_OP_HIDDEN_SLIDE_END
               : type == 1 ? MOENOTES_OP_SLIDE_END_FLICK
               : type == 2 ? MOENOTES_OP_SLIDE_END_TRACE
                           : MOENOTES_OP_SLIDE_END;
    if (v->pos_auto)
        return MOENOTES_OP_SLIDE_CONNECTION;
    return !v->visible ? MOENOTES_OP_HIDDEN
           : type == 2 ? MOENOTES_OP_SLIDE_CONNECTION_TRACE
                       : MOENOTES_OP_SLIDE_CONNECTION;
}
static void interpolate(const moenotes_note_view_t *a, const moenotes_note_view_t *b, int32_t tick,
                        moenotes_line_sample_t *o) {
    float t = b->tick > a->tick ? (float)(tick - a->tick) / (float)(b->tick - a->tick) : 0;
    float l = (float)a->lane_start_float +
              eased(t, a->ease_left) * ((float)b->lane_start_float - (float)a->lane_start_float);
    float ar = (float)a->lane_start_float + (float)a->width,
          br = (float)b->lane_start_float + (float)b->width;
    float r = ar + eased(t, a->ease_right) * (br - ar);
    o->lane_start = l;
    o->lane_end = r - 1.0f;
    o->width = r - l;
}
typedef struct raw_ref {
    yyjson_val *raw;
    size_t source;
    int32_t tick;
} raw_ref_t;
static int raw_cmp(const void *a, const void *b) {
    const raw_ref_t *x = a, *y = b;
    if (x->tick != y->tick)
        return x->tick < y->tick ? -1 : 1;
    return x->source < y->source ? -1 : x->source > y->source;
}
static int build_notes(moenotes_score_t *s, yyjson_val *array) {
    size_t count = yyjson_arr_size(array);
    if (count > MAX_RECORDS)
        return fail(s, MOENOTES_ERR_RANGE);
    if (!count)
        return 1;
    raw_ref_t *refs = alloc(&s->alc, count * sizeof(*refs));
    if (!refs)
        return fail(s, MOENOTES_ERR_OUT_OF_MEMORY);
    int ok = 0;
    for (size_t i = 0; i < count; i++) {
        yyjson_val *raw = yyjson_arr_get(array, i), *t = get(raw, "t");
        if (!yyjson_is_obj(raw)) {
            fail(s, MOENOTES_ERR_SCHEMA);
            goto done;
        }
        if (!t)
            t = get(yyjson_arr_get(get(raw, "node"), 0), "t");
        refs[i] = (raw_ref_t){raw, i, 0};
        if (!integer(s, t, 0, 0, INT32_MAX, &refs[i].tick))
            goto done;
    }
    qsort(refs, count, sizeof(*refs), raw_cmp);
    for (size_t i = 0; i < count; i++) {
        yyjson_val *raw = refs[i].raw;
        int type;
        if (!note_type(s, raw, &type))
            goto done;
        if (type != 3 && type != 4) {
            note_t n;
            if (!read_note(s, raw, (int)refs[i].source, &n))
                goto done;
            n.v.operate_type = type == 1   ? MOENOTES_OP_FLICK
                               : type == 2 ? MOENOTES_OP_TRACE
                                           : MOENOTES_OP_NORMAL;
            if (!append_note(s, n))
                goto done;
            continue;
        }
        yyjson_val *nodes = get(raw, "node");
        if (!yyjson_is_arr(nodes)) {
            fail(s, MOENOTES_ERR_SCHEMA);
            goto done;
        }
        int outer = get(raw, "t") != NULL;
        size_t nn = yyjson_arr_size(nodes) + (size_t)outer;
        if (nn < 2) {
            fail(s, MOENOTES_ERR_SCHEMA);
            goto done;
        }
        if (!RESERVE(s, lines, s->line_count + 1))
            goto done;
        line_t line = {.first = s->note_count, .count = nn, .slot = -1, .guide = type == 4};
        int32_t parent = s->next_id, lid = (int32_t)s->line_count;
        for (size_t k = 0; k < nn; k++) {
            yyjson_val *r = outer && !k ? raw : yyjson_arr_get(nodes, k - (size_t)outer);
            note_t n;
            int nt;
            if (!note_type(s, r, &nt) || !read_note(s, r, (int)refs[i].source, &n))
                goto done;
            if (k && n.v.tick < s->notes[s->note_count - 1].v.tick)
                s->warnings |= MOENOTES_WARNING_NONMONOTONIC_LINE;
            n.v.operate_type = line_op(line.guide, nt, k, nn, &n.v);
            n.v.line_id = lid;
            n.v.parent_note_id = parent;
            n.v.slide_along = n.v.pos_auto;
            if (!append_note(s, n))
                goto done;
        }
        for (size_t k = 0; k < nn; k++) {
            moenotes_note_view_t *v = &s->notes[line.first + k].v;
            if (!v->pos_auto)
                continue;
            size_t l = k, r = k;
            while (l > 0 && s->notes[line.first + l].v.pos_auto)
                l--;
            while (r + 1 < nn && s->notes[line.first + r].v.pos_auto)
                r++;
            if (!k || k + 1 == nn) {
                const moenotes_note_view_t *first = &s->notes[line.first].v;
                geometry(v, (float)first->lane_start_float, (float)first->width);
            } else {
                moenotes_line_sample_t sample;
                interpolate(&s->notes[line.first + l].v, &s->notes[line.first + r].v, v->tick,
                            &sample);
                if (sample.width < 0 || fabs(sample.lane_start) > 200000 || sample.width > 200000) {
                    fail(s, MOENOTES_ERR_RANGE);
                    goto done;
                }
                geometry(v, (float)sample.lane_start, (float)sample.width);
            }
        }
        int32_t start = s->notes[line.first].v.tick;
        for (int slot = 0; slot < SLOTS; slot++) {
            int occupied = 0;
            for (size_t j = 0; j < s->line_count; j++) {
                const line_t *p = &s->lines[j];
                if (p->guide == line.guide && p->slot == slot &&
                    s->notes[p->first + p->count - 1].v.tick >= start) {
                    occupied = 1;
                    break;
                }
            }
            if (!occupied) {
                line.slot = slot;
                break;
            }
        }
        if (line.slot < 0) {
            fail(s, MOENOTES_ERR_RANGE);
            goto done;
        }
        for (size_t k = 0; k < nn; k++)
            s->notes[line.first + k].v.line_index = line.slot;
        s->lines[s->line_count++] = line;
    }
    if (s->mirror)
        for (size_t i = 0; i < s->note_count; i++) {
            moenotes_note_view_t *v = &s->notes[i].v;
            geometry(v, 24.0f - (float)v->lane_start_float - (float)v->width, (float)v->width);
            if (v->direction)
                v->direction = v->direction == MOENOTES_DIRECTION_LEFT ? MOENOTES_DIRECTION_RIGHT
                                                                       : MOENOTES_DIRECTION_LEFT;
            /* Native judgement geometry retains source easing under mirror.
             * The renderer's independent edge swap belongs in sample_line. */
        }
    ok = 1;
done:
    release(&s->alc, refs);
    return ok;
}

static int endpoint(int op) {
    return op == 20 || op == 22 || op == 41 || op == 42 || op == 61 || op == 62 || op == 80 ||
           op == 82 || (op >= 100 && op <= 105);
}
static int same_overlap_key(const note_t *a, const note_t *b) {
    return a->v.tick == b->v.tick && a->overlap_lane == b->overlap_lane &&
           a->overlap_width == b->overlap_width;
}
static int same_endpoint_bucket(const moenotes_note_view_t *a, const moenotes_note_view_t *b) {
    float x = (float)a->position.bar + (float)a->position.bar_progress;
    float y = (float)b->position.bar + (float)b->position.bar_progress;
    return x == y && a->lane_start == b->lane_start &&
           a->lane_end - a->lane_start == b->lane_end - b->lane_start;
}
static void merge_graph(moenotes_score_t *s) {
    /* Guide starts consume the first matching standalone tap/flick/trace. */
    for (size_t l = 0; l < s->line_count; l++) {
        line_t *line = &s->lines[l];
        moenotes_note_view_t *v = &s->notes[line->first].v;
        if (!line->guide || v->pos_auto)
            continue;
        for (size_t i = 0; i < s->note_count; i++) {
            moenotes_note_view_t *p = &s->notes[i].v;
            if (p->line_id != -1 || !same_overlap_key(&s->notes[line->first], &s->notes[i]))
                continue;
            if (p->operate_type != 1 && p->operate_type != 40 && p->operate_type != 60)
                continue;
            v->operate_type = p->operate_type == 1    ? MOENOTES_OP_GUIDE_BEGIN_NORMAL
                              : p->operate_type == 40 ? MOENOTES_OP_GUIDE_BEGIN_FLICK
                                                      : MOENOTES_OP_GUIDE_BEGIN_TRACE;
            v->direction = p->direction;
            /* One overlap-key lookup is shared by every matching guide start. */
            if (s->notes[i].alias == SIZE_MAX)
                s->notes[i].alias = line->first;
            break;
        }
    }
    for (size_t i = 0; i < s->note_count; i++) {
        moenotes_note_view_t *v = &s->notes[i].v;
        if (!endpoint(v->operate_type) || s->notes[i].alias != SIZE_MAX)
            continue;
        for (size_t j = 0; j < i; j++) {
            moenotes_note_view_t *p = &s->notes[j].v;
            if (s->notes[j].alias != SIZE_MAX || p->line_id < 0 || !same_endpoint_bucket(v, p) ||
                p->operate_type != v->operate_type || p->direction != v->direction ||
                p->critical != v->critical || p->ease_left != v->ease_left ||
                p->ease_right != v->ease_right)
                continue;
            s->notes[i].alias = j;
            s->warnings |= MOENOTES_WARNING_SHARED_ENDPOINT;
            break;
        }
    }
    for (size_t i = 0; i < s->note_count; i++) {
        moenotes_note_view_t *v = &s->notes[i].v;
        if (v->line_id >= 0)
            v->parent_note_id = s->notes[canonical(s, s->lines[v->line_id].first)].v.id;
    }
}

/* No pointer into notes may survive append_note: it can reallocate the array. */
static int add_hidden(moenotes_score_t *s) {
    size_t base = s->note_count;
    for (size_t i = 0; i < base; i++) {
        note_t flick = s->notes[i];
        if (flick.v.operate_type != MOENOTES_OP_FLICK || !flick.slots)
            continue;
        for (size_t j = 0; j < s->line_count; j++) {
            line_t line = s->lines[j];
            moenotes_note_view_t begin = s->notes[canonical(s, line.first)].v,
                                 end = s->notes[line.first + line.count - 1].v;
            if (line.guide || !(flick.slots & (1u << line.slot)) || flick.v.tick < begin.tick ||
                flick.v.tick >= end.tick)
                continue;
            note_t h = flick;
            h.v.operate_type = MOENOTES_OP_HIDDEN;
            h.v.visible = 0;
            h.v.generated = 1;
            h.v.parent_note_id = begin.id;
            h.v.line_id = (int32_t)j;
            h.v.line_index = line.slot;
            h.v.hidden_for_note_id = flick.v.id;
            h.v.pair_note_id = -1;
            h.v.direction = MOENOTES_DIRECTION_NORMAL;
            h.v.critical = 0;
            if (!append_note(s, h))
                return 0;
        }
    }
    return 1;
}
static int boundary(const moenotes_note_view_t *v) {
    return moenotes_operate_type_is_judgement(v->operate_type);
}
static int same_position(const moenotes_position_t *a, const moenotes_position_t *b) {
    float x = (float)a->bar_progress, y = (float)b->bar_progress;
    float tolerance = fmaxf(1e-6f * fmaxf(fabsf(x), fabsf(y)), 8.0f * 1.40129846e-45f);
    return a->bar == b->bar && fabsf(x - y) < tolerance;
}
static const sig_t *sig_at_bar(const moenotes_score_t *s, int32_t bar) {
    const sig_t *g = s->sigs;
    for (size_t i = 1; i < s->sig_count && s->sigs[i].bar <= bar; i++)
        g = &s->sigs[i];
    return g;
}
static float bar_rhythm(const sig_t *g) { return (float)g->num * 4.0f / (float)g->den; }
static int beat_position(moenotes_score_t *s, int32_t bar, double progress, int32_t *tick,
                         moenotes_position_t *out) {
    const sig_t *g = sig_at_bar(s, bar);
    float p = (float)progress;
    double t = g->tick + (double)(bar - g->bar) * g->length + (double)p * g->length;
    if (t < 0 || t > INT32_MAX)
        return fail(s, MOENOTES_ERR_RANGE);
    *tick = even_round(t);
    if (!note_time(s, bar, p, &out->time_ms))
        return fail(s, MOENOTES_ERR_RANGE);
    out->bar = bar;
    out->bar_progress = p;
    out->rhythmic_unit = (int32_t)(bar_rhythm(g) * 2.0f);
    if (out->rhythmic_unit < 1)
        return fail(s, MOENOTES_ERR_UNSUPPORTED);
    out->rhythm = even_round(p * (float)out->rhythmic_unit);
    return 1;
}
typedef struct beat_candidate {
    int32_t tick;
    moenotes_position_t position;
} beat_candidate_t;

static float judgement_ratio(const moenotes_position_t *p, const moenotes_note_view_t *a,
                             const moenotes_note_view_t *b) {
    int64_t dt = (int64_t)b->position.time_ms - a->position.time_ms;
    if (dt)
        return (float)((int64_t)p->time_ms - a->position.time_ms) / (float)dt;
    float start = (float)a->position.bar + (float)a->position.bar_progress;
    float end = (float)b->position.bar + (float)b->position.bar_progress;
    float target = (float)p->bar + (float)p->bar_progress;
    return end > start ? (target - start) / (end - start) : 1.0f;
}
static void judgement_sample(const moenotes_position_t *p, const moenotes_note_view_t *a,
                             const moenotes_note_view_t *b, moenotes_line_sample_t *out) {
    float ratio = eased(judgement_ratio(p, a, b), a->ease_left);
    ratio = fminf(1.0f, fmaxf(0.0f, ratio));
    float lane = (float)a->lane_start_float +
                 ratio * ((float)b->lane_start_float - (float)a->lane_start_float);
    float last = (float)a->lane_end_float +
                 ratio * ((float)b->lane_end_float - (float)a->lane_end_float);
    out->lane_start = lane;
    out->lane_end = last;
    out->width = last - lane + 1.0f;
}
static int add_combos(moenotes_score_t *s) {
    beat_candidate_t *beats = NULL;
    size_t capacity = 0;
    int ok = 0;
    for (size_t li = 0; li < s->line_count; li++) {
        line_t line = s->lines[li];
        if (line.guide)
            continue;
        moenotes_note_view_t begin = s->notes[canonical(s, line.first)].v,
                             end = s->notes[line.first + line.count - 1].v;
        size_t left = 0, count = 0;
        for (size_t right = 1; right < line.count; right++) {
            moenotes_note_view_t b = s->notes[line.first + right].v;
            if (right + 1 < line.count && !boundary(&b))
                continue;
            moenotes_note_view_t a = s->notes[line.first + left].v;
            int32_t bar = a.position.bar;
            double progress = (float)a.position.bar_progress;
            double step = 1.0 / (2.0 * (double)bar_rhythm(sig_at_bar(s, bar)));
            for (size_t iteration = 0;; iteration++) {
                if (iteration >= MAX_RECORDS) {
                    fail(s, MOENOTES_ERR_RANGE);
                    goto done;
                }
                progress += step;
                if (progress >= 1) {
                    if (bar == INT32_MAX) {
                        fail(s, MOENOTES_ERR_RANGE);
                        goto done;
                    }
                    bar++;
                    progress -= 1;
                    step = 1.0 / (2.0 * (double)bar_rhythm(sig_at_bar(s, bar)));
                }
                beat_candidate_t beat;
                if (!beat_position(s, bar, progress, &beat.tick, &beat.position))
                    goto done;
                if (beat.position.time_ms >= b.position.time_ms)
                    break;
                if (!reserve(s, (void **)&beats, &capacity, count + 1, sizeof(*beats)))
                    goto done;
                beats[count++] = beat;
            }
            left = right;
        }
        /* Keep suppressed candidates in this list: first/last refers to the
         * original complete iterator result, before duplicate filtering. */
        for (size_t bi = 0; bi < count; bi++) {
            note_t n = {0};
            n.alias = SIZE_MAX;
            n.v.tick = beats[bi].tick;
            n.v.position = beats[bi].position;
            int duplicate = 0;
            for (size_t k = 1; k < line.count; k++) {
                const moenotes_note_view_t *v = &s->notes[line.first + k].v;
                if (boundary(v) && same_position(&n.v.position, &v->position)) {
                    duplicate = 1;
                    break;
                }
            }
            if (duplicate)
                continue;
            double threshold = 15000.0 / bpm_at_time(s, n.v.position.time_ms);
            int skip = 0;
            for (size_t k = 1; k + 1 < line.count; k++) {
                const moenotes_note_view_t *v = &s->notes[line.first + k].v;
                int64_t dt = (int64_t)v->position.time_ms - n.v.position.time_ms;
                if (boundary(v) && dt > 0 && dt <= threshold) {
                    skip = 1;
                    break;
                }
            }
            if ((!bi && (int64_t)n.v.position.time_ms - begin.position.time_ms < threshold) ||
                (bi + 1 == count && (int64_t)end.position.time_ms - n.v.position.time_ms < threshold))
                skip = 1;
            n.v.operate_type = skip ? MOENOTES_OP_COMBO_SKIP : MOENOTES_OP_COMBO;
            n.v.parent_note_id = begin.id;
            n.v.line_id = (int32_t)li;
            n.v.line_index = line.slot;
            n.v.source_index = s->notes[line.first].v.source_index;
            n.v.lane_count = LANES;
            n.v.pair_note_id = n.v.hidden_for_note_id = -1;
            n.v.generated = 1;
            size_t gl = 0, gr = line.count - 1;
            for (size_t k = 1; k < line.count; k++) {
                const moenotes_note_view_t *v = &s->notes[line.first + k].v;
                if (v->pos_auto && k + 1 < line.count)
                    continue;
                if (v->tick >= n.v.tick) {
                    gr = k;
                    break;
                }
                gl = k;
            }
            moenotes_note_view_t ga = s->notes[line.first + gl].v,
                                 gb = s->notes[line.first + gr].v;
            moenotes_line_sample_t sample;
            judgement_sample(&n.v.position, &ga, &gb, &sample);
            geometry(&n.v, (float)sample.lane_start, (float)sample.width);
            n.v.lane_end_float = (float)sample.lane_end;
            n.v.lane_start = (int32_t)floor(sample.lane_start);
            n.v.lane_end = (int32_t)ceil(sample.lane_end);
            if (!append_note(s, n))
                goto done;
        }
    }
    ok = 1;
done:
    release(&s->alc, beats);
    return ok;
}
typedef struct order_entry {
    size_t index;
    int32_t tick, id;
    float position_key;
    int32_t lane, source, origin_tick;
    int group, priority;
    size_t sequence, bucket;
} order_entry_t;
static int order_cmp(const void *a, const void *b) {
    const order_entry_t *x = a, *y = b;
    if (x->tick != y->tick)
        return x->tick < y->tick ? -1 : 1;
    return x->id < y->id ? -1 : x->id > y->id;
}
static int creation_priority(int op) {
    switch (op) {
    case 20: case 41: case 61: case 80:
    case 100: case 101: case 102: case 104:
        return 8;
    case 122:
        return 9;
    default:
        return 10;
    }
}
static int sequence_cmp(const void *a, const void *b) {
    const order_entry_t *x = a, *y = b;
    if (x->group != y->group)
        return x->group < y->group ? -1 : 1;
    if (x->group && x->origin_tick != y->origin_tick)
        return x->origin_tick < y->origin_tick ? -1 : 1;
    if (x->source != y->source)
        return x->source < y->source ? -1 : 1;
    return x->index < y->index ? -1 : x->index > y->index;
}
static int bucket_cmp(const void *a, const void *b) {
    const order_entry_t *x = a, *y = b;
    if (x->position_key != y->position_key)
        return x->position_key < y->position_key ? -1 : 1;
    if (x->lane != y->lane)
        return x->lane < y->lane ? -1 : 1;
    return x->sequence < y->sequence ? -1 : x->sequence > y->sequence;
}
static int creation_cmp(const void *a, const void *b) {
    const order_entry_t *x = a, *y = b;
    if (x->position_key != y->position_key)
        return x->position_key < y->position_key ? -1 : 1;
    if (x->priority != y->priority)
        return x->priority < y->priority ? -1 : 1;
    if (x->bucket != y->bucket)
        return x->bucket < y->bucket ? -1 : 1;
    return x->sequence < y->sequence ? -1 : x->sequence > y->sequence;
}
static int build_order(moenotes_score_t *s) {
    if (!s->note_count)
        return 1;
    order_entry_t *entries = alloc(&s->alc, s->note_count * sizeof(*entries));
    s->order = alloc(&s->alc, s->note_count * sizeof(*s->order));
    if (!entries || !s->order) {
        release(&s->alc, entries);
        return fail(s, MOENOTES_ERR_OUT_OF_MEMORY);
    }
    for (size_t i = 0; i < s->note_count; i++)
        if (s->notes[i].alias == SIZE_MAX)
            entries[s->order_count++] = (order_entry_t){.index = i,
                .tick = s->notes[i].v.tick, .id = s->notes[i].v.id};
    qsort(entries, s->order_count, sizeof(*entries), order_cmp);
    size_t source_count = 0;
    for (size_t i = 0; i < s->order_count; i++) {
        size_t ix = entries[i].index;
        s->order[i] = ix;
        const moenotes_note_view_t *v = &s->notes[ix].v;
        if (v->generated)
            continue;
        order_entry_t *e = &entries[source_count++];
        *e = (order_entry_t){.index = ix};
        e->position_key = (float)v->position.bar + (float)v->position.bar_progress;
        e->lane = v->lane_start;
        e->source = v->source_index;
        e->priority = creation_priority(v->operate_type);
        if (v->line_id >= 0) {
            const line_t *l = &s->lines[v->line_id];
            e->group = l->guide ? 2 : 1;
            e->origin_tick = s->notes[l->first].v.tick;
        }
    }
    qsort(entries, source_count, sizeof(*entries), sequence_cmp);
    for (size_t i = 0; i < source_count; i++)
        entries[i].sequence = i;
    qsort(entries, source_count, sizeof(*entries), bucket_cmp);
    size_t bucket = 0;
    for (size_t i = 0; i < source_count; i++) {
        if (!i || entries[i].position_key != entries[i - 1].position_key ||
            entries[i].lane != entries[i - 1].lane)
            bucket = entries[i].sequence;
        entries[i].bucket = bucket;
    }
    qsort(entries, source_count, sizeof(*entries), creation_cmp);
    size_t prev = SIZE_MAX;
    for (size_t i = 0; i < source_count; i++) {
        size_t ix = entries[i].index;
        s->notes[ix].creation_order = i;
        moenotes_note_view_t *v = &s->notes[ix].v;
        int op = v->operate_type;
        if (v->generated || (op != 1 && op != 20 && op != 22 && op != 40 && op != 41 && op != 42))
            continue;
        if (prev != SIZE_MAX && same_position(&s->notes[prev].v.position, &v->position)) {
            v->pair_note_id = s->notes[prev].v.id;
            s->notes[prev].v.pair_note_id = v->id;
        }
        prev = ix;
    }
    for (size_t i = 0; i < s->note_count; i++)
        if (s->notes[i].v.generated)
            s->notes[i].creation_order = source_count + i;
    release(&s->alc, entries);
    return 1;
}

typedef struct membership {
    size_t note;
    int32_t line, tick, id;
    float position_key;
    size_t creation_order;
} membership_t;
static int membership_cmp(const void *a, const void *b) {
    const membership_t *x = a, *y = b;
    if (x->line != y->line)
        return x->line < y->line ? -1 : 1;
    if (x->position_key != y->position_key)
        return x->position_key < y->position_key ? -1 : 1;
    return x->creation_order < y->creation_order ? -1 : x->creation_order > y->creation_order;
}
/* Build both directions once. Aliases contribute all source memberships but
 * a canonical note appears only once within any particular source line. */
static int build_indices(moenotes_score_t *s) {
    if (!s->note_count)
        return 1;
    membership_t *members = alloc(&s->alc, s->note_count * sizeof(*members));
    s->commands = alloc(&s->alc, s->order_count * sizeof(*s->commands));
    if (!members || !s->commands) {
        release(&s->alc, members);
        return fail(s, MOENOTES_ERR_OUT_OF_MEMORY);
    }
    size_t count = 0;
    for (size_t i = 0; i < s->note_count; i++) {
        int32_t line = s->notes[i].v.line_id;
        if (line < 0)
            continue;
        size_t c = canonical(s, i);
        const moenotes_note_view_t *v = &s->notes[c].v;
        members[count++] = (membership_t){c, line, v->tick, v->id,
            (float)v->position.bar + (float)v->position.bar_progress, s->notes[c].creation_order};
    }
    qsort(members, count, sizeof(*members), membership_cmp);
    size_t unique = 0;
    for (size_t i = 0; i < count; i++) {
        if (unique && members[i].line == members[unique - 1].line &&
            members[i].note == members[unique - 1].note)
            continue;
        members[unique++] = members[i];
    }
    if (unique) {
        s->line_members = alloc(&s->alc, unique * sizeof(*s->line_members));
        s->note_lines = alloc(&s->alc, unique * sizeof(*s->note_lines));
        if (!s->line_members || !s->note_lines) {
            release(&s->alc, members);
            return fail(s, MOENOTES_ERR_OUT_OF_MEMORY);
        }
    }
    for (size_t i = 0; i < unique; i++) {
        line_t *l = &s->lines[members[i].line];
        if (!l->member_count)
            l->member_offset = i;
        l->member_count++;
        s->line_members[i] = members[i].note;
        s->notes[members[i].note].membership_count++;
    }
    size_t offset = 0;
    for (size_t i = 0; i < s->note_count; i++) {
        note_t *n = &s->notes[i];
        n->membership_offset = offset;
        offset += n->membership_count;
        n->membership_count = 0;
    }
    for (size_t i = 0; i < unique; i++) {
        note_t *n = &s->notes[members[i].note];
        s->note_lines[n->membership_offset + n->membership_count++] = members[i].line;
    }
    for (size_t i = 0; i < s->order_count; i++) {
        size_t ix = s->order[i];
        if (moenotes_operate_type_is_judgement(s->notes[ix].v.operate_type))
            s->commands[s->command_count++] = ix;
    }
    release(&s->alc, members);
    return 1;
}

/* Keep source geometry for renderer sampling. Native rewrites only the exposed
 * Connection/ConnectionTrace geometry after final line construction. */
static void finalize_geometry(moenotes_score_t *s) {
    for (size_t i = 0; i < s->order_count; i++) {
        note_t *n = &s->notes[s->order[i]];
        const moenotes_note_view_t *v = &n->v;
        if (!v->slide_along || (v->operate_type != MOENOTES_OP_SLIDE_CONNECTION &&
                               v->operate_type != MOENOTES_OP_SLIDE_CONNECTION_TRACE) ||
            v->line_id < 0)
            continue;
        const line_t *l = &s->lines[v->line_id];
        const moenotes_note_view_t *a = NULL;
        for (size_t j = 0; j < l->member_count; j++) {
            const moenotes_note_view_t *b = &s->notes[s->line_members[l->member_offset + j]].v;
            if (b->generated || b->slide_along)
                continue;
            if (a && a->position.time_ms <= v->position.time_ms &&
                v->position.time_ms <= b->position.time_ms) {
                judgement_sample(&v->position, a, b, &n->final_geometry);
                n->final_lane_start = (int32_t)floor(n->final_geometry.lane_start);
                n->final_lane_end = (int32_t)ceil(n->final_geometry.lane_end);
                n->has_final_geometry = 1;
                break;
            }
            a = b;
        }
    }
}

static void expose_note(const note_t *n, moenotes_note_view_t *out) {
    *out = n->v;
    if (n->has_final_geometry) {
        out->lane_start_float = n->final_geometry.lane_start;
        out->lane_end_float = n->final_geometry.lane_end;
        out->width = n->final_geometry.width;
        out->lane_start = n->final_lane_start;
        out->lane_end = n->final_lane_end;
    }
}

static moenotes_result_t inflate_input(const moenotes_allocator_t *a, const void *data, size_t size,
                                       unsigned char **out, size_t *length) {
    if (size > MAX_CHART_INPUT)
        return MOENOTES_ERR_RANGE;
    if (size < 2 || ((const unsigned char *)data)[0] != 0x1f ||
        ((const unsigned char *)data)[1] != 0x8b) {
        *out = alloc(a, size + 1);
        if (!*out)
            return MOENOTES_ERR_OUT_OF_MEMORY;
        memcpy(*out, data, size);
        (*out)[size] = 0;
        *length = size;
        return MOENOTES_OK;
    }
    z_stream z;
    memset(&z, 0, sizeof(z));
    z.zalloc = z_alloc;
    z.zfree = z_free;
    z.opaque = (void *)a;
    int ret = inflateInit2(&z, 16 + MAX_WBITS);
    if (ret != Z_OK)
        return ret == Z_MEM_ERROR ? MOENOTES_ERR_OUT_OF_MEMORY : MOENOTES_ERR_GZIP;
    size_t cap = 4096;
    unsigned char *p = alloc(a, cap);
    moenotes_result_t result = MOENOTES_OK;
    if (!p) {
        inflateEnd(&z);
        return MOENOTES_ERR_OUT_OF_MEMORY;
    }
    z.next_in = (Bytef *)data;
    z.avail_in = (uInt)size;
    for (;;) {
        z.next_out = p + z.total_out;
        z.avail_out = (uInt)(cap - z.total_out);
        ret = inflate(&z, Z_NO_FLUSH);
        if (ret == Z_STREAM_END) {
            if (z.avail_in)
                result = MOENOTES_ERR_GZIP;
            break;
        }
        if (ret != Z_OK || (z.avail_in == 0 && z.avail_out != 0)) {
            result = ret == Z_MEM_ERROR ? MOENOTES_ERR_OUT_OF_MEMORY : MOENOTES_ERR_GZIP;
            break;
        }
        if (!z.avail_out) {
            if (cap == MAX_CHART_INPUT) {
                result = MOENOTES_ERR_RANGE;
                break;
            }
            cap *= 2;
            unsigned char *q = a->realloc_fn(a->ctx, p, cap);
            if (!q) {
                result = MOENOTES_ERR_OUT_OF_MEMORY;
                break;
            }
            p = q;
        }
    }
    *length = (size_t)z.total_out;
    inflateEnd(&z);
    if (result != MOENOTES_OK) {
        release(a, p);
        return result;
    }
    *out = p;
    return MOENOTES_OK;
}

moenotes_result_t moenotes_score_parse(const void *data, size_t size,
                                       const moenotes_parse_options_t *options,
                                       const moenotes_allocator_t *allocator,
                                       moenotes_score_t **out, char *error, size_t error_size) {
    moenotes_result_t result = MOENOTES_OK;
    if (out)
        *out = NULL;
    if (error && error_size)
        error[0] = 0;
    const moenotes_allocator_t *a = allocator ? allocator : &default_alc;
    moenotes_parse_options_t defaults;
    moenotes_default_parse_options(&defaults);
    const moenotes_parse_options_t *o = options ? options : &defaults;
    if (!data || !size || !out || !a->malloc_fn || !a->realloc_fn || !a->free_fn ||
        o->start_note_id < 0) {
        result = MOENOTES_ERR_INVALID_ARGUMENT;
        goto done;
    }
    if (o->slide_combo_unit != 0 && o->slide_combo_unit != 8) {
        result = MOENOTES_ERR_UNSUPPORTED;
        goto done;
    }
    unsigned char *json = NULL;
    size_t length = 0;
    result = inflate_input(a, data, size, &json, &length);
    if (result != MOENOTES_OK)
        goto done;
    size_t first = 0;
    while (first < length && (json[first] == ' ' || json[first] == '\n' || json[first] == '\r' ||
                              json[first] == '\t'))
        first++;
    if (first < length && json[first] == '#') {
        release(a, json);
        result = MOENOTES_ERR_UNSUPPORTED;
        goto done;
    }
    yyjson_alc ya = {yy_alloc, yy_realloc, yy_free, (void *)a};
    yyjson_read_err je;
    yyjson_doc *doc = yyjson_read_opts((char *)json, length, 0, &ya, &je);
    release(a, json);
    if (!doc) {
        result = je.code == YYJSON_READ_ERROR_MEMORY_ALLOCATION ? MOENOTES_ERR_OUT_OF_MEMORY
                                                                : MOENOTES_ERR_JSON;
        goto done;
    }
    moenotes_score_t *s = alloc(a, sizeof(*s));
    if (!s) {
        yyjson_doc_free(doc);
        result = MOENOTES_ERR_OUT_OF_MEMORY;
        goto done;
    }
    memset(s, 0, sizeof(*s));
    s->alc = *a;
    s->next_id = o->start_note_id;
    s->first_id = o->start_note_id;
    s->mirror = o->mirror;
    yyjson_val *root = yyjson_doc_get_root(doc), *score = get(root, "score");
    if (!score)
        score = root;
    yyjson_val *events = get(score, "events"), *notes = get(score, "notes");
    if (!yyjson_is_obj(score) || !yyjson_is_obj(events) || !yyjson_is_arr(notes))
        s->error = MOENOTES_ERR_SCHEMA;
    else if (build_timeline(s, events) && build_extra_events(s, events) && build_notes(s, notes)) {
        merge_graph(s);
        if ((!o->add_flick_hidden || add_hidden(s)) && (!o->slide_combo_unit || add_combos(s)) &&
            build_order(s))
            if (build_indices(s))
                finalize_geometry(s);
    }
    yyjson_doc_free(doc);
    result = s->error;
    if (result == MOENOTES_OK)
        *out = s;
    else
        moenotes_score_free(s);
done:
    if (error && error_size && result != MOENOTES_OK)
        snprintf(error, error_size, "%s", moenotes_result_string(result));
    return result;
}
void moenotes_score_free(moenotes_score_t *s) {
    if (!s)
        return;
    moenotes_allocator_t a = s->alc;
    for (size_t i = 0; i < s->event_count; i++)
        release(&a, s->events[i].values);
    release(&a, s->events);
    release(&a, s->notes);
    release(&a, s->lines);
    release(&a, s->order);
    release(&a, s->line_members);
    release(&a, s->note_lines);
    release(&a, s->commands);
    release(&a, s->bpms);
    release(&a, s->sigs);
    release(&a, s);
}
size_t moenotes_score_note_count(const moenotes_score_t *s) { return s ? s->order_count : 0; }
int32_t moenotes_score_lane_count(const moenotes_score_t *s) { return s ? LANES : 0; }
uint32_t moenotes_score_warnings(const moenotes_score_t *s) { return s ? s->warnings : 0; }
moenotes_result_t moenotes_score_note_at(const moenotes_score_t *s, size_t index,
                                         moenotes_note_view_t *out) {
    if (!s || !out)
        return MOENOTES_ERR_INVALID_ARGUMENT;
    if (index >= s->order_count)
        return MOENOTES_ERR_RANGE;
    expose_note(&s->notes[s->order[index]], out);
    return MOENOTES_OK;
}
moenotes_result_t moenotes_score_source_note_at(const moenotes_score_t *s, size_t index,
                                                moenotes_note_view_t *out) {
    if (!s || !out)
        return MOENOTES_ERR_INVALID_ARGUMENT;
    if (index >= s->order_count)
        return MOENOTES_ERR_RANGE;
    *out = s->notes[s->order[index]].v;
    return MOENOTES_OK;
}
size_t moenotes_score_bpm_count(const moenotes_score_t *s) { return s ? s->bpm_count : 0; }
moenotes_result_t moenotes_score_bpm_at(const moenotes_score_t *s, size_t index,
                                        moenotes_bpm_event_t *out) {
    if (!s || !out)
        return MOENOTES_ERR_INVALID_ARGUMENT;
    if (index >= s->bpm_count)
        return MOENOTES_ERR_RANGE;
    out->tick = s->bpms[index].tick;
    out->bpm = s->bpms[index].value;
    return position(s, out->tick, &out->position) ? MOENOTES_OK : MOENOTES_ERR_RANGE;
}
size_t moenotes_score_signature_count(const moenotes_score_t *s) { return s ? s->sig_count : 0; }
moenotes_result_t moenotes_score_signature_at(const moenotes_score_t *s, size_t index,
                                              moenotes_signature_event_t *out) {
    if (!s || !out)
        return MOENOTES_ERR_INVALID_ARGUMENT;
    if (index >= s->sig_count)
        return MOENOTES_ERR_RANGE;
    out->tick = s->sigs[index].tick;
    out->numerator = s->sigs[index].num;
    out->denominator = s->sigs[index].den;
    return position(s, out->tick, &out->position) ? MOENOTES_OK : MOENOTES_ERR_RANGE;
}
moenotes_result_t moenotes_score_position_at_tick(const moenotes_score_t *s, int32_t tick,
                                                  moenotes_position_t *out) {
    if (!s || !out)
        return MOENOTES_ERR_INVALID_ARGUMENT;
    return position(s, tick, out) ? MOENOTES_OK : MOENOTES_ERR_RANGE;
}
moenotes_result_t moenotes_score_note_position_at_tick(const moenotes_score_t *s, int32_t tick,
                                                       moenotes_position_t *out) {
    if (!s || !out)
        return MOENOTES_ERR_INVALID_ARGUMENT;
    return note_position(s, tick, out) ? MOENOTES_OK : MOENOTES_ERR_RANGE;
}
size_t moenotes_score_event_count(const moenotes_score_t *s) { return s ? s->event_count : 0; }
moenotes_result_t moenotes_score_event_at(const moenotes_score_t *s, size_t index,
                                          moenotes_event_t *out) {
    if (!s || !out)
        return MOENOTES_ERR_INVALID_ARGUMENT;
    if (index >= s->event_count)
        return MOENOTES_ERR_RANGE;
    *out = s->events[index].v;
    return MOENOTES_OK;
}
moenotes_result_t moenotes_score_event_value_at(const moenotes_score_t *s, size_t index,
                                                size_t value, int32_t *out) {
    if (!s || !out)
        return MOENOTES_ERR_INVALID_ARGUMENT;
    if (index >= s->event_count || value >= s->events[index].v.value_count)
        return MOENOTES_ERR_RANGE;
    *out = s->events[index].values[value];
    return MOENOTES_OK;
}
size_t moenotes_score_call_rhythm_count(const moenotes_score_t *s, size_t index) {
    if (!s || index >= s->event_count || s->events[index].v.type != MOENOTES_EVENT_CALL)
        return 0;
    const event_t *e = &s->events[index];
    size_t count = 0;
    for (size_t i = 0; i < e->v.value_count; i++)
        count += e->values[i] == 1;
    return count;
}
moenotes_result_t moenotes_score_call_rhythm_at(const moenotes_score_t *s, size_t index,
                                               size_t rhythm, double *out) {
    if (!s || !out)
        return MOENOTES_ERR_INVALID_ARGUMENT;
    if (index >= s->event_count || s->events[index].v.type != MOENOTES_EVENT_CALL)
        return MOENOTES_ERR_RANGE;
    const event_t *e = &s->events[index];
    for (size_t i = 0; i < e->v.value_count; i++)
        if (e->values[i] == 1) {
            if (!rhythm) {
                *out = ((float)i + 1.0f) / (float)e->v.value_count;
                return MOENOTES_OK;
            }
            rhythm--;
        }
    return MOENOTES_ERR_RANGE;
}
size_t moenotes_score_bar_line_count(const moenotes_score_t *s) {
    if (!s)
        return 0;
    int32_t bar = 0;
    for (size_t i = 0; i < s->note_count; i++)
        if (!s->notes[i].v.generated && s->notes[i].v.position.bar > bar)
            bar = s->notes[i].v.position.bar;
    return (size_t)bar + 1;
}
moenotes_result_t moenotes_score_bar_line_at(const moenotes_score_t *s, size_t index,
                                            moenotes_position_t *out) {
    if (!s || !out)
        return MOENOTES_ERR_INVALID_ARGUMENT;
    if (index >= moenotes_score_bar_line_count(s) || index > INT32_MAX)
        return MOENOTES_ERR_RANGE;
    const sig_t *g = sig_at_bar(s, (int32_t)index);
    int64_t tick = (int64_t)g->tick + ((int64_t)index - g->bar) * g->length;
    if (tick < 0 || tick > INT32_MAX || !position(s, (int32_t)tick, out))
        return MOENOTES_ERR_RANGE;
    return MOENOTES_OK;
}
static float last_position_key(const moenotes_score_t *s) {
    float key = -1.0f;
    for (size_t i = 0; i < s->order_count; i++) {
        const moenotes_position_t *p = &s->notes[s->order[i]].v.position;
        float current = (float)p->bar + (float)p->bar_progress;
        if (current > key)
            key = current;
    }
    return key;
}
size_t moenotes_score_last_timing_note_count(const moenotes_score_t *s) {
    if (!s)
        return 0;
    float key = last_position_key(s);
    size_t count = 0;
    for (size_t i = 0; i < s->order_count; i++) {
        const moenotes_position_t *p = &s->notes[s->order[i]].v.position;
        count += (float)p->bar + (float)p->bar_progress == key;
    }
    return count;
}
moenotes_result_t moenotes_score_last_timing_note_at(const moenotes_score_t *s, size_t index,
                                                    moenotes_note_view_t *out) {
    if (!s || !out)
        return MOENOTES_ERR_INVALID_ARGUMENT;
    float key = last_position_key(s);
    for (size_t i = 0; i < s->order_count; i++) {
        const moenotes_position_t *p = &s->notes[s->order[i]].v.position;
        if ((float)p->bar + (float)p->bar_progress == key) {
            if (!index) {
                expose_note(&s->notes[s->order[i]], out);
                return MOENOTES_OK;
            }
            index--;
        }
    }
    return MOENOTES_ERR_RANGE;
}
size_t moenotes_score_line_count(const moenotes_score_t *s) { return s ? s->line_count : 0; }
static const note_t *note_by_id(const moenotes_score_t *s, int32_t id) {
    if (!s || id < s->first_id)
        return NULL;
    size_t i = (size_t)((int64_t)id - s->first_id);
    if (i >= s->note_count || s->notes[i].alias != SIZE_MAX)
        return NULL;
    return &s->notes[i];
}
moenotes_result_t moenotes_score_note_fever_event(const moenotes_score_t *s, int32_t id,
                                                 int32_t *out) {
    if (!s || !out)
        return MOENOTES_ERR_INVALID_ARGUMENT;
    const note_t *n = note_by_id(s, id);
    if (!n)
        return MOENOTES_ERR_RANGE;
    *out = -1;
    for (size_t i = 0; i < s->event_count; i++) {
        const moenotes_event_t *e = &s->events[i].v;
        if (e->type == MOENOTES_EVENT_FEVER && e->position.time_ms <= n->v.position.time_ms &&
            n->v.position.time_ms <= e->end_position.time_ms) {
            *out = (int32_t)i;
            break;
        }
    }
    return MOENOTES_OK;
}
size_t moenotes_score_note_line_count(const moenotes_score_t *s, int32_t id) {
    const note_t *n = note_by_id(s, id);
    return n ? n->membership_count : 0;
}
moenotes_result_t moenotes_score_note_line_at(const moenotes_score_t *s, int32_t id, size_t index,
                                              int32_t *out) {
    if (!s || !out)
        return MOENOTES_ERR_INVALID_ARGUMENT;
    const note_t *n = note_by_id(s, id);
    if (!n || index >= n->membership_count)
        return MOENOTES_ERR_RANGE;
    *out = s->note_lines[n->membership_offset + index];
    return MOENOTES_OK;
}
size_t moenotes_score_line_member_count(const moenotes_score_t *s, int32_t id) {
    return s && id >= 0 && (size_t)id < s->line_count ? s->lines[id].member_count : 0;
}
moenotes_result_t moenotes_score_line_member_at(const moenotes_score_t *s, int32_t id,
                                                size_t index, moenotes_note_view_t *out) {
    if (!s || !out)
        return MOENOTES_ERR_INVALID_ARGUMENT;
    if (id < 0 || (size_t)id >= s->line_count || index >= s->lines[id].member_count)
        return MOENOTES_ERR_RANGE;
    expose_note(&s->notes[s->line_members[s->lines[id].member_offset + index]], out);
    return MOENOTES_OK;
}
moenotes_result_t moenotes_score_line_at(const moenotes_score_t *s, size_t index,
                                          moenotes_line_view_t *out) {
    if (!s || !out)
        return MOENOTES_ERR_INVALID_ARGUMENT;
    if (index >= s->line_count)
        return MOENOTES_ERR_RANGE;
    const line_t *l = &s->lines[index];
    const moenotes_note_view_t *first = &s->notes[l->first].v;
    *out = (moenotes_line_view_t){(int32_t)index, l->slot, first->source_index,
        s->notes[canonical(s, l->first)].v.id,
        s->notes[canonical(s, l->first + l->count - 1)].v.id, (uint8_t)l->guide};
    return MOENOTES_OK;
}
static moenotes_result_t sample_line(const moenotes_score_t *s, int32_t id, int32_t tick,
                                     int judgement, moenotes_line_sample_t *out) {
    if (!s || !out)
        return MOENOTES_ERR_INVALID_ARGUMENT;
    if (id < 0 || (size_t)id >= s->line_count)
        return MOENOTES_ERR_RANGE;
    const line_t *l = &s->lines[id];
    const moenotes_note_view_t *first = &s->notes[l->first].v,
                               *last = &s->notes[l->first + l->count - 1].v;
    if (tick < first->tick || tick > last->tick)
        return MOENOTES_ERR_RANGE;
    size_t left = 0, right = l->count - 1;
    for (size_t i = 1; i < l->count; i++) {
        const moenotes_note_view_t *v = &s->notes[l->first + i].v;
        if (v->pos_auto && i + 1 < l->count)
            continue;
        if (v->tick >= tick) {
            right = i;
            break;
        }
        left = i;
    }
    moenotes_note_view_t a = s->notes[l->first + left].v;
    if (judgement) {
        moenotes_position_t p;
        if (!note_position(s, tick, &p))
            return MOENOTES_ERR_RANGE;
        judgement_sample(&p, &a, &s->notes[l->first + right].v, out);
        return MOENOTES_OK;
    }
    if (s->mirror) {
        moenotes_ease_t e = a.ease_left;
        a.ease_left = a.ease_right;
        a.ease_right = e;
    }
    interpolate(&a, &s->notes[l->first + right].v, tick, out);
    return MOENOTES_OK;
}
moenotes_result_t moenotes_score_sample_line(const moenotes_score_t *s, int32_t id, int32_t tick,
                                             moenotes_line_sample_t *out) {
    return sample_line(s, id, tick, 0, out);
}
moenotes_result_t moenotes_score_sample_judgement_line(const moenotes_score_t *s, int32_t id,
                                                       int32_t tick, moenotes_line_sample_t *out) {
    return sample_line(s, id, tick, 1, out);
}
uint32_t moenotes_score_full_combo_count(const moenotes_score_t *s, uint8_t combos,
                                         uint8_t hidden) {
    if (!s)
        return 0;
    uint32_t n = 0;
    for (size_t i = 0; i < s->note_count; i++) {
        if (s->notes[i].alias != SIZE_MAX)
            continue;
        const moenotes_note_view_t *v = &s->notes[i].v;
        if ((moenotes_operate_type_is_judgement(v->operate_type) &&
             (combos || v->operate_type != MOENOTES_OP_COMBO)) ||
            (hidden && v->hidden_for_note_id >= 0))
            n++;
    }
    return n;
}
size_t moenotes_score_command_count(const moenotes_score_t *s) {
    return s ? s->command_count : 0;
}
moenotes_result_t moenotes_score_command_at(const moenotes_score_t *s, size_t index, int32_t type,
                                            int32_t life, int32_t combo, moenotes_command_t *out) {
    if (!s || !out)
        return MOENOTES_ERR_INVALID_ARGUMENT;
    if (index > INT32_MAX || (int64_t)combo + (int64_t)index > INT32_MAX)
        return MOENOTES_ERR_RANGE;
    if (index >= s->command_count)
        return MOENOTES_ERR_RANGE;
    const moenotes_note_view_t *v = &s->notes[s->commands[index]].v;
    *out = (moenotes_command_t){
        v->position.time_ms, combo + (int32_t)index, life, v->id, v->operate_type, type};
    return MOENOTES_OK;
}
