#include "gh_audio.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"

static const char *TAG = "gh_audio";

#define GH_AUDIO_SAMPLE_RATE 16000
#define GH_AUDIO_CHANNELS 1
#define GH_AUDIO_BITS_PER_SAMPLE 16
#define GH_AUDIO_FRAME_SAMPLES 256
#define GH_AUDIO_TEST_FRAMES 63
#define GH_AUDIO_TONE_HZ 880
#define GH_AUDIO_TONE_AMPLITUDE 9000
#define GH_AUDIO_SPEAKER_VOLUME 45
#define GH_AUDIO_MIC_GAIN_DB 30.0f
#define GH_AUDIO_TWO_PI 6.28318530717958647692f
#define GH_AUDIO_MIN_CAPTURE_SPAN 32
#define GH_VOICE_MAX_SECONDS 8
#define GH_WAV_HEADER_BYTES 44
#define GH_VOICE_LEVEL_REPORT_FRAMES 5

static esp_codec_dev_handle_t s_codec;
static volatile bool s_voice_stop_requested;

typedef struct {
    esp_codec_dev_handle_t codec;
    TaskHandle_t waiter;
    size_t bytes_written;
    int error;
} speaker_test_context_t;

static esp_err_t codec_result(int result, const char *operation)
{
    if (result == ESP_CODEC_DEV_OK) return ESP_OK;
    ESP_LOGE(TAG, "%s failed: %d", operation, result);
    return ESP_FAIL;
}

static esp_codec_dev_sample_info_t sample_info(void)
{
    const esp_codec_dev_sample_info_t info = {
        .bits_per_sample = GH_AUDIO_BITS_PER_SAMPLE,
        .channel = GH_AUDIO_CHANNELS,
        .channel_mask = 0,
        .sample_rate = GH_AUDIO_SAMPLE_RATE,
        .mclk_multiple = 256,
    };
    return info;
}

static esp_err_t create_codec(esp_codec_dev_handle_t *codec)
{
    i2s_chan_handle_t tx = NULL;
    i2s_chan_handle_t rx = NULL;
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
    channel_config.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, &tx, &rx), TAG,
                        "create I2S channels");

    const i2s_std_config_t i2s_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(GH_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din = BSP_I2S_DSIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &i2s_config), TAG,
                        "initialize I2S TX");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx, &i2s_config), TAG,
                        "initialize I2S RX");

    audio_codec_i2s_cfg_t data_config = {
        .port = CONFIG_BSP_I2S_NUM,
        .rx_handle = rx,
        .tx_handle = tx,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&data_config);
    ESP_RETURN_ON_FALSE(data_if != NULL, ESP_ERR_NO_MEM, TAG,
                        "create codec I2S interface");

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(gpio_if != NULL, ESP_ERR_NO_MEM, TAG,
                        "create codec GPIO interface");
    audio_codec_i2c_cfg_t control_config = {
        .port = BSP_I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = bsp_i2c_get_handle(),
    };
    const audio_codec_ctrl_if_t *control_if =
        audio_codec_new_i2c_ctrl(&control_config);
    ESP_RETURN_ON_FALSE(control_if != NULL, ESP_ERR_NO_MEM, TAG,
                        "create codec I2C interface");

    const esp_codec_dev_hw_gain_t hardware_gain = {
        .pa_voltage = 5.0,
        .codec_dac_voltage = 3.3,
    };
    es8311_codec_cfg_t es8311_config = {
        .ctrl_if = control_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = BSP_POWER_AMP_IO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = hardware_gain,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_config);
    ESP_RETURN_ON_FALSE(codec_if != NULL, ESP_ERR_NO_MEM, TAG,
                        "create ES8311 codec");

    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    *codec = esp_codec_dev_new(&device_config);
    ESP_RETURN_ON_FALSE(*codec != NULL, ESP_ERR_NO_MEM, TAG,
                        "create ES8311 device");

    esp_codec_dev_sample_info_t info = sample_info();
    ESP_RETURN_ON_ERROR(codec_result(esp_codec_dev_open(*codec, &info),
                                     "open ES8311"),
                        TAG, "open ES8311");
    ESP_RETURN_ON_ERROR(codec_result(
                            esp_codec_dev_set_out_vol(*codec,
                                                      GH_AUDIO_SPEAKER_VOLUME),
                            "set speaker volume"),
                        TAG, "set speaker volume");
    ESP_RETURN_ON_ERROR(codec_result(
                            esp_codec_dev_set_in_gain(*codec,
                                                     GH_AUDIO_MIC_GAIN_DB),
                            "set microphone gain"),
                        TAG, "set microphone gain");

    ESP_LOGI(TAG,
             "ESP32-S3 Hardware-in-the-loop | ESP32-S3 ES8311 Codec | PASS | "
             "address=0x%02X sample_rate=%d mode=full-duplex",
             ES8311_CODEC_DEFAULT_ADDR, GH_AUDIO_SAMPLE_RATE);
    return ESP_OK;
}

static void speaker_test_task(void *argument)
{
    speaker_test_context_t *context = argument;
    int16_t samples[GH_AUDIO_FRAME_SAMPLES];
    float phase = 0.0f;
    const float phase_step =
        GH_AUDIO_TWO_PI * GH_AUDIO_TONE_HZ / GH_AUDIO_SAMPLE_RATE;

    for (int frame = 0; frame < GH_AUDIO_TEST_FRAMES; ++frame) {
        for (size_t index = 0; index < GH_AUDIO_FRAME_SAMPLES; ++index) {
            samples[index] =
                (int16_t)(sinf(phase) * GH_AUDIO_TONE_AMPLITUDE);
            phase += phase_step;
            if (phase >= GH_AUDIO_TWO_PI) phase -= GH_AUDIO_TWO_PI;
        }
        const int result =
            esp_codec_dev_write(context->codec, samples, sizeof(samples));
        if (result != ESP_CODEC_DEV_OK) {
            context->error = result;
            break;
        }
        context->bytes_written += sizeof(samples);
    }
    xTaskNotifyGive(context->waiter);
    vTaskDelete(NULL);
}

static esp_err_t capture_microphone(esp_codec_dev_handle_t codec,
                                    size_t *sample_count,
                                    int16_t *minimum, int16_t *maximum,
                                    uint64_t *absolute_sum)
{
    int16_t samples[GH_AUDIO_FRAME_SAMPLES];
    *sample_count = 0;
    *minimum = INT16_MAX;
    *maximum = INT16_MIN;
    *absolute_sum = 0;

    for (int frame = 0; frame < GH_AUDIO_TEST_FRAMES; ++frame) {
        const int result = esp_codec_dev_read(codec, samples, sizeof(samples));
        if (result != ESP_CODEC_DEV_OK) return ESP_FAIL;
        for (size_t index = 0; index < GH_AUDIO_FRAME_SAMPLES; ++index) {
            const int32_t value = samples[index];
            if (value < *minimum) *minimum = (int16_t)value;
            if (value > *maximum) *maximum = (int16_t)value;
            *absolute_sum += value < 0 ? (uint32_t)-value : (uint32_t)value;
        }
        *sample_count += GH_AUDIO_FRAME_SAMPLES;
    }
    return ESP_OK;
}

esp_err_t gh_audio_run_hil(void)
{
    esp_err_t ret = gh_audio_init();
    if (ret != ESP_OK) return ret;

    speaker_test_context_t speaker = {
        .codec = s_codec,
        .waiter = xTaskGetCurrentTaskHandle(),
        .bytes_written = 0,
        .error = ESP_CODEC_DEV_OK,
    };
    BaseType_t created = xTaskCreate(speaker_test_task, "speaker_hil", 4096,
                                     &speaker, 5, NULL);
    ESP_RETURN_ON_FALSE(created == pdPASS, ESP_ERR_NO_MEM, TAG,
                        "create speaker HIL task");

    size_t sample_count = 0;
    int16_t minimum = 0;
    int16_t maximum = 0;
    uint64_t absolute_sum = 0;
    ret = capture_microphone(s_codec, &sample_count, &minimum, &maximum,
                             &absolute_sum);
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(3000)) == 0) {
        ESP_LOGE(TAG, "ESP32-S3 speaker HIL timed out");
        return ESP_ERR_TIMEOUT;
    }
    if (ret != ESP_OK || speaker.error != ESP_CODEC_DEV_OK) return ESP_FAIL;

    const int32_t span = (int32_t)maximum - (int32_t)minimum;
    const uint32_t mean_absolute = sample_count == 0
                                       ? 0
                                       : (uint32_t)(absolute_sum / sample_count);
    if (sample_count == 0 || span < GH_AUDIO_MIN_CAPTURE_SPAN) {
        ESP_LOGE(TAG,
                 "ESP32-S3 microphone capture lacks real signal variation: "
                 "samples=%u min=%d max=%d span=%" PRId32,
                 (unsigned)sample_count, minimum, maximum, span);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG,
             "ESP32-S3 Hardware-in-the-loop | ESP32-S3 Board Microphone Capture "
             "| PASS | samples=%u min=%d max=%d span=%" PRId32
             " mean_absolute=%" PRIu32,
             (unsigned)sample_count, minimum, maximum, span, mean_absolute);
    ESP_LOGI(TAG,
             "ESP32-S3 Hardware-in-the-loop | ESP32-S3 Board Speaker Stream | "
             "PASS | frequency=%dHz volume=%d bytes=%u audible_confirmation=pending",
             GH_AUDIO_TONE_HZ, GH_AUDIO_SPEAKER_VOLUME,
             (unsigned)speaker.bytes_written);
    return ESP_OK;
}

esp_err_t gh_audio_init(void)
{
    if (s_codec != NULL) return ESP_OK;
    return create_codec(&s_codec);
}

static uint16_t frame_level(const int16_t *samples, size_t count,
                            uint16_t *peak)
{
    uint64_t square_sum = 0;
    uint32_t local_peak = 0;
    for (size_t i = 0; i < count; ++i) {
        int32_t value = samples[i];
        uint32_t absolute_value = value < 0 ? (uint32_t)-value : (uint32_t)value;
        if (absolute_value > local_peak) local_peak = absolute_value;
        square_sum += (uint64_t)((int64_t)value * value);
    }
    *peak = (uint16_t)(local_peak > UINT16_MAX ? UINT16_MAX : local_peak);
    return count == 0 ? 0 : (uint16_t)sqrt((double)square_sum / count);
}

static void write_u16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)value;
    target[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)value;
    target[1] = (uint8_t)(value >> 8);
    target[2] = (uint8_t)(value >> 16);
    target[3] = (uint8_t)(value >> 24);
}

static void make_wav_header(uint8_t *header, uint32_t pcm_bytes)
{
    memcpy(header, "RIFF", 4);
    write_u32(header + 4, pcm_bytes + 36);
    memcpy(header + 8, "WAVEfmt ", 8);
    write_u32(header + 16, 16);
    write_u16(header + 20, 1);
    write_u16(header + 22, GH_AUDIO_CHANNELS);
    write_u32(header + 24, GH_AUDIO_SAMPLE_RATE);
    write_u32(header + 28, GH_AUDIO_SAMPLE_RATE * GH_AUDIO_CHANNELS * 2);
    write_u16(header + 32, GH_AUDIO_CHANNELS * 2);
    write_u16(header + 34, GH_AUDIO_BITS_PER_SAMPLE);
    memcpy(header + 36, "data", 4);
    write_u32(header + 40, pcm_bytes);
}

esp_err_t gh_audio_capture_voice(gh_voice_capture_t *capture,
                                 gh_audio_level_cb_t level_cb,
                                 void *context)
{
    if (capture == NULL || s_codec == NULL) return ESP_ERR_INVALID_STATE;
    memset(capture, 0, sizeof(*capture));
    const size_t max_pcm = GH_AUDIO_SAMPLE_RATE * 2 * GH_VOICE_MAX_SECONDS;
    uint8_t *wav = heap_caps_malloc(GH_WAV_HEADER_BYTES + max_pcm,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (wav == NULL) return ESP_ERR_NO_MEM;
    s_voice_stop_requested = false;

    size_t pcm_bytes = 0;
    uint64_t quiet_sum = 0;
    uint64_t speech_sum = 0;
    uint32_t quiet_frames = 0;
    uint32_t speech_frames = 0;
    uint32_t silence_frames = 0;
    uint32_t underflows = 0;
    uint16_t maximum_peak = 0;
    uint32_t frame_count = 0;
    bool speech = false;
    int16_t frame[GH_AUDIO_FRAME_SAMPLES];
    while (!s_voice_stop_requested && pcm_bytes < max_pcm) {
        const int read_result = esp_codec_dev_read(s_codec, frame, sizeof(frame));
        if (read_result != ESP_CODEC_DEV_OK) {
            underflows++;
            free(wav);
            return ESP_FAIL;
        }
        size_t received = sizeof(frame);
        if (pcm_bytes + received > max_pcm) received = max_pcm - pcm_bytes;
        memcpy(wav + GH_WAV_HEADER_BYTES + pcm_bytes, frame, received);
        pcm_bytes += received;
        uint16_t peak = 0;
        uint16_t rms = frame_level(frame, received / sizeof(int16_t), &peak);
        if (peak > maximum_peak) maximum_peak = peak;
        if (quiet_frames < 18) {
            quiet_sum += rms;
            quiet_frames++;
        }
        const uint32_t baseline = quiet_frames ? (uint32_t)(quiet_sum / quiet_frames) : 0;
        const uint32_t threshold = baseline * 18 / 10 + 600;
        const bool frame_speech = quiet_frames >= 10 && rms > threshold;
        if (frame_speech) {
            speech = true;
            silence_frames = 0;
            speech_sum += rms;
            speech_frames++;
        } else if (speech) {
            silence_frames++;
        }
        /* Updating the AMOLED for every 16 ms audio frame needlessly competes
         * with I2S/Wi-Fi for internal DMA memory.  A ~12.5 Hz meter remains
         * visually smooth while leaving deterministic headroom for SPI TX. */
        frame_count++;
        if (level_cb != NULL && frame_count % GH_VOICE_LEVEL_REPORT_FRAMES == 0) {
            level_cb(rms, peak, frame_speech, context);
        }
        if (speech && silence_frames >= 50 && pcm_bytes >= GH_AUDIO_SAMPLE_RATE * 2) {
            s_voice_stop_requested = true;
        }
        if (!speech && pcm_bytes >= GH_AUDIO_SAMPLE_RATE * 2 * 4) {
            s_voice_stop_requested = true;
        }
    }
    s_voice_stop_requested = true;
    if (pcm_bytes < GH_AUDIO_SAMPLE_RATE / 2) {
        free(wav);
        return ESP_ERR_INVALID_SIZE;
    }
    make_wav_header(wav, (uint32_t)pcm_bytes);
    capture->wav_data = wav;
    capture->wav_size = GH_WAV_HEADER_BYTES + pcm_bytes;
    capture->duration_ms = (uint32_t)(pcm_bytes * 1000 /
                                      (GH_AUDIO_SAMPLE_RATE * 2));
    capture->quiet_rms = quiet_frames ? (uint32_t)(quiet_sum / quiet_frames) : 0;
    capture->speech_rms = speech_frames ? (uint32_t)(speech_sum / speech_frames) : 0;
    capture->peak = maximum_peak;
    capture->overflow_count = 0;
    capture->underflow_count = underflows;
    capture->speech_detected = speech;
    ESP_LOGI(TAG,
             "ESP32-S3 voice capture real PCM duration_ms=%lu quiet_rms=%lu "
             "speech_rms=%lu peak=%lu overflow=%lu underflow=%lu speech=%s",
             (unsigned long)capture->duration_ms,
             (unsigned long)capture->quiet_rms,
             (unsigned long)capture->speech_rms,
             (unsigned long)capture->peak,
             (unsigned long)capture->overflow_count,
             (unsigned long)capture->underflow_count,
             capture->speech_detected ? "true" : "false");
    return ESP_OK;
}

void gh_audio_request_stop(void)
{
    s_voice_stop_requested = true;
}

void gh_audio_capture_free(gh_voice_capture_t *capture)
{
    if (capture == NULL) return;
    free(capture->wav_data);
    memset(capture, 0, sizeof(*capture));
}
