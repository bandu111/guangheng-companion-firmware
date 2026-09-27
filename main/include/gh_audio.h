#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize the ESP32-S3 ES8311/I2S runtime without producing boot audio. */
esp_err_t gh_audio_init(void);

/** Run the optional ESP32-S3 board microphone and speaker manufacturing HIL. */
esp_err_t gh_audio_run_hil(void);

typedef struct {
    uint8_t *wav_data;
    size_t wav_size;
    uint32_t duration_ms;
    uint32_t quiet_rms;
    uint32_t speech_rms;
    uint32_t peak;
    uint32_t overflow_count;
    uint32_t underflow_count;
    bool speech_detected;
} gh_voice_capture_t;

typedef void (*gh_audio_level_cb_t)(uint16_t rms, uint16_t peak,
                                    bool speech, void *context);

/** Blocking voice capture. Call from a worker task, never the LVGL task. */
esp_err_t gh_audio_capture_voice(gh_voice_capture_t *capture,
                                 gh_audio_level_cb_t level_cb,
                                 void *context);
void gh_audio_request_stop(void);
void gh_audio_capture_free(gh_voice_capture_t *capture);

#ifdef __cplusplus
}
#endif
