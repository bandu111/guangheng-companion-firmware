#include "gh_model.h"

#include <string.h>
#include <stdio.h>

void gh_model_init(gh_app_model_t *model)
{
    memset(model, 0, sizeof(*model));
    model->energy = (gh_energy_snapshot_t) {
        .source_mode = GH_SOURCE_UNKNOWN,
        .stale = true,
    };
    model->agent_state = GH_AGENT_OFFLINE;
    model->screen = GH_SCREEN_BOOT;
    model->backend_online = false;
    model->websocket_online = false;
    model->magnet_attached = false;
}

bool gh_model_can_approve(const gh_app_model_t *model)
{
    return model != NULL && model->backend_online &&
           !model->energy.stale &&
           model->agent_state == GH_AGENT_PENDING_APPROVAL && model->proposal.pending &&
           !model->proposal.expired && model->proposal.proposal_id[0] != '\0' &&
           model->action_set_id > 0 && model->approval_nonce[0] != '\0' &&
           model->action_set_version[0] != '\0' &&
           strcmp(model->proposal.status, "PENDING") == 0;
}

bool gh_model_begin_approval(gh_app_model_t *model)
{
    if (!gh_model_can_approve(model)) {
        return false;
    }
    model->agent_state = GH_AGENT_EXECUTING;
    model->screen = GH_SCREEN_EXECUTION;
    model->proposal.pending = false;
    model->generation++;
    return true;
}

void gh_model_cancel_approval(gh_app_model_t *model)
{
    if (model == NULL || model->agent_state != GH_AGENT_EXECUTING) {
        return;
    }
    model->agent_state = GH_AGENT_PENDING_APPROVAL;
    model->screen = GH_SCREEN_PROPOSAL;
    model->proposal.pending = true;
    model->generation++;
}

void gh_model_set_offline(gh_app_model_t *model)
{
    if (model == NULL) {
        return;
    }
    model->backend_online = false;
    model->websocket_online = false;
    model->agent_state = GH_AGENT_OFFLINE;
    model->screen = GH_SCREEN_OFFLINE;
    model->voice_active = false;
    model->energy.stale = true;
    model->generation++;
}

void gh_model_open_screen(gh_app_model_t *model, gh_screen_t screen)
{
    if (model == NULL || screen >= GH_SCREEN_COUNT) return;
    if (screen == GH_SCREEN_AMBIENT &&
        model->screen == GH_SCREEN_VERIFICATION && model->action_set_id > 0) {
        model->acknowledged_action_set_id = model->action_set_id;
    }
    model->screen = screen;
    model->generation++;
}

void gh_model_route_runtime(gh_app_model_t *model)
{
    if (model == NULL) return;
    gh_screen_t target = model->screen;
    if (model->pairing_code[0] != '\0') {
        target = GH_SCREEN_PAIRING;
    } else if (model->connection_state == GH_CONNECTION_CONNECTING) {
        target = GH_SCREEN_WIFI_CONNECTING;
    } else if (model->connection_state == GH_CONNECTION_TIME_SYNCING) {
        target = GH_SCREEN_TIME_SYNC;
    } else if (model->connection_state == GH_CONNECTION_BACKEND_CONNECTING) {
        target = GH_SCREEN_BACKEND_CONNECTING;
    } else if (!model->backend_online || model->energy.stale) {
        target = GH_SCREEN_OFFLINE;
    } else if (model->proposal.pending) {
        if (model->screen != GH_SCREEN_APPROVAL &&
            model->screen != GH_SCREEN_VOICE_LISTENING &&
            model->screen != GH_SCREEN_VOICE_TRANSCRIBING &&
            model->screen != GH_SCREEN_VOICE_RESULT) {
            target = GH_SCREEN_PROPOSAL;
        }
    } else if (model->screen == GH_SCREEN_BOOT ||
               model->screen == GH_SCREEN_WIFI_CONNECTING ||
               model->screen == GH_SCREEN_TIME_SYNC ||
               model->screen == GH_SCREEN_BACKEND_CONNECTING ||
               model->screen == GH_SCREEN_PAIRING ||
               model->screen == GH_SCREEN_PROPOSAL ||
               model->screen == GH_SCREEN_APPROVAL ||
               model->screen == GH_SCREEN_OFFLINE) {
        target = GH_SCREEN_AMBIENT;
    }
    if (target != model->screen) {
        model->screen = target;
        model->generation++;
    }
}

void gh_model_voice_begin(gh_app_model_t *model)
{
    if (model == NULL || !model->backend_online || model->energy.stale) return;
    model->voice_active = true;
    model->voice_state = GH_VOICE_LISTENING;
    model->voice_rms = 0;
    model->voice_peak = 0;
    model->voice_speech = false;
    model->voice_transcript[0] = '\0';
    model->voice_answer[0] = '\0';
    model->voice_intent[0] = '\0';
    model->voice_error[0] = '\0';
    model->voice_requires_touch_approval = false;
    model->screen = GH_SCREEN_VOICE_LISTENING;
    model->generation++;
}

void gh_model_voice_level(gh_app_model_t *model, uint16_t rms, uint16_t peak,
                          bool speech)
{
    if (model == NULL || model->voice_state != GH_VOICE_LISTENING) return;
    model->voice_rms = rms;
    model->voice_peak = peak;
    model->voice_speech = speech;
    model->generation++;
}

void gh_model_voice_transcribing(gh_app_model_t *model)
{
    if (model == NULL || !model->voice_active) return;
    model->voice_state = GH_VOICE_TRANSCRIBING;
    model->screen = GH_SCREEN_VOICE_TRANSCRIBING;
    model->generation++;
}

void gh_model_voice_complete(gh_app_model_t *model, const char *transcript,
                             const char *answer, const char *intent,
                             bool requires_touch_approval)
{
    if (model == NULL) return;
    model->voice_active = false;
    model->voice_state = GH_VOICE_COMPLETED;
    snprintf(model->voice_transcript, sizeof(model->voice_transcript), "%s",
             transcript ? transcript : "");
    snprintf(model->voice_answer, sizeof(model->voice_answer), "%s",
             answer ? answer : "");
    snprintf(model->voice_intent, sizeof(model->voice_intent), "%s",
             intent ? intent : "UNKNOWN");
    model->voice_requires_touch_approval = requires_touch_approval;
    if (requires_touch_approval && model->proposal.pending) {
        /* Voice may reveal the Proposal but never enter Approval or execute. */
        model->screen = GH_SCREEN_PROPOSAL;
    } else {
        model->screen = GH_SCREEN_VOICE_RESULT;
    }
    model->generation++;
}

void gh_model_voice_fail(gh_app_model_t *model, const char *message)
{
    if (model == NULL) return;
    model->voice_active = false;
    model->voice_state = GH_VOICE_FAILED;
    snprintf(model->voice_error, sizeof(model->voice_error), "%s",
             message ? message : "没听清，请再说一次。");
    model->screen = GH_SCREEN_VOICE_RESULT;
    model->generation++;
}

void gh_model_mark_snapshot_fresh(gh_app_model_t *model, int64_t synchronized_at)
{
    if (model == NULL) {
        return;
    }
    model->backend_online = true;
    model->energy.stale = false;
    model->energy.last_successful_sync_at = synchronized_at;
}

void gh_model_set_lifted(gh_app_model_t *model, bool lifted)
{
    if (model == NULL) {
        return;
    }
    model->lifted = lifted;
    if (lifted && model->proposal.pending &&
        model->screen != GH_SCREEN_APPROVAL) {
        /* A lift may reveal a real Proposal, but can never enter or perform
         * Approval. */
        model->screen = GH_SCREEN_PROPOSAL;
    } else if (lifted && model->screen == GH_SCREEN_AMBIENT) {
        model->screen = GH_SCREEN_EXPLAIN;
    } else if (!lifted && model->screen == GH_SCREEN_EXPLAIN) {
        model->screen = GH_SCREEN_AMBIENT;
    }
    model->generation++;
}

void gh_model_set_magnet(gh_app_model_t *model, bool attached)
{
    if (model == NULL) {
        return;
    }
    model->magnet_attached = attached;
    model->generation++;
}

const char *gh_source_mode_label(gh_source_mode_t mode)
{
    switch (mode) {
    case GH_SOURCE_SIMULATOR: return "SIMULATOR";
    case GH_SOURCE_REAL: return "REAL";
    case GH_SOURCE_REPLAY: return "REPLAY";
    default: return "UNKNOWN";
    }
}

const char *gh_agent_state_label(gh_agent_state_t state)
{
    switch (state) {
    case GH_AGENT_MONITORING: return "MONITORING";
    case GH_AGENT_OPPORTUNITY: return "OPPORTUNITY";
    case GH_AGENT_PENDING_APPROVAL: return "NEEDS CONFIRMATION";
    case GH_AGENT_EXECUTING: return "EXECUTING";
    case GH_AGENT_VERIFIED: return "VERIFIED";
    case GH_AGENT_FAILED: return "FAILED";
    case GH_AGENT_OFFLINE: return "OFFLINE";
    default: return "UNKNOWN";
    }
}
