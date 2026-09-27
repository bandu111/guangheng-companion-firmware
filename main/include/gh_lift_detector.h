#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    GH_LIFT_IDLE = 0,
    GH_LIFT_STATIONARY,
    GH_LIFT_MOVING,
    GH_LIFT_CANDIDATE,
    GH_LIFT_READABLE,
    GH_LIFT_COOLDOWN,
} gh_lift_state_t;

typedef struct {
    float accel_x;
    float accel_y;
    float accel_z;
    float gyro_x;
    float gyro_y;
    float gyro_z;
    uint32_t timestamp_ms;
    bool valid;
} gh_imu_sample_t;

typedef struct {
    float gravity_mps2;
    float stationary_accel_tolerance_mps2;
    float move_accel_delta_mps2;
    float stationary_gyro_dps;
    float move_gyro_dps;
    float readable_angle_min_deg;
    float readable_angle_max_deg;
    float filter_alpha;
    uint32_t stationary_ms;
    uint32_t readable_stable_ms;
    uint32_t movement_timeout_ms;
    uint32_t cooldown_ms;
} gh_lift_config_t;

typedef struct {
    gh_lift_config_t config;
    gh_lift_state_t state;
    float filtered_ax;
    float filtered_ay;
    float filtered_az;
    float filtered_gx;
    float filtered_gy;
    float filtered_gz;
    float baseline_ax;
    float baseline_ay;
    float baseline_az;
    float orientation_delta_deg;
    uint32_t state_since_ms;
    uint32_t stationary_since_ms;
    uint32_t candidate_since_ms;
    uint32_t cooldown_until_ms;
    bool filter_ready;
    bool baseline_ready;
    uint32_t read_failures;
    uint32_t lift_events;
} gh_lift_detector_t;

gh_lift_config_t gh_lift_default_config(void);
void gh_lift_detector_init(gh_lift_detector_t *detector,
                           const gh_lift_config_t *config);
bool gh_lift_detector_update(gh_lift_detector_t *detector,
                             const gh_imu_sample_t *sample);
const char *gh_lift_state_name(gh_lift_state_t state);
