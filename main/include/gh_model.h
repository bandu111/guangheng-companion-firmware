#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    GH_SOURCE_UNKNOWN = 0,
    GH_SOURCE_UNAVAILABLE = GH_SOURCE_UNKNOWN,
    GH_SOURCE_SIMULATOR,
    GH_SOURCE_REAL,
    GH_SOURCE_REPLAY,
} gh_source_mode_t;

typedef enum {
    GH_AGENT_MONITORING = 0,
    GH_AGENT_OPPORTUNITY,
    GH_AGENT_PENDING_APPROVAL,
    GH_AGENT_EXECUTING,
    GH_AGENT_VERIFIED,
    GH_AGENT_FAILED,
    GH_AGENT_OFFLINE,
} gh_agent_state_t;

typedef enum {
    GH_SCREEN_BOOT = 0,
    GH_SCREEN_WIFI_CONNECTING,
    GH_SCREEN_TIME_SYNC,
    GH_SCREEN_BACKEND_CONNECTING,
    GH_SCREEN_PAIRING,
    GH_SCREEN_AMBIENT,
    GH_SCREEN_VOICE_LISTENING,
    GH_SCREEN_VOICE_TRANSCRIBING,
    GH_SCREEN_VOICE_RESULT,
    GH_SCREEN_EXPLAIN,
    GH_SCREEN_PROPOSAL,
    GH_SCREEN_APPROVAL,
    GH_SCREEN_EXECUTION,
    GH_SCREEN_VERIFICATION,
    GH_SCREEN_OFFLINE,
    GH_SCREEN_DEVICE_STATUS,
    GH_SCREEN_COUNT,
} gh_screen_t;

typedef enum {
    GH_CONNECTION_OFFLINE = 0,
    GH_CONNECTION_CONNECTING,
    GH_CONNECTION_TIME_SYNCING,
    GH_CONNECTION_BACKEND_CONNECTING,
    GH_CONNECTION_SECURE_READY,
} gh_connection_state_t;

typedef enum {
    GH_VOICE_IDLE = 0,
    GH_VOICE_LISTENING,
    GH_VOICE_TRANSCRIBING,
    GH_VOICE_COMPLETED,
    GH_VOICE_FAILED,
} gh_voice_state_t;

typedef struct {
    float pv_kw;
    float home_kw;
    float grid_kw;
    float battery_soc_pct;
    float companion_battery_pct;
    float companion_battery_voltage_v;
    bool companion_battery_present;
    bool companion_battery_pct_available;
    bool companion_battery_voltage_available;
    bool companion_battery_charging;
    bool grid_importing;
    bool battery_charging;
    gh_source_mode_t source_mode;
    int64_t observed_at_ms;
    int64_t last_successful_sync_at;
    bool stale;
} gh_energy_snapshot_t;

#define GH_MAX_ACTIONS 4

typedef struct {
    int proposal_id;
    int execution_id;
    char source_device_id[64];
    char device_name[40];
    char capability[48];
    char action[64];
    char status[24];
    char result_code[64];
    char result_message[96];
    float current_value;
    float target_value;
    float expected_delta_w;
    bool expected_delta_available;
} gh_action_step_t;

typedef struct {
    char proposal_id[40];
    uint32_t revision;
    char title[64];
    char status[24];
    char reason[128];
    char expected_impact[96];
    char do_nothing[96];
    char expires_at[40];
    int64_t expires_at_epoch;
    float expected_grid_delta_w;
    bool expected_grid_delta_available;
    gh_action_step_t actions[GH_MAX_ACTIONS];
    size_t action_count;
    bool pending;
    bool expired;
    char verification_status[24];
    char verification_message[160];
    float before_grid_power_w;
    float after_grid_power_w;
    float actual_grid_delta_w;
    bool before_grid_available;
    bool after_grid_available;
    bool actual_grid_delta_available;
} gh_proposal_t;

typedef struct {
    gh_energy_snapshot_t energy;
    gh_proposal_t proposal;
    gh_agent_state_t agent_state;
    gh_screen_t screen;
    bool backend_online;
    bool websocket_online;
    bool lifted;
    bool magnet_attached;
    bool voice_active;
    gh_voice_state_t voice_state;
    uint16_t voice_rms;
    uint16_t voice_peak;
    bool voice_speech;
    char voice_transcript[160];
    char voice_answer[320];
    char voice_intent[40];
    char voice_error[80];
    bool voice_requires_touch_approval;
    gh_connection_state_t connection_state;
    uint32_t generation;
    char pairing_code[8];
    char approval_nonce[96];
    char action_set_version[64];
    int action_set_id;
    int acknowledged_action_set_id;
} gh_app_model_t;

void gh_model_init(gh_app_model_t *model);
bool gh_model_can_approve(const gh_app_model_t *model);
bool gh_model_begin_approval(gh_app_model_t *model);
void gh_model_cancel_approval(gh_app_model_t *model);
void gh_model_set_offline(gh_app_model_t *model);
void gh_model_mark_snapshot_fresh(gh_app_model_t *model, int64_t synchronized_at);
void gh_model_set_lifted(gh_app_model_t *model, bool lifted);
void gh_model_set_magnet(gh_app_model_t *model, bool attached);
void gh_model_route_runtime(gh_app_model_t *model);
void gh_model_open_screen(gh_app_model_t *model, gh_screen_t screen);
void gh_model_voice_begin(gh_app_model_t *model);
void gh_model_voice_level(gh_app_model_t *model, uint16_t rms, uint16_t peak,
                          bool speech);
void gh_model_voice_transcribing(gh_app_model_t *model);
void gh_model_voice_complete(gh_app_model_t *model, const char *transcript,
                             const char *answer, const char *intent,
                             bool requires_touch_approval);
void gh_model_voice_fail(gh_app_model_t *model, const char *message);
const char *gh_source_mode_label(gh_source_mode_t mode);
const char *gh_agent_state_label(gh_agent_state_t state);
