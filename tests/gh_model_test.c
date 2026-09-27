#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "gh_lift_detector.h"
#include "gh_model.h"
#include "gh_snapshot_parser.h"

static void test_snapshot_simulator_source_is_truthful(void)
{
    static const char snapshot[] =
        "{\"observed_at\":\"2026-09-25T13:35:48Z\",\"energy\":{"
        "\"source\":{\"source_mode\":\"simulator\"},"
        "\"power\":{\"solar_w\":4200,\"home_load_w\":1900,"
        "\"grid_import_w\":0,\"grid_export_w\":2300},"
        "\"storage\":{\"soc_percent\":90}}}";
    cJSON *root = cJSON_Parse(snapshot);
    assert(root != NULL);
    gh_app_model_t model;
    gh_model_init(&model);
    assert(gh_snapshot_apply_energy(root, &model));
    assert(model.energy.source_mode == GH_SOURCE_SIMULATOR);
    assert(model.energy.pv_kw == 4.2f);
    cJSON_Delete(root);
}

static void test_snapshot_unknown_and_replay_sources_are_explicit(void)
{
    static const char replay[] =
        "{\"energy\":{\"source\":{\"source_mode\":\"replay\"},"
        "\"power\":{},\"storage\":{}}}";
    cJSON *root = cJSON_Parse(replay);
    gh_app_model_t model;
    gh_model_init(&model);
    assert(gh_snapshot_apply_energy(root, &model));
    assert(model.energy.source_mode == GH_SOURCE_REPLAY);
    cJSON_Delete(root);

    static const char unknown[] =
        "{\"energy\":{\"source\":{\"source_mode\":\"other\"},"
        "\"power\":{},\"storage\":{}}}";
    root = cJSON_Parse(unknown);
    assert(gh_snapshot_apply_energy(root, &model));
    assert(model.energy.source_mode == GH_SOURCE_UNKNOWN);
    cJSON_Delete(root);
}

static void test_stale_preserves_last_successful_sync(void)
{
    gh_app_model_t model;
    gh_model_init(&model);
    gh_model_mark_snapshot_fresh(&model, 1234567890);
    assert(!model.energy.stale);
    assert(model.energy.last_successful_sync_at == 1234567890);
    gh_model_set_offline(&model);
    assert(model.energy.stale);
    assert(model.energy.last_successful_sync_at == 1234567890);
}

static void configure_pending_backend_proposal(gh_app_model_t *model)
{
    /* Snapshot-shaped fixture mirroring production Action Set #3 as returned by
     * GET /api/v1/companion/snapshot (four PENDING child proposals: ids 36-39,
     * devices anker_solix_123DNMS4567890123 / anker_solix_123DMY64567890123,
     * expected_grid_delta_w 2300). nonce is a TEST ONLY placeholder. */
    static const char snapshot[] =
        "{\"pending_action_set\":{"
        "\"id\":3,\"opportunity_code\":\"SOLAR_SURPLUS_SELF_CONSUMPTION\","
        "\"title\":\"Absorb solar surplus\",\"reason\":\"Meter export detected\","
        "\"status\":\"PENDING\",\"expected_grid_delta_w\":2300,"
        "\"items\":["
        "{\"id\":9,\"sequence\":1,\"device_id\":6,"
        "\"source_device_id\":\"anker_solix_123DNMS4567890123\","
        "\"device_name\":\"Anker SOLIX XE AC\","
        "\"capability\":\"battery_power_direction\","
        "\"current_value\":0,\"target_value\":0,"
        "\"expected_delta_w\":null,\"proposal_id\":36,"
        "\"execution_id\":null,\"status\":\"PENDING\","
        "\"result_code\":null,\"result_message\":null},"
        "{\"id\":10,\"sequence\":2,\"device_id\":6,"
        "\"source_device_id\":\"anker_solix_123DNMS4567890123\","
        "\"device_name\":\"Anker SOLIX XE AC\","
        "\"capability\":\"battery_power_setpoint\","
        "\"current_value\":0,\"target_value\":1200,"
        "\"expected_delta_w\":1200,\"proposal_id\":37,"
        "\"execution_id\":null,\"status\":\"PENDING\","
        "\"result_code\":null,\"result_message\":null},"
        "{\"id\":11,\"sequence\":3,\"device_id\":3,"
        "\"source_device_id\":\"anker_solix_123DMY64567890123\","
        "\"device_name\":\"Anker SOLIX Solarbank Max\","
        "\"capability\":\"battery_power_direction\","
        "\"current_value\":0,\"target_value\":0,"
        "\"expected_delta_w\":null,\"proposal_id\":38,"
        "\"execution_id\":null,\"status\":\"PENDING\","
        "\"result_code\":null,\"result_message\":null},"
        "{\"id\":12,\"sequence\":4,\"device_id\":3,"
        "\"source_device_id\":\"anker_solix_123DMY64567890123\","
        "\"device_name\":\"Anker SOLIX Solarbank Max\","
        "\"capability\":\"battery_power_setpoint\","
        "\"current_value\":0,\"target_value\":1100,"
        "\"expected_delta_w\":1100,\"proposal_id\":39,"
        "\"execution_id\":null,\"status\":\"PENDING\","
        "\"result_code\":null,\"result_message\":null}]},"
        "\"approval_challenge\":{\"action_set_id\":3,"
        "\"nonce\":\"single-use-nonce\","
        "\"action_set_version\":\"3:2026-09-25T14:01:07.186956\","
        "\"expires_at\":\"2030-09-25T14:02:37.000000\"}}";
    gh_model_init(model);
    gh_model_mark_snapshot_fresh(model, 1);
    cJSON *root = cJSON_Parse(snapshot);
    assert(root != NULL);
    assert(gh_snapshot_apply_pending_action_set(root, model, 1));
    cJSON_Delete(root);
    assert(model->action_set_id == 3);
    assert(model->proposal.action_count == 4);
    assert(model->proposal.actions[0].proposal_id == 36);
    assert(model->proposal.actions[1].proposal_id == 37);
    assert(model->proposal.actions[2].proposal_id == 38);
    assert(model->proposal.actions[3].proposal_id == 39);
    assert(strcmp(model->proposal.actions[1].device_name, "Anker SOLIX XE AC") == 0);
    assert(strcmp(model->proposal.actions[3].device_name,
                  "Anker SOLIX Solarbank Max") == 0);
    assert(model->proposal.actions[1].expected_delta_available);
    assert(model->proposal.actions[1].expected_delta_w == 1200.0f);
    assert(!model->proposal.actions[0].expected_delta_available);
    assert(strcmp(model->proposal.proposal_id, "36") == 0);
    assert(strcmp(model->proposal.status, "PENDING") == 0);
    assert(model->proposal.expected_grid_delta_available);
    assert(model->proposal.expected_grid_delta_w == 2300.0f);
    assert(model->approval_nonce[0] != '\0');
    assert(model->action_set_version[0] != '\0');
}

static void test_real_snapshot_shape_enables_approval(void)
{
    gh_app_model_t model;
    configure_pending_backend_proposal(&model);
    assert(gh_model_can_approve(&model));
}

static void test_stale_or_invalid_challenge_disables_approval(void)
{
    gh_app_model_t model;
    configure_pending_backend_proposal(&model);
    model.energy.stale = true;
    assert(!gh_model_can_approve(&model));
    model.energy.stale = false;
    model.approval_nonce[0] = '\0';
    assert(!gh_model_can_approve(&model));
}

static void test_approval_gate_rejects_each_missing_field(void)
{
    gh_app_model_t model;

    /* status not PENDING */
    configure_pending_backend_proposal(&model);
    snprintf(model.proposal.status, sizeof(model.proposal.status), "%s", "APPROVED");
    assert(!gh_model_can_approve(&model));

    /* missing action_set_id */
    configure_pending_backend_proposal(&model);
    model.action_set_id = 0;
    assert(!gh_model_can_approve(&model));

    /* missing action_set_version */
    configure_pending_backend_proposal(&model);
    model.action_set_version[0] = '\0';
    assert(!gh_model_can_approve(&model));

    /* missing proposal_id */
    configure_pending_backend_proposal(&model);
    model.proposal.proposal_id[0] = '\0';
    assert(!gh_model_can_approve(&model));

    /* expired challenge */
    configure_pending_backend_proposal(&model);
    model.proposal.expired = true;
    assert(!gh_model_can_approve(&model));
}

static void test_expired_or_non_pending_snapshot_never_approvable(void)
{
    /* Expired at parse time: a snapshot whose expires_at is already past must
     * land in the model as expired, so the approval gate stays closed. */
    static const char expired_snapshot[] =
        "{\"pending_action_set\":{"
        "\"id\":3,\"title\":\"Absorb solar surplus\",\"status\":\"PENDING\","
        "\"reason\":\"Meter export detected\","
        "\"items\":[{\"id\":9,\"device_name\":\"Anker SOLIX XE AC\","
        "\"capability\":\"battery_power_setpoint\",\"current_value\":0,"
        "\"target_value\":1200,\"proposal_id\":36,\"status\":\"PENDING\"}]},"
        "\"approval_challenge\":{\"action_set_id\":3,"
        "\"nonce\":\"single-use-nonce\",\"action_set_version\":\"v1\","
        "\"expires_at\":\"2020-01-01T00:00:00\"}}";
    gh_app_model_t model;
    gh_model_init(&model);
    gh_model_mark_snapshot_fresh(&model, 1);
    cJSON *root = cJSON_Parse(expired_snapshot);
    assert(root != NULL);
    assert(gh_snapshot_apply_pending_action_set(root, &model, 1700000000));
    assert(model.proposal.expired);
    assert(!gh_model_can_approve(&model));
    cJSON_Delete(root);
}

static void test_backend_proposal_requires_online_state(void)
{
    gh_app_model_t model;
    configure_pending_backend_proposal(&model);
    assert(gh_model_can_approve(&model));
    model.backend_online = false;
    assert(!gh_model_can_approve(&model));
}

static void test_offline_never_approves(void)
{
    gh_app_model_t model;
    configure_pending_backend_proposal(&model);
    gh_model_set_offline(&model);
    assert(model.screen == GH_SCREEN_OFFLINE);
    assert(!gh_model_can_approve(&model));
    assert(!gh_model_begin_approval(&model));
}

static void test_approval_binds_current_pending_proposal(void)
{
    gh_app_model_t model;
    configure_pending_backend_proposal(&model);
    assert(gh_model_begin_approval(&model));
    assert(model.agent_state == GH_AGENT_EXECUTING);
    assert(model.screen == GH_SCREEN_EXECUTION);
    assert(!model.proposal.pending);
    assert(!gh_model_can_approve(&model));
}

static void test_lift_does_not_approve(void)
{
    gh_app_model_t model;
    configure_pending_backend_proposal(&model);
    model.screen = GH_SCREEN_AMBIENT;
    gh_model_set_lifted(&model, true);
    assert(model.screen == GH_SCREEN_PROPOSAL);
    assert(model.proposal.pending);
    assert(model.agent_state == GH_AGENT_PENDING_APPROVAL);
}

static bool feed_imu(gh_lift_detector_t *detector, uint32_t *time_ms,
                     float ax, float ay, float az,
                     float gx, float gy, float gz, int samples)
{
    bool event = false;
    for (int i = 0; i < samples; ++i) {
        *time_ms += 40;
        const gh_imu_sample_t sample = {
            .accel_x = ax, .accel_y = ay, .accel_z = az,
            .gyro_x = gx, .gyro_y = gy, .gyro_z = gz,
            .timestamp_ms = *time_ms, .valid = true,
        };
        if (gh_lift_detector_update(detector, &sample)) event = true;
    }
    return event;
}

static void settle_flat(gh_lift_detector_t *detector, uint32_t *time_ms)
{
    assert(!feed_imu(detector, time_ms, 0.0f, 0.0f, -9.807f,
                     0.0f, 0.0f, 0.0f, 24));
    assert(detector->state == GH_LIFT_STATIONARY);
}

static bool perform_lift(gh_lift_detector_t *detector, uint32_t *time_ms)
{
    bool event = feed_imu(detector, time_ms, 0.0f, -5.0f, -8.0f,
                          75.0f, 4.0f, 0.0f, 4);
    event |= feed_imu(detector, time_ms, 0.0f, -9.1f, -3.0f,
                      0.0f, 0.0f, 0.0f, 20);
    return event;
}

static void test_g4_stationary_and_single_spike_do_not_lift(void)
{
    gh_lift_detector_t detector;
    gh_lift_detector_init(&detector, NULL);
    uint32_t time_ms = 0;
    settle_flat(&detector, &time_ms);
    assert(detector.lift_events == 0);
    assert(!feed_imu(&detector, &time_ms, 0.0f, 0.0f, -9.807f,
                     80.0f, 0.0f, 0.0f, 1));
    assert(!feed_imu(&detector, &time_ms, 0.0f, 0.0f, -9.807f,
                     0.0f, 0.0f, 0.0f, 20));
    assert(detector.lift_events == 0);
}

static void test_g4_full_lift_emits_once_and_holding_is_suppressed(void)
{
    gh_lift_detector_t detector;
    gh_lift_detector_init(&detector, NULL);
    uint32_t time_ms = 0;
    settle_flat(&detector, &time_ms);
    assert(perform_lift(&detector, &time_ms));
    assert(detector.lift_events == 1);
    assert(!feed_imu(&detector, &time_ms, 0.0f, -9.1f, -3.0f,
                     0.0f, 0.0f, 0.0f, 150));
    assert(detector.lift_events == 1);
}

static void test_g4_after_cooldown_new_lift_is_allowed(void)
{
    gh_lift_detector_t detector;
    gh_lift_detector_init(&detector, NULL);
    uint32_t time_ms = 0;
    settle_flat(&detector, &time_ms);
    assert(perform_lift(&detector, &time_ms));
    assert(detector.lift_events == 1);
    /* Put the unit back flat and leave it still through cooldown. */
    assert(!feed_imu(&detector, &time_ms, 0.0f, 0.0f, -9.807f,
                     0.0f, 0.0f, 0.0f, 145));
    if (detector.state != GH_LIFT_STATIONARY) {
        assert(!feed_imu(&detector, &time_ms, 0.0f, 0.0f, -9.807f,
                         0.0f, 0.0f, 0.0f, 24));
    }
    assert(detector.state == GH_LIFT_STATIONARY);
    assert(perform_lift(&detector, &time_ms));
    assert(detector.lift_events == 2);
}

static void test_g4_invalid_imu_never_fakes_lift(void)
{
    gh_lift_detector_t detector;
    gh_lift_detector_init(&detector, NULL);
    const gh_imu_sample_t invalid = {.valid = false};
    for (int i = 0; i < 100; ++i) {
        assert(!gh_lift_detector_update(&detector, &invalid));
    }
    assert(detector.lift_events == 0);
    assert(detector.read_failures == 100);
}

static void test_magnet_does_not_approve(void)
{
    gh_app_model_t model;
    configure_pending_backend_proposal(&model);
    gh_model_set_magnet(&model, true);
    assert(model.magnet_attached);
    assert(model.proposal.pending);
    assert(model.agent_state == GH_AGENT_PENDING_APPROVAL);
}

static void test_g5_voice_approval_intent_only_reveals_proposal(void)
{
    gh_app_model_t model;
    configure_pending_backend_proposal(&model);
    model.screen = GH_SCREEN_AMBIENT;
    gh_model_voice_begin(&model);
    assert(model.screen == GH_SCREEN_VOICE_LISTENING);
    assert(model.voice_active);
    gh_model_voice_transcribing(&model);
    assert(model.screen == GH_SCREEN_VOICE_TRANSCRIBING);
    gh_model_voice_complete(&model, "执行刚才那个方案", "请长按确认",
                            "APPROVAL_INTENT", true);
    assert(model.screen == GH_SCREEN_PROPOSAL);
    assert(model.proposal.pending);
    assert(model.agent_state == GH_AGENT_PENDING_APPROVAL);
    assert(!model.voice_active);
}

static void test_g5_voice_failure_never_creates_proposal(void)
{
    gh_app_model_t model;
    gh_model_init(&model);
    gh_model_mark_snapshot_fresh(&model, 1);
    model.screen = GH_SCREEN_AMBIENT;
    gh_model_voice_begin(&model);
    gh_model_voice_fail(&model, "没听清，请再说一次。");
    assert(model.screen == GH_SCREEN_VOICE_RESULT);
    assert(model.voice_state == GH_VOICE_FAILED);
    assert(!model.proposal.pending);
    assert(!gh_model_can_approve(&model));
}

static void test_g6_execution_and_l123_verification_come_from_backend(void)
{
    static const char snapshot[] =
        "{\"current_action_set\":{\"id\":7,\"title\":\"协同削峰\","
        "\"reason\":\"总表检测到峰值\",\"status\":\"SUCCEEDED\","
        "\"verification_status\":\"VERIFIED\","
        "\"verification_message\":\"Smart Meter 已验证\","
        "\"before_grid_power_w\":7100,\"after_grid_power_w\":3600,"
        "\"actual_grid_delta_w\":3500,\"items\":[{"
        "\"device_name\":\"Anker SOLIX Max AC\","
        "\"capability\":\"battery_power_setpoint\","
        "\"current_value\":0,\"target_value\":2500,"
        "\"proposal_id\":71,\"execution_id\":81,"
        "\"status\":\"SUCCEEDED\",\"result_code\":\"READBACK_VERIFIED\","
        "\"result_message\":\"设备写入与状态回读一致\"}]}}";
    gh_app_model_t model;
    gh_model_init(&model);
    cJSON *root = cJSON_Parse(snapshot);
    assert(root != NULL);
    assert(gh_snapshot_apply_current_action_set(root, &model));
    assert(model.action_set_id == 7);
    assert(model.screen == GH_SCREEN_VERIFICATION);
    assert(model.agent_state == GH_AGENT_VERIFIED);
    assert(model.proposal.action_count == 1);
    assert(model.proposal.actions[0].execution_id == 81);
    assert(strcmp(model.proposal.actions[0].result_code,
                  "READBACK_VERIFIED") == 0);
    assert(strcmp(model.proposal.verification_status, "VERIFIED") == 0);
    assert(model.proposal.before_grid_power_w == 7100.0f);
    assert(model.proposal.after_grid_power_w == 3600.0f);
    gh_model_open_screen(&model, GH_SCREEN_AMBIENT);
    assert(model.acknowledged_action_set_id == 7);
    assert(gh_snapshot_apply_current_action_set(root, &model));
    assert(model.screen == GH_SCREEN_AMBIENT);
    cJSON_Delete(root);
}

static void test_g9_partial_and_unavailable_verification_remain_truthful(void)
{
    static const char partial_snapshot[] =
        "{\"current_action_set\":{\"id\":9,\"title\":\"协同削峰\"," 
        "\"reason\":\"总表检测到峰值\",\"status\":\"PARTIAL\"," 
        "\"verification_status\":\"NOT_VERIFIED\"," 
        "\"verification_message\":\"家庭能源效果未验证\"," 
        "\"before_grid_power_w\":7100,\"after_grid_power_w\":5200,"
        "\"items\":[{\"device_name\":\"Solarbank 4\"," 
        "\"capability\":\"battery_power_setpoint\",\"current_value\":0,"
        "\"target_value\":1800,\"proposal_id\":91,\"execution_id\":101,"
        "\"status\":\"SUCCEEDED\",\"result_code\":\"READBACK_VERIFIED\"},"
        "{\"device_name\":\"Smart Plug Gen 2\"," 
        "\"capability\":\"switch\",\"current_value\":1,\"target_value\":0,"
        "\"proposal_id\":92,\"execution_id\":102,\"status\":\"BLOCKED\","
        "\"result_code\":\"SAFETY_BLOCKED\"}]}}";
    gh_app_model_t model;
    gh_model_init(&model);
    cJSON *root = cJSON_Parse(partial_snapshot);
    assert(root != NULL);
    assert(gh_snapshot_apply_current_action_set(root, &model));
    assert(model.screen == GH_SCREEN_VERIFICATION);
    assert(model.agent_state == GH_AGENT_FAILED);
    assert(strcmp(model.proposal.status, "PARTIAL") == 0);
    assert(strcmp(model.proposal.verification_status, "NOT_VERIFIED") == 0);
    assert(model.proposal.action_count == 2);
    assert(strcmp(model.proposal.actions[0].result_code,
                  "READBACK_VERIFIED") == 0);
    assert(strcmp(model.proposal.actions[1].status, "BLOCKED") == 0);
    cJSON_Delete(root);

    static const char unavailable_snapshot[] =
        "{\"current_action_set\":{\"id\":10,\"title\":\"储能调整\"," 
        "\"reason\":\"降低购电峰值\",\"status\":\"PARTIAL\"," 
        "\"verification_status\":\"UNAVAILABLE\"," 
        "\"verification_message\":\"验证时窗内未取得总表数据\"," 
        "\"items\":[{\"device_name\":\"Solarbank 4\"," 
        "\"capability\":\"backup_reserve\",\"current_value\":30,"
        "\"target_value\":50,\"proposal_id\":93,\"execution_id\":103,"
        "\"status\":\"SUCCEEDED\",\"result_code\":\"READBACK_VERIFIED\"}]}}";
    gh_model_init(&model);
    root = cJSON_Parse(unavailable_snapshot);
    assert(root != NULL);
    assert(gh_snapshot_apply_current_action_set(root, &model));
    assert(model.screen == GH_SCREEN_VERIFICATION);
    assert(model.agent_state == GH_AGENT_FAILED);
    assert(strcmp(model.proposal.verification_status, "UNAVAILABLE") == 0);
    assert(!model.proposal.before_grid_available);
    assert(!model.proposal.after_grid_available);
    cJSON_Delete(root);
}

static void test_g3_screen_router_lifecycle(void)
{
    gh_app_model_t model;
    gh_model_init(&model);
    assert(model.screen == GH_SCREEN_BOOT);

    model.connection_state = GH_CONNECTION_CONNECTING;
    gh_model_route_runtime(&model);
    assert(model.screen == GH_SCREEN_WIFI_CONNECTING);

    model.connection_state = GH_CONNECTION_TIME_SYNCING;
    gh_model_route_runtime(&model);
    assert(model.screen == GH_SCREEN_TIME_SYNC);

    model.connection_state = GH_CONNECTION_BACKEND_CONNECTING;
    gh_model_route_runtime(&model);
    assert(model.screen == GH_SCREEN_BACKEND_CONNECTING);

    snprintf(model.pairing_code, sizeof(model.pairing_code), "%s", "123456");
    gh_model_route_runtime(&model);
    assert(model.screen == GH_SCREEN_PAIRING);

    model.pairing_code[0] = '\0';
    model.connection_state = GH_CONNECTION_SECURE_READY;
    gh_model_mark_snapshot_fresh(&model, 10);
    gh_model_route_runtime(&model);
    assert(model.screen == GH_SCREEN_AMBIENT);
}

static void test_g3_pending_approval_and_user_navigation(void)
{
    gh_app_model_t model;
    configure_pending_backend_proposal(&model);
    model.connection_state = GH_CONNECTION_SECURE_READY;
    gh_model_route_runtime(&model);
    assert(model.screen == GH_SCREEN_PROPOSAL);
    gh_model_open_screen(&model, GH_SCREEN_APPROVAL);
    gh_model_route_runtime(&model);
    assert(model.screen == GH_SCREEN_APPROVAL);
}

static void test_g3_offline_and_reconnect_route(void)
{
    gh_app_model_t model;
    gh_model_init(&model);
    gh_model_set_offline(&model);
    assert(model.screen == GH_SCREEN_OFFLINE);
    model.connection_state = GH_CONNECTION_SECURE_READY;
    gh_model_mark_snapshot_fresh(&model, 20);
    gh_model_route_runtime(&model);
    assert(model.screen == GH_SCREEN_AMBIENT);
    gh_model_open_screen(&model, GH_SCREEN_DEVICE_STATUS);
    gh_model_route_runtime(&model);
    assert(model.screen == GH_SCREEN_DEVICE_STATUS);
}

int main(void)
{
    test_snapshot_simulator_source_is_truthful();
    test_snapshot_unknown_and_replay_sources_are_explicit();
    test_stale_preserves_last_successful_sync();
    test_real_snapshot_shape_enables_approval();
    test_stale_or_invalid_challenge_disables_approval();
    test_approval_gate_rejects_each_missing_field();
    test_expired_or_non_pending_snapshot_never_approvable();
    test_backend_proposal_requires_online_state();
    test_offline_never_approves();
    test_approval_binds_current_pending_proposal();
    test_lift_does_not_approve();
    test_magnet_does_not_approve();
    test_g5_voice_approval_intent_only_reveals_proposal();
    test_g5_voice_failure_never_creates_proposal();
    test_g6_execution_and_l123_verification_come_from_backend();
    test_g9_partial_and_unavailable_verification_remain_truthful();
    test_g3_screen_router_lifecycle();
    test_g3_pending_approval_and_user_navigation();
    test_g3_offline_and_reconnect_route();
    test_g4_stationary_and_single_spike_do_not_lift();
    test_g4_full_lift_emits_once_and_holding_is_suppressed();
    test_g4_after_cooldown_new_lift_is_allowed();
    test_g4_invalid_imu_never_fakes_lift();
    puts("gh_model_tests: all tests passed");
    return 0;
}
