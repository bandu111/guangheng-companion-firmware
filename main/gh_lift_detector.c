#include "gh_lift_detector.h"

#include <math.h>
#include <stddef.h>

#define GH_RAD_TO_DEG 57.2957795f

static float magnitude3(float x, float y, float z)
{
    return sqrtf(x * x + y * y + z * z);
}

static float absolute(float value)
{
    return value < 0.0f ? -value : value;
}

static void enter_state(gh_lift_detector_t *detector, gh_lift_state_t state,
                        uint32_t now_ms)
{
    detector->state = state;
    detector->state_since_ms = now_ms;
}

gh_lift_config_t gh_lift_default_config(void)
{
    return (gh_lift_config_t) {
        .gravity_mps2 = 9.807f,
        .stationary_accel_tolerance_mps2 = 1.25f,
        .move_accel_delta_mps2 = 1.80f,
        .stationary_gyro_dps = 12.0f,
        .move_gyro_dps = 32.0f,
        .readable_angle_min_deg = 24.0f,
        .readable_angle_max_deg = 135.0f,
        .filter_alpha = 0.24f,
        .stationary_ms = 720,
        .readable_stable_ms = 280,
        .movement_timeout_ms = 2800,
        .cooldown_ms = 4000,
    };
}

void gh_lift_detector_init(gh_lift_detector_t *detector,
                           const gh_lift_config_t *config)
{
    if (detector == NULL) return;
    *detector = (gh_lift_detector_t) {0};
    detector->config = config != NULL ? *config : gh_lift_default_config();
    detector->state = GH_LIFT_IDLE;
}

static float orientation_delta_deg(const gh_lift_detector_t *detector)
{
    const float baseline_mag = magnitude3(detector->baseline_ax,
                                          detector->baseline_ay,
                                          detector->baseline_az);
    const float current_mag = magnitude3(detector->filtered_ax,
                                         detector->filtered_ay,
                                         detector->filtered_az);
    if (baseline_mag < 0.1f || current_mag < 0.1f) return 0.0f;
    float cosine = (detector->baseline_ax * detector->filtered_ax +
                    detector->baseline_ay * detector->filtered_ay +
                    detector->baseline_az * detector->filtered_az) /
                   (baseline_mag * current_mag);
    if (cosine > 1.0f) cosine = 1.0f;
    if (cosine < -1.0f) cosine = -1.0f;
    return acosf(cosine) * GH_RAD_TO_DEG;
}

bool gh_lift_detector_update(gh_lift_detector_t *detector,
                             const gh_imu_sample_t *sample)
{
    if (detector == NULL || sample == NULL || !sample->valid) {
        if (detector != NULL) detector->read_failures++;
        return false;
    }

    const float alpha = detector->config.filter_alpha;
    if (!detector->filter_ready) {
        detector->filtered_ax = sample->accel_x;
        detector->filtered_ay = sample->accel_y;
        detector->filtered_az = sample->accel_z;
        detector->filtered_gx = sample->gyro_x;
        detector->filtered_gy = sample->gyro_y;
        detector->filtered_gz = sample->gyro_z;
        detector->filter_ready = true;
        detector->stationary_since_ms = sample->timestamp_ms;
    } else {
        detector->filtered_ax += alpha * (sample->accel_x - detector->filtered_ax);
        detector->filtered_ay += alpha * (sample->accel_y - detector->filtered_ay);
        detector->filtered_az += alpha * (sample->accel_z - detector->filtered_az);
        detector->filtered_gx += alpha * (sample->gyro_x - detector->filtered_gx);
        detector->filtered_gy += alpha * (sample->gyro_y - detector->filtered_gy);
        detector->filtered_gz += alpha * (sample->gyro_z - detector->filtered_gz);
    }

    const uint32_t now = sample->timestamp_ms;
    const float accel_mag = magnitude3(detector->filtered_ax,
                                       detector->filtered_ay,
                                       detector->filtered_az);
    const float gyro_mag = magnitude3(detector->filtered_gx,
                                      detector->filtered_gy,
                                      detector->filtered_gz);
    const bool gravity_stable =
        absolute(accel_mag - detector->config.gravity_mps2) <=
        detector->config.stationary_accel_tolerance_mps2;
    const bool low_rotation = gyro_mag <= detector->config.stationary_gyro_dps;
    const bool moving =
        absolute(accel_mag - detector->config.gravity_mps2) >=
            detector->config.move_accel_delta_mps2 ||
        gyro_mag >= detector->config.move_gyro_dps;
    detector->orientation_delta_deg = detector->baseline_ready
                                          ? orientation_delta_deg(detector)
                                          : 0.0f;

    if (detector->state == GH_LIFT_COOLDOWN) {
        if ((int32_t)(now - detector->cooldown_until_ms) < 0) return false;
        enter_state(detector, GH_LIFT_IDLE, now);
        detector->stationary_since_ms = now;
        detector->baseline_ready = false;
    }

    switch (detector->state) {
    case GH_LIFT_IDLE:
        if (gravity_stable && low_rotation) {
            if (now - detector->stationary_since_ms >= detector->config.stationary_ms) {
                detector->baseline_ax = detector->filtered_ax;
                detector->baseline_ay = detector->filtered_ay;
                detector->baseline_az = detector->filtered_az;
                detector->baseline_ready = true;
                enter_state(detector, GH_LIFT_STATIONARY, now);
            }
        } else {
            detector->stationary_since_ms = now;
        }
        break;

    case GH_LIFT_STATIONARY:
        if (moving) enter_state(detector, GH_LIFT_MOVING, now);
        break;

    case GH_LIFT_MOVING: {
        if (now - detector->state_since_ms > detector->config.movement_timeout_ms) {
            enter_state(detector, GH_LIFT_IDLE, now);
            detector->stationary_since_ms = now;
            detector->baseline_ready = false;
            break;
        }
        const float angle = detector->orientation_delta_deg;
        if (detector->baseline_ready &&
            angle >= detector->config.readable_angle_min_deg &&
            angle <= detector->config.readable_angle_max_deg &&
            gravity_stable && gyro_mag < detector->config.move_gyro_dps) {
            detector->candidate_since_ms = now;
            enter_state(detector, GH_LIFT_CANDIDATE, now);
        }
        break;
    }

    case GH_LIFT_CANDIDATE: {
        const float angle = detector->orientation_delta_deg;
        const bool readable = angle >= detector->config.readable_angle_min_deg &&
                              angle <= detector->config.readable_angle_max_deg &&
                              gravity_stable && low_rotation;
        if (!readable) {
            if (moving) enter_state(detector, GH_LIFT_MOVING, now);
            else detector->candidate_since_ms = now;
            break;
        }
        if (now - detector->candidate_since_ms >=
            detector->config.readable_stable_ms) {
            enter_state(detector, GH_LIFT_READABLE, now);
        }
        break;
    }

    case GH_LIFT_READABLE:
        detector->lift_events++;
        detector->cooldown_until_ms = now + detector->config.cooldown_ms;
        enter_state(detector, GH_LIFT_COOLDOWN, now);
        return true;

    case GH_LIFT_COOLDOWN:
        break;
    }
    return false;
}

const char *gh_lift_state_name(gh_lift_state_t state)
{
    switch (state) {
    case GH_LIFT_IDLE: return "IDLE";
    case GH_LIFT_STATIONARY: return "STATIONARY";
    case GH_LIFT_MOVING: return "MOVING";
    case GH_LIFT_CANDIDATE: return "LIFT_CANDIDATE";
    case GH_LIFT_READABLE: return "READABLE";
    case GH_LIFT_COOLDOWN: return "COOLDOWN";
    default: return "UNKNOWN";
    }
}
