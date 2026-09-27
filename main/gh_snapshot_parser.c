#include "gh_snapshot_parser.h"

#include <stdio.h>
#include <string.h>

static float json_number(const cJSON *parent, const char *name)
{
    if (parent == NULL) return 0.0f;
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
    return cJSON_IsNumber(item) ? (float)item->valuedouble : 0.0f;
}

static gh_source_mode_t parse_source_mode(const cJSON *energy)
{
    const cJSON *source = cJSON_GetObjectItemCaseSensitive(energy, "source");
    const cJSON *mode = cJSON_GetObjectItemCaseSensitive(source, "source_mode");
    if (!cJSON_IsString(mode)) return GH_SOURCE_UNKNOWN;
    if (strcmp(mode->valuestring, "simulator") == 0) return GH_SOURCE_SIMULATOR;
    if (strcmp(mode->valuestring, "real") == 0 ||
        strcmp(mode->valuestring, "home_assistant") == 0) return GH_SOURCE_REAL;
    if (strcmp(mode->valuestring, "replay") == 0) return GH_SOURCE_REPLAY;
    return GH_SOURCE_UNKNOWN;
}

bool gh_snapshot_apply_energy(const cJSON *root, gh_app_model_t *model)
{
    if (root == NULL || model == NULL) return false;
    const cJSON *energy = cJSON_GetObjectItemCaseSensitive(root, "energy");
    const cJSON *power = cJSON_GetObjectItemCaseSensitive(energy, "power");
    const cJSON *storage = cJSON_GetObjectItemCaseSensitive(energy, "storage");
    if (!cJSON_IsObject(energy) || !cJSON_IsObject(power) ||
        !cJSON_IsObject(storage)) return false;

    model->energy.pv_kw = json_number(power, "solar_w") / 1000.0f;
    model->energy.home_kw = json_number(power, "home_load_w") / 1000.0f;
    const float import_kw = json_number(power, "grid_import_w") / 1000.0f;
    const float export_kw = json_number(power, "grid_export_w") / 1000.0f;
    model->energy.grid_kw = import_kw > 0 ? import_kw : export_kw;
    model->energy.grid_importing = import_kw > 0;
    model->energy.battery_soc_pct = json_number(storage, "soc_percent");
    model->energy.source_mode = parse_source_mode(energy);
    return true;
}

static void copy_json_string(char *destination, size_t size,
                             const cJSON *parent, const char *name)
{
    const cJSON *item = parent == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(parent, name);
    if (size == 0) return;
    if (cJSON_IsString(item)) {
        snprintf(destination, size, "%s", item->valuestring);
    } else {
        destination[0] = '\0';
    }
}

static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = (unsigned)(year - era * 400);
    const unsigned adjusted_month = month > 2 ? month - 3U : month + 9U;
    const unsigned day_of_year =
        (153U * adjusted_month + 2U) / 5U + day - 1U;
    const unsigned day_of_era =
        year_of_era * 365U + year_of_era / 4U - year_of_era / 100U + day_of_year;
    return (int64_t)era * 146097 + (int64_t)day_of_era - 719468;
}

static bool parse_utc_datetime(const char *value, int64_t *epoch)
{
    if (value == NULL || epoch == NULL) return false;
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (sscanf(value, "%4d-%2d-%2dT%2d:%2d:%2d",
               &year, &month, &day, &hour, &minute, &second) != 6) return false;
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 60) return false;
    *epoch = days_from_civil(year, (unsigned)month, (unsigned)day) * 86400 +
             hour * 3600 + minute * 60 + second;
    return true;
}

static void clear_pending_action_set(gh_app_model_t *model)
{
    memset(&model->proposal, 0, sizeof(model->proposal));
    model->action_set_id = 0;
    model->approval_nonce[0] = '\0';
    model->action_set_version[0] = '\0';
    model->agent_state = model->backend_online ? GH_AGENT_MONITORING : GH_AGENT_OFFLINE;
}

bool gh_snapshot_apply_pending_action_set(const cJSON *root,
                                          gh_app_model_t *model,
                                          int64_t now_epoch)
{
    if (root == NULL || model == NULL) return false;
    const cJSON *action_set =
        cJSON_GetObjectItemCaseSensitive(root, "pending_action_set");
    const cJSON *challenge =
        cJSON_GetObjectItemCaseSensitive(root, "approval_challenge");
    if (cJSON_IsNull(action_set) || action_set == NULL) {
        clear_pending_action_set(model);
        return challenge == NULL || cJSON_IsNull(challenge);
    }
    if (!cJSON_IsObject(action_set) || !cJSON_IsObject(challenge)) {
        clear_pending_action_set(model);
        return false;
    }

    const cJSON *action_set_id = cJSON_GetObjectItemCaseSensitive(action_set, "id");
    const cJSON *challenge_id =
        cJSON_GetObjectItemCaseSensitive(challenge, "action_set_id");
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(action_set, "items");
    const cJSON *nonce = cJSON_GetObjectItemCaseSensitive(challenge, "nonce");
    const cJSON *version =
        cJSON_GetObjectItemCaseSensitive(challenge, "action_set_version");
    const cJSON *expires = cJSON_GetObjectItemCaseSensitive(challenge, "expires_at");
    if (!cJSON_IsNumber(action_set_id) || action_set_id->valueint <= 0 ||
        !cJSON_IsNumber(challenge_id) || challenge_id->valueint != action_set_id->valueint ||
        !cJSON_IsArray(items) || cJSON_GetArraySize(items) == 0 ||
        !cJSON_IsString(nonce) || nonce->valuestring[0] == '\0' ||
        !cJSON_IsString(version) || version->valuestring[0] == '\0' ||
        !cJSON_IsString(expires)) {
        clear_pending_action_set(model);
        return false;
    }

    gh_proposal_t proposal = {0};
    copy_json_string(proposal.title, sizeof(proposal.title), action_set, "title");
    copy_json_string(proposal.status, sizeof(proposal.status), action_set, "status");
    copy_json_string(proposal.reason, sizeof(proposal.reason), action_set, "reason");
    snprintf(proposal.expires_at, sizeof(proposal.expires_at), "%s", expires->valuestring);
    if (strcmp(proposal.status, "PENDING") != 0 ||
        !parse_utc_datetime(expires->valuestring, &proposal.expires_at_epoch)) {
        clear_pending_action_set(model);
        return false;
    }
    proposal.expired = now_epoch > 0 && now_epoch >= proposal.expires_at_epoch;

    const cJSON *expected_grid_delta =
        cJSON_GetObjectItemCaseSensitive(action_set, "expected_grid_delta_w");
    if (cJSON_IsNumber(expected_grid_delta)) {
        proposal.expected_grid_delta_available = true;
        proposal.expected_grid_delta_w = (float)expected_grid_delta->valuedouble;
        snprintf(proposal.expected_impact, sizeof(proposal.expected_impact),
                 "预计电网功率变化 %.0f W", proposal.expected_grid_delta_w);
    }

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, items) {
        if (proposal.action_count >= GH_MAX_ACTIONS) break;
        const cJSON *proposal_id = cJSON_GetObjectItemCaseSensitive(item, "proposal_id");
        const cJSON *current_value = cJSON_GetObjectItemCaseSensitive(item, "current_value");
        const cJSON *target_value = cJSON_GetObjectItemCaseSensitive(item, "target_value");
        if (!cJSON_IsNumber(proposal_id) || proposal_id->valueint <= 0 ||
            !cJSON_IsNumber(current_value) || !cJSON_IsNumber(target_value)) {
            clear_pending_action_set(model);
            return false;
        }
        gh_action_step_t *step = &proposal.actions[proposal.action_count++];
        step->proposal_id = proposal_id->valueint;
        copy_json_string(step->source_device_id, sizeof(step->source_device_id),
                         item, "source_device_id");
        copy_json_string(step->device_name, sizeof(step->device_name), item, "device_name");
        copy_json_string(step->capability, sizeof(step->capability), item, "capability");
        copy_json_string(step->status, sizeof(step->status), item, "status");
        step->current_value = (float)current_value->valuedouble;
        step->target_value = (float)target_value->valuedouble;
        const cJSON *expected_delta =
            cJSON_GetObjectItemCaseSensitive(item, "expected_delta_w");
        if (cJSON_IsNumber(expected_delta)) {
            step->expected_delta_available = true;
            step->expected_delta_w = (float)expected_delta->valuedouble;
        }
        char capability_text[sizeof(step->capability)];
        snprintf(capability_text, sizeof(capability_text), "%s", step->capability);
        snprintf(step->action, sizeof(step->action), "%s %.0f -> %.0f",
                 capability_text, step->current_value, step->target_value);
        if (proposal.proposal_id[0] == '\0') {
            snprintf(proposal.proposal_id, sizeof(proposal.proposal_id), "%d",
                     step->proposal_id);
        }
    }
    if (proposal.action_count == 0 || proposal.proposal_id[0] == '\0') {
        clear_pending_action_set(model);
        return false;
    }

    proposal.pending = true;
    model->proposal = proposal;
    model->action_set_id = action_set_id->valueint;
    snprintf(model->approval_nonce, sizeof(model->approval_nonce), "%s", nonce->valuestring);
    snprintf(model->action_set_version, sizeof(model->action_set_version), "%s",
             version->valuestring);
    model->agent_state = GH_AGENT_PENDING_APPROVAL;
    model->screen = GH_SCREEN_PROPOSAL;
    return true;
}

bool gh_snapshot_apply_current_action_set(const cJSON *root,
                                          gh_app_model_t *model)
{
    if (root == NULL || model == NULL) return false;
    const cJSON *action_set =
        cJSON_GetObjectItemCaseSensitive(root, "current_action_set");
    if (action_set == NULL || cJSON_IsNull(action_set)) return true;
    if (!cJSON_IsObject(action_set)) return false;
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(action_set, "id");
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(action_set, "items");
    const cJSON *status = cJSON_GetObjectItemCaseSensitive(action_set, "status");
    if (!cJSON_IsNumber(id) || id->valueint <= 0 || !cJSON_IsArray(items) ||
        !cJSON_IsString(status)) return false;

    gh_proposal_t runtime = model->proposal;
    memset(runtime.actions, 0, sizeof(runtime.actions));
    runtime.action_count = 0;
    copy_json_string(runtime.title, sizeof(runtime.title), action_set, "title");
    copy_json_string(runtime.reason, sizeof(runtime.reason), action_set, "reason");
    copy_json_string(runtime.status, sizeof(runtime.status), action_set, "status");
    copy_json_string(runtime.verification_status,
                     sizeof(runtime.verification_status), action_set,
                     "verification_status");
    copy_json_string(runtime.verification_message,
                     sizeof(runtime.verification_message), action_set,
                     "verification_message");
    const cJSON *before =
        cJSON_GetObjectItemCaseSensitive(action_set, "before_grid_power_w");
    const cJSON *after =
        cJSON_GetObjectItemCaseSensitive(action_set, "after_grid_power_w");
    const cJSON *actual =
        cJSON_GetObjectItemCaseSensitive(action_set, "actual_grid_delta_w");
    runtime.before_grid_available = cJSON_IsNumber(before);
    runtime.after_grid_available = cJSON_IsNumber(after);
    runtime.actual_grid_delta_available = cJSON_IsNumber(actual);
    if (runtime.before_grid_available) runtime.before_grid_power_w = before->valuedouble;
    if (runtime.after_grid_available) runtime.after_grid_power_w = after->valuedouble;
    if (runtime.actual_grid_delta_available) runtime.actual_grid_delta_w = actual->valuedouble;

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, items) {
        if (runtime.action_count >= GH_MAX_ACTIONS) break;
        gh_action_step_t *step = &runtime.actions[runtime.action_count++];
        const cJSON *proposal_id = cJSON_GetObjectItemCaseSensitive(item, "proposal_id");
        const cJSON *execution_id = cJSON_GetObjectItemCaseSensitive(item, "execution_id");
        step->proposal_id = cJSON_IsNumber(proposal_id) ? proposal_id->valueint : 0;
        step->execution_id = cJSON_IsNumber(execution_id) ? execution_id->valueint : 0;
        copy_json_string(step->device_name, sizeof(step->device_name), item,
                         "device_name");
        copy_json_string(step->capability, sizeof(step->capability), item,
                         "capability");
        copy_json_string(step->status, sizeof(step->status), item, "status");
        copy_json_string(step->result_code, sizeof(step->result_code), item,
                         "result_code");
        copy_json_string(step->result_message, sizeof(step->result_message), item,
                         "result_message");
        step->current_value = json_number(item, "current_value");
        step->target_value = json_number(item, "target_value");
        char capability_text[sizeof(step->capability)];
        snprintf(capability_text, sizeof(capability_text), "%s",
                 step->capability);
        snprintf(step->action, sizeof(step->action), "%s %.0f -> %.0f",
                 capability_text, step->current_value, step->target_value);
    }
    model->proposal = runtime;
    model->action_set_id = id->valueint;
    model->proposal.pending = strcmp(status->valuestring, "PENDING") == 0;
    if (strcmp(status->valuestring, "APPROVED") == 0 ||
        strcmp(status->valuestring, "EXECUTING") == 0) {
        model->agent_state = GH_AGENT_EXECUTING;
        model->screen = GH_SCREEN_EXECUTION;
    } else if ((strcmp(status->valuestring, "SUCCEEDED") == 0 ||
                strcmp(status->valuestring, "PARTIAL") == 0 ||
                strcmp(status->valuestring, "BLOCKED") == 0) &&
               model->acknowledged_action_set_id != id->valueint) {
        model->agent_state = strcmp(status->valuestring, "SUCCEEDED") == 0
                                 ? GH_AGENT_VERIFIED
                                 : GH_AGENT_FAILED;
        model->screen = GH_SCREEN_VERIFICATION;
    }
    return true;
}
