#ifndef MNS_MOENOTES_SCORE_H
#define MNS_MOENOTES_SCORE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MNS_ABI_VERSION 1
typedef struct mns_prepared mns_prepared;
typedef struct mns_deck mns_deck;
typedef struct mns_analysis mns_analysis;
typedef enum mns_status {
    MNS_OK = 0,
    MNS_INVALID_INPUT = 1,
    MNS_OUT_OF_MEMORY = 2,
    MNS_INTERNAL_ERROR = 3
} mns_status;
const char *mns_version(void);
/* Borrowed error text until the next API operation on this thread. */
const char *mns_last_error(void);
/* Returned UTF-8 strings belong to the caller; release with mns_string_free. */
void mns_string_free(char *value);
/* Inputs are borrowed for the call, then owned immutable preparation is retained.
 * master_json is {"MasterTableName":{"_allData":[...]},...}.
 * request declares schema_version=1, music_score_id, mode and optional context.
 * Every failure sets *out=NULL. Prepared/deck handles may be freed after creating
 * dependent handles; those retain shared ownership internally. */
mns_status mns_prepare(const char *master_json, size_t master_size, const void *chart, size_t chart_size,
                       const char *chart_name, const char *request_json, size_t request_size,
                       mns_prepared **out);
void mns_prepared_free(mns_prepared *prepared);
mns_status mns_prepared_report(const mns_prepared *prepared, char **out_json);
/* JSON is either structured deck/account or player_data/deck_id, plus deck_context. */
mns_status mns_deck_resolve(const mns_prepared *prepared, const char *request_json, size_t request_size,
                            mns_deck **out);
void mns_deck_free(mns_deck *deck);
mns_status mns_deck_report(const mns_deck *deck, char **out_json);
/* Options: contributions, ranks[3], memory_limit_bytes, exact_memory_limit_bytes,
 * estimator_seed. Changing these requires a new analysis; estimates are not
 * original-game seed predictions. No implicit I/O, threads, or network. */
mns_status mns_analysis_create(const mns_prepared *prepared, const mns_deck *deck, const char *options_json,
                               size_t options_size, mns_analysis **out);
void mns_analysis_free(mns_analysis *analysis);
/* Single-writer. advance is cooperative; budget_ms bounds work between complete
 * kernel calls. Work may run slightly longer to finish one path. */
mns_status mns_analysis_advance(mns_analysis *analysis, uint32_t budget_ms, uint64_t max_evaluations);
mns_status mns_analysis_snapshot(const mns_analysis *analysis, char **out_json);
void mns_analysis_cancel(mns_analysis *analysis);
/* Clears cancellation only. A memory-limited exact lane remains frozen;
 * start a new session with a larger memory budget to restart exact search. */
void mns_analysis_resume(mns_analysis *analysis);
#ifdef __cplusplus
}
#endif
#endif
