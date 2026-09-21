/*
 * Audio capture and playback.
 *
 * The board carries an ES8311 codec for the speaker and an ES7210 ADC in
 * front of the two microphones. Both are already wired up by the BSP; this
 * service adds recording to a WAV file, playback, and the running level the
 * Audio screen's meter draws.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SVC_AUDIO_IDLE = 0,
    SVC_AUDIO_RECORDING,
    SVC_AUDIO_PLAYING,
} svc_audio_state_t;

typedef struct {
    svc_audio_state_t state;
    uint32_t          elapsed_ms;
    uint8_t           level;        /* 0-100, smoothed input level */
    char              path[96];     /* file being written or played */
} svc_audio_status_t;

/** @brief Bring up I2S and both codecs. */
esp_err_t svc_audio_init(void);

/** @brief Current state, including the live meter level. */
const svc_audio_status_t *svc_audio_status(void);

/**
 * @brief Start recording 16-bit mono 16 kHz WAV to @p path.
 *
 * Pass NULL to have the service name the file by timestamp under
 * svc_storage_media_root().
 */
esp_err_t svc_audio_record_start(const char *path);

/** @brief Stop recording and finalise the WAV header. */
esp_err_t svc_audio_record_stop(void);

/** @brief Play a WAV file. */
esp_err_t svc_audio_play(const char *path);

/** @brief Stop playback. */
esp_err_t svc_audio_stop(void);

/** @brief Speaker volume, 0-100. Persisted through svc_settings. */
esp_err_t svc_audio_set_volume(uint8_t percent);

/** @brief Microphone gain, 0-100. Persisted through svc_settings. */
esp_err_t svc_audio_set_mic_gain(uint8_t percent);

/** @brief A short click for button feedback. Silent when volume is 0. */
void svc_audio_click(void);

#ifdef __cplusplus
}
#endif
