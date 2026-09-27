#pragma once

#include <stdbool.h>

#include "cJSON.h"
#include "gh_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Apply the energy section of a real Companion Snapshot payload.
 * Returns false when the required energy/power/storage structure is absent.
 * Unknown source strings remain explicitly GH_SOURCE_UNKNOWN. */
bool gh_snapshot_apply_energy(const cJSON *root, gh_app_model_t *model);

/** Apply the pending Action Set and its approval challenge from the same real
 * Companion Snapshot payload. A null pending Action Set is valid and clears
 * the previous approval state. Returns false for a malformed or unbound
 * challenge, leaving approval disabled. `now_epoch` is UTC Unix time. */
bool gh_snapshot_apply_pending_action_set(const cJSON *root,
                                          gh_app_model_t *model,
                                          int64_t now_epoch);

/** Apply backend-truth execution and unified verification state. */
bool gh_snapshot_apply_current_action_set(const cJSON *root,
                                          gh_app_model_t *model);

#ifdef __cplusplus
}
#endif
