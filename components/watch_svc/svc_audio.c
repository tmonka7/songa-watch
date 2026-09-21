#include "watch_svc/svc_audio.h"
#include "watch_svc/svc_event.h"
#include "watch_svc/svc_settings.h"
#include "watch_svc/svc_storage.h"
#include "watch_svc/svc_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"

static const char *TAG = "svc_audio";

#define SAMPLE_RATE_HZ   16000
#define BITS_PER_SAMPLE     16
#define CHANNELS             1
#define CHUNK_SAMPLES      512
#define CHUNK_BYTES      (CHUNK_SAMPLES * sizeof(int16_t))

static svc_audio_status_t     s_status;
static esp_codec_dev_handle_t s_spk;
static esp_codec_dev_handle_t s_mic;
static volatile bool          s_stop_requested;
static TaskHandle_t           s_worker;
static char                   s_pending_path[96];
static volatile bool          s_want_record;
static volatile bool          s_want_play;

/* ------------------------------------------------------------- WAV header */

/* Canonical 44-byte PCM WAV header. Sizes are patched in once recording
 * stops and the final length is known. */
static void wav_header(uint8_t hdr[44], uint32_t data_bytes)
{
    const uint32_t byte_rate   = SAMPLE_RATE_HZ * CHANNELS * (BITS_PER_SAMPLE / 8);
    const uint16_t block_align = CHANNELS * (BITS_PER_SAMPLE / 8);
    const uint32_t riff_size   = data_bytes + 36;

    memcpy(hdr + 0,  "RIFF", 4);
    hdr[4] = (uint8_t)(riff_size);
    hdr[5] = (uint8_t)(riff_size >> 8);
    hdr[6] = (uint8_t)(riff_size >> 16);
    hdr[7] = (uint8_t)(riff_size >> 24);
    memcpy(hdr + 8,  "WAVEfmt ", 8);
    hdr[16] = 16; hdr[17] = 0; hdr[18] = 0; hdr[19] = 0;   /* fmt chunk size */
    hdr[20] = 1;  hdr[21] = 0;                             /* PCM */
    hdr[22] = CHANNELS; hdr[23] = 0;
    hdr[24] = (uint8_t)(SAMPLE_RATE_HZ);
    hdr[25] = (uint8_t)(SAMPLE_RATE_HZ >> 8);
    hdr[26] = (uint8_t)(SAMPLE_RATE_HZ >> 16);
    hdr[27] = (uint8_t)(SAMPLE_RATE_HZ >> 24);
    hdr[28] = (uint8_t)(byte_rate);
    hdr[29] = (uint8_t)(byte_rate >> 8);
    hdr[30] = (uint8_t)(byte_rate >> 16);
    hdr[31] = (uint8_t)(byte_rate >> 24);
    hdr[32] = (uint8_t)block_align; hdr[33] = 0;
    hdr[34] = BITS_PER_SAMPLE; hdr[35] = 0;
    memcpy(hdr + 36, "data", 4);
    hdr[40] = (uint8_t)(data_bytes);
    hdr[41] = (uint8_t)(data_bytes >> 8);
    hdr[42] = (uint8_t)(data_bytes >> 16);
    hdr[43] = (uint8_t)(data_bytes >> 24);
}

/* ---------------------------------------------------------------- metering */

/* Peak of the block, mapped onto 0-100 with a logarithmic feel so quiet
 * speech still moves the meter. */
static uint8_t block_level(const int16_t *samples, size_t count)
{
    int32_t peak = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t v = samples[i];
        if (v < 0) { v = -v; }
        if (v > peak) { peak = v; }
    }
    if (peak < 64) {
        return 0;
    }
    /* log2(peak) spans 6..15 for anything audible; stretch that to 0..100. */
    int bits = 0;
    for (int32_t p = peak; p > 1; p >>= 1) {
        bits++;
    }
    int level = (bits - 6) * 100 / 9;
    if (level < 0)   { level = 0; }
    if (level > 100) { level = 100; }
    return (uint8_t)level;
}

/* ------------------------------------------------------------------ worker */

static void do_record(void)
{
    FILE *f = fopen(s_pending_path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "cannot open %s for writing", s_pending_path);
        s_status.state = SVC_AUDIO_IDLE;
        return;
    }

    uint8_t hdr[44];
    wav_header(hdr, 0);            /* patched on close */
    fwrite(hdr, 1, sizeof(hdr), f);

    const esp_codec_dev_sample_info_t fs = {
        .sample_rate     = SAMPLE_RATE_HZ,
        .channel         = CHANNELS,
        .bits_per_sample = BITS_PER_SAMPLE,
    };
    if (esp_codec_dev_open(s_mic, (esp_codec_dev_sample_info_t *)&fs) != ESP_OK) {
        ESP_LOGE(TAG, "cannot open the microphone");
        fclose(f);
        s_status.state = SVC_AUDIO_IDLE;
        return;
    }

    int16_t *buf = malloc(CHUNK_BYTES);
    if (buf == NULL) {
        esp_codec_dev_close(s_mic);
        fclose(f);
        s_status.state = SVC_AUDIO_IDLE;
        return;
    }

    const int64_t started = esp_timer_get_time();
    uint32_t written = 0;
    uint8_t smoothed = 0;

    while (!s_stop_requested) {
        if (esp_codec_dev_read(s_mic, buf, CHUNK_BYTES) != ESP_OK) {
            break;
        }
        written += (uint32_t)fwrite(buf, 1, CHUNK_BYTES, f);

        /* Exponential smoothing keeps the meter readable instead of
         * flickering on every block. */
        const uint8_t raw = block_level(buf, CHUNK_SAMPLES);
        smoothed = (uint8_t)((smoothed * 3 + raw) / 4);
        s_status.level = smoothed;
        s_status.elapsed_ms = (uint32_t)((esp_timer_get_time() - started) / 1000);
        svc_event_post(WATCH_EV_AUDIO_LEVEL, &smoothed, sizeof(smoothed));
    }

    free(buf);
    esp_codec_dev_close(s_mic);

    /* Rewrite the header now that the length is known. */
    wav_header(hdr, written);
    fseek(f, 0, SEEK_SET);
    fwrite(hdr, 1, sizeof(hdr), f);
    fclose(f);

    ESP_LOGI(TAG, "recorded %lu bytes to %s", (unsigned long)written, s_pending_path);
    s_status.level = 0;
    s_status.state = SVC_AUDIO_IDLE;
}

static void do_play(void)
{
    FILE *f = fopen(s_pending_path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "cannot open %s", s_pending_path);
        s_status.state = SVC_AUDIO_IDLE;
        return;
    }
    fseek(f, 44, SEEK_SET);   /* skip the header; we only write our own WAVs */

    const esp_codec_dev_sample_info_t fs = {
        .sample_rate     = SAMPLE_RATE_HZ,
        .channel         = CHANNELS,
        .bits_per_sample = BITS_PER_SAMPLE,
    };
    if (esp_codec_dev_open(s_spk, (esp_codec_dev_sample_info_t *)&fs) != ESP_OK) {
        fclose(f);
        s_status.state = SVC_AUDIO_IDLE;
        return;
    }

    uint8_t *buf = malloc(CHUNK_BYTES);
    if (buf != NULL) {
        const int64_t started = esp_timer_get_time();
        size_t n;
        while (!s_stop_requested && (n = fread(buf, 1, CHUNK_BYTES, f)) > 0) {
            esp_codec_dev_write(s_spk, buf, n);
            s_status.elapsed_ms = (uint32_t)((esp_timer_get_time() - started) / 1000);
        }
        free(buf);
    }

    esp_codec_dev_close(s_spk);
    fclose(f);
    s_status.state = SVC_AUDIO_IDLE;
}

static void audio_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (s_want_record) {
            s_want_record = false;
            s_stop_requested = false;
            s_status.state = SVC_AUDIO_RECORDING;
            do_record();
        } else if (s_want_play) {
            s_want_play = false;
            s_stop_requested = false;
            s_status.state = SVC_AUDIO_PLAYING;
            do_play();
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

/* ---------------------------------------------------------------------- api */

esp_err_t svc_audio_init(void)
{
    const i2s_std_config_t i2s_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws   = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din  = BSP_I2S_DSIN,
        },
    };
    ESP_RETURN_ON_ERROR(bsp_audio_init(&i2s_cfg), TAG, "i2s");

    s_spk = bsp_audio_codec_speaker_init();
    s_mic = bsp_audio_codec_microphone_init();
    if (s_spk == NULL || s_mic == NULL) {
        ESP_LOGW(TAG, "codec init incomplete (spk=%p mic=%p)", s_spk, s_mic);
    }

    const watch_settings_t *cfg = svc_settings_get();
    (void)svc_audio_set_volume(cfg->volume);
    (void)svc_audio_set_mic_gain(cfg->mic_gain);

    if (xTaskCreatePinnedToCore(audio_task, "watch_aud", 5120, NULL, 4, &s_worker, 1)
        != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "audio ready (%d Hz mono)", SAMPLE_RATE_HZ);
    return ESP_OK;
}

const svc_audio_status_t *svc_audio_status(void)
{
    return &s_status;
}

esp_err_t svc_audio_record_start(const char *path)
{
    if (s_status.state != SVC_AUDIO_IDLE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_mic == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (path != NULL && path[0] != '\0') {
        strncpy(s_pending_path, path, sizeof(s_pending_path) - 1);
        s_pending_path[sizeof(s_pending_path) - 1] = '\0';
    } else {
        struct tm t;
        svc_time_now(&t);
        snprintf(s_pending_path, sizeof(s_pending_path),
                 "%s/REC_%02d%02d%02d_%02d%02d%02d.wav",
                 svc_storage_media_root(),
                 (t.tm_year + 1900) % 100, t.tm_mon + 1, t.tm_mday,
                 t.tm_hour, t.tm_min, t.tm_sec);
    }

    strncpy(s_status.path, s_pending_path, sizeof(s_status.path) - 1);
    s_status.path[sizeof(s_status.path) - 1] = '\0';
    s_status.elapsed_ms = 0;
    s_want_record = true;
    return ESP_OK;
}

esp_err_t svc_audio_record_stop(void)
{
    if (s_status.state != SVC_AUDIO_RECORDING) {
        return ESP_ERR_INVALID_STATE;
    }
    s_stop_requested = true;
    /* Let the worker close the file before the caller looks at it. */
    for (int i = 0; i < 100 && s_status.state != SVC_AUDIO_IDLE; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_OK;
}

esp_err_t svc_audio_play(const char *path)
{
    if (s_status.state != SVC_AUDIO_IDLE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_spk == NULL || path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(s_pending_path, path, sizeof(s_pending_path) - 1);
    s_pending_path[sizeof(s_pending_path) - 1] = '\0';
    strncpy(s_status.path, s_pending_path, sizeof(s_status.path) - 1);
    s_status.elapsed_ms = 0;
    s_want_play = true;
    return ESP_OK;
}

esp_err_t svc_audio_stop(void)
{
    s_stop_requested = true;
    return ESP_OK;
}

esp_err_t svc_audio_set_volume(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    (void)svc_settings_set_volume(percent);
    if (s_spk == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_codec_dev_set_out_vol(s_spk, (int)percent);
}

esp_err_t svc_audio_set_mic_gain(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    (void)svc_settings_set_mic_gain(percent);
    if (s_mic == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    /* esp_codec_dev takes microphone gain in dB; the ES7210 usefully spans
     * roughly 0-37.5 dB, so map the slider onto that. */
    const float gain_db = (float)percent * 37.5f / 100.0f;
    return esp_codec_dev_set_in_gain(s_mic, gain_db);
}

void svc_audio_click(void)
{
    /* Deliberately a no-op until a click asset ships: a synthesised beep
     * through the full open/write/close path costs more than the feedback
     * is worth, and a half-second stall on every tap is worse than silence. */
}
