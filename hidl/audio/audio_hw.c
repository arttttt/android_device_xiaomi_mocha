/*
 * Copyright (C) 2014 Cirrus Logic, Inc.
 * Copyright (C) 2012-14 Wolfson Microelectronics plc
 *
 * This code is heavily based on AOSP HAL for the asus/grouper
 *
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "tinyhal"
/*#define LOG_NDEBUG 0*/

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include <cutils/list.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <cutils/str_parms.h>

#include <utils/Timers.h>

#include <hardware/audio.h>
#include <hardware/hardware.h>

#include <system/audio.h>

#include <tinyalsa/asoundlib.h>

#ifdef TINYHAL_COMPRESS_PLAYBACK
#include <sound/compress_params.h>
#include <sound/compress_offload.h>
#include <tinycompress/tinycompress.h>
#endif

#include <audio_utils/clock.h>
#include <audio_utils/primitives.h>
#include <audio_utils/resampler.h>

#include <tinyhal/audio_config.h>

#include <math.h>

/* These values are defined in _frames_ (not bytes) to match the ALSA API */
#define OUT_PERIOD_SIZE_DEFAULT 256
#define OUT_PERIOD_COUNT_DEFAULT 4
#define OUT_CHANNEL_MASK_DEFAULT AUDIO_CHANNEL_OUT_STEREO
#define OUT_CHANNEL_COUNT_DEFAULT 2
#define OUT_RATE_DEFAULT 44100

#define IN_PERIOD_SIZE_DEFAULT 256
#define IN_PERIOD_COUNT_DEFAULT 4
#define IN_CHANNEL_MASK_DEFAULT AUDIO_CHANNEL_IN_MONO
#define IN_CHANNEL_COUNT_DEFAULT 1
#define IN_RATE_DEFAULT 44100

#define IN_PCM_BUFFER_SIZE_DEFAULT \
        (IN_PERIOD_SIZE_DEFAULT * IN_CHANNEL_COUNT_DEFAULT * sizeof(uint16_t))

/*
 * How long compress_write() will wait for driver to signal a poll()
 * before giving up. Set to -1 to make it wait indefinitely
 */
#define MAX_COMPRESS_POLL_WAIT_MS   -1

#ifndef ETC_PATH
#define ETC_PATH "/system/etc"
#endif

#ifdef TINYHAL_COMPRESS_PLAYBACK
enum async_mode {
    ASYNC_NONE,
    ASYNC_POLL,
    ASYNC_EARLY_DRAIN,
    ASYNC_FULL_DRAIN
};

typedef void* (*async_common_fn_t)(void* arg);

typedef struct {
    bool                    exit;
    pthread_cond_t          cv;
    const struct audio_stream_out* stream;
    stream_callback_t       callback;
    void*                   callback_param;
    pthread_mutex_t         mutex;
    pthread_t               thread;
    enum async_mode         mode;
} async_common_t;
#endif /* TINYHAL_COMPRESS_PLAYBACK */

struct audio_device {
    struct audio_hw_device hw_device;

    pthread_mutex_t lock;
    bool mic_mute;
    struct config_mgr *cm;

    const struct hw_stream *global_stream;

    /* Open PCM output streams, under lock */
    struct listnode pcm_outputs;

    /* All open streams, for finding one by its io handle; under lock */
    struct listnode outputs;
    struct listnode inputs;

    /* The last audio patch handle given out, under lock */
    audio_patch_handle_t last_patch_handle;

    /*
     * The FM radio, see "FM radio" below; under lock. The named streams
     * of the config that route it, the patch that plays it and the
     * devices it plays on, the inputs on the tuner, and the gain the patch
     * last took, with the bottom of its volume's scale, below which FM
     * mutes.
     */
    const struct hw_stream *fm_stream;
    const struct hw_stream *fm_capture_stream;
    audio_patch_handle_t fm_patch_handle;
    uint32_t fm_devices;
    int fm_captures;
    bool fm_gain_set;
    long fm_gain_mb;
    long fm_min_mb;
    const char *fm_tap;     /* the tap use-case that is on, NULL for none */
};


typedef void(*out_close_fn)(struct audio_stream_out *);

/* Fields common to all types of output stream */
struct stream_out_common {
    struct audio_stream_out stream;

    out_close_fn    close;
    struct audio_device *dev;
    const struct hw_stream *hw;

    pthread_mutex_t lock;

    bool standby;

    /* In audio_device.outputs. The io handle and the handle of the patch
     * that routes the stream are under the device lock */
    struct listnode node;
    audio_io_handle_t io_handle;
    audio_patch_handle_t patch_handle;

    /*
     * Stream parameters as seen by AudioFlinger
     * If stream is resampling AudioFlinger buffers before
     * passing them to hardware, these members refer to the
     * _input_ data from AudioFlinger
     */
    audio_format_t format;
    uint32_t channel_mask;
    int channel_count;
    uint32_t sample_rate;
    size_t frame_size;
    uint32_t buffer_size;

    uint32_t latency;

#ifdef TINYHAL_COMPRESS_PLAYBACK
    bool use_async;
    async_common_t async_common;
#endif
};

/*
 * What the PCM of an output stream that plays at its track's rate takes,
 * as read from ALSA when the stream is opened: the standard rates within
 * its rate range, and its sample formats.
 */
#define OUT_PCM_CAPS_MAX_RATES 6

struct out_pcm_caps {
    uint32_t rates[OUT_PCM_CAPS_MAX_RATES];
    int num_rates;
    bool s16;
    bool s24;
};

struct stream_out_pcm {
    struct stream_out_common common;

    struct pcm *pcm;

    struct listnode node;   /* in audio_device.pcm_outputs */

    /* Set when the config gives the stream no rate: the PCM then runs at
     * the rate, and in the format, it was opened with */
    bool follows_track;
    struct out_pcm_caps caps;

    /* What is written to the PCM when it isn't the track's data as is:
     * 24-bit packed unpacked to S24_LE */
    void *scratch;
    size_t scratch_size;


    uint32_t hw_sample_rate;    /* Actual sample rate of hardware */
    int hw_channel_count;       /* Actual number of output channels */

    /* For get_presentation_position: frames handed to the PCM since the
     * stream was opened (standby does not reset it), and the last answer
     * given, so that one is repeated while there is no running PCM to ask. */
    uint64_t frames_written;
    uint64_t presented_frames;
    struct timespec presented_time;
    bool presented_valid;
};

#ifdef TINYHAL_COMPRESS_PLAYBACK
struct stream_out_compress {
    struct stream_out_common common;

    struct compress *compress;

    struct snd_codec codec;

    struct {
        const uint8_t *data;
        int len;
    } write;

    bool started;
    volatile bool paused; /* Prevents standby while in pause */

    struct compr_gapless_mdata g_data;
    bool refresh_gapless_meta;
};
#endif /* TINYHAL_COMPRESS_PLAYBACK */

struct in_resampler {
    struct resampler_itfe *resampler;
    struct resampler_buffer_provider buf_provider;
    int16_t *buffer;
    size_t in_buffer_size;
    int in_buffer_frames;
    size_t frames_in;
    int read_status;
};

typedef void(*in_close_fn)(struct audio_stream *);

/* Fields common to all types of input stream */
struct stream_in_common {
    struct audio_stream_in stream;

    in_close_fn    close;
    struct audio_device *dev;
    const struct hw_stream *hw;

    pthread_mutex_t lock;

    bool standby;

    /* In audio_device.inputs. The io handle, the handle of the patch
     * that routes the stream and whether that is from the FM tuner are
     * under the device lock */
    struct listnode node;
    audio_io_handle_t io_handle;
    audio_patch_handle_t patch_handle;
    bool on_tuner;

    /*
     * Stream parameters as seen by AudioFlinger
     * If stream is resampling AudioFlinger buffers before
     * passing them to hardware, these members refer to the
     * _input_ data from AudioFlinger
     */
    audio_devices_t devices;
    audio_format_t format;
    uint32_t channel_mask;
    int channel_count;
    uint32_t sample_rate;
    size_t frame_size;
    size_t buffer_size;

    int input_source;

    nsecs_t last_read_ns;
};

struct stream_in_pcm {
    struct stream_in_common common;

    struct pcm *pcm;

    uint32_t hw_sample_rate;    /* Actual sample rate of hardware */
    int hw_channel_count;       /* Actual number of input channels */
    uint32_t period_size;       /* ... of PCM input */

    struct in_resampler resampler;

    /* For get_capture_position: frames handed to AudioFlinger since the
     * stream was opened (standby does not reset it), in its rate, and the
     * last answer given, repeated while there is no running PCM to ask. */
    uint64_t frames_read;
    uint64_t captured_frames;
    int64_t captured_time_ns;
    bool captured_valid;
};

static uint32_t out_get_sample_rate(const struct audio_stream *stream);
static uint32_t in_get_sample_rate(const struct audio_stream *stream);

/*********************************************************************
 * Stream common functions
 *********************************************************************/

static int stream_invoke_usecases(const struct hw_stream *stream, const char *kvpairs)
{
    char *parms;
    char *p, *temp;
    char *pval;
    char value[32];
    int ret;

    ALOGV("+stream_invoke_usecases(%p) '%s'", stream, kvpairs);

    parms = strdup(kvpairs);
    if (!parms) {
        return -ENOMEM;
    }

    /*
     * It's not obvious what we should do if multiple parameters
     * are given and we only understand some. The action taken
     * here is to process all that we understand and only return
     * and error if we don't understand any
     */
    ret = -ENOTSUP;

    if (stream != NULL) {
        p = strtok_r(parms, ";", &temp);
        while (p) {
            pval = strchr(p, '=');
            if (pval && (pval[1] != '\0')) {
                *pval = '\0';
                if (apply_use_case(stream, p, pval+1) >= 0) {
                    ret = 0;
                }
                *pval = '=';
            }
            p = strtok_r(NULL, ";", &temp);
        }
    }

    free(parms);

    return ret;
}

/*********************************************************************
 * Output stream common functions
 *********************************************************************/

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    struct stream_out_common *out = (struct stream_out_common *)stream;
    uint32_t rate;

    if (out->sample_rate != 0) {
        rate = out->sample_rate;
    } else {
        rate = out->hw->rate;
    }

    ALOGV("out_get_sample_rate=%u", rate);
    return rate;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return -ENOSYS;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct stream_out_common *out = (struct stream_out_common *)stream;
    ALOGV("out_get_buffer_size(%p): %u", stream, out->buffer_size);
    return out->buffer_size;
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
    struct stream_out_common *out = (struct stream_out_common *)stream;
    audio_channel_mask_t mask;

    if (out->channel_mask != 0) {
        mask = out->channel_mask;
    } else {
        mask = OUT_CHANNEL_MASK_DEFAULT;
    }

    ALOGV("out_get_channels=%x", mask);
    return mask;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    struct stream_out_common *out = (struct stream_out_common *)stream;
    /*ALOGV("out_get_format(%p): 0x%x", stream, out->format);*/
    return out->format;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    return -ENOSYS;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    ALOGV("+out_set_parameters(%p) '%s'", stream, kvpairs);

    struct stream_out_common *out = (struct stream_out_common *)stream;
    struct audio_device *adev = out->dev;

    /* Routes come as audio patches, see "Audio patches" below */
    pthread_mutex_lock(&adev->lock);

    stream_invoke_usecases(out->hw, kvpairs);

    pthread_mutex_unlock(&adev->lock);

    ALOGV("-out_set_parameters(%p)", out);

    /*
     * It's meaningless to return an error here - it's not an error if
     * we were sent a parameter we aren't interested in
     */
    return 0;
}

static void get_audio_format(struct str_parms *str_parms,
                             audio_format_t audio_format)
{
    const char *format;

    switch (audio_format) {
    case AUDIO_FORMAT_PCM:
        format = "AUDIO_FORMAT_PCM";
        break;
    case AUDIO_FORMAT_MP3:
        format = "AUDIO_FORMAT_MP3";
        break;
    case AUDIO_FORMAT_AMR_NB:
        format = "AUDIO_FORMAT_AMR_NB";
        break;
    case AUDIO_FORMAT_AMR_WB:
        format = "AUDIO_FORMAT_AMR_WB";
        break;
    case AUDIO_FORMAT_AAC:
        format = "AUDIO_FORMAT_AAC";
        break;
    case AUDIO_FORMAT_HE_AAC_V1:
        format = "AUDIO_FORMAT_HE_AAC_V1";
        break;
    case AUDIO_FORMAT_HE_AAC_V2:
        format = "AUDIO_FORMAT_HE_AAC_V2";
        break;
    case AUDIO_FORMAT_VORBIS:
        format = "AUDIO_FORMAT_VORBIS";
        break;
    case AUDIO_FORMAT_PCM_16_BIT:
        format = "AUDIO_FORMAT_PCM_16_BIT";
        break;
    case AUDIO_FORMAT_PCM_8_BIT:
        format = "AUDIO_FORMAT_PCM_8_BIT";
        break;
    case AUDIO_FORMAT_PCM_32_BIT:
        format = "AUDIO_FORMAT_PCM_32_BIT";
        break;
    case AUDIO_FORMAT_PCM_8_24_BIT:
        format = "AUDIO_FORMAT_PCM_8_24_BIT";
        break;
    default:
        format = "AUDIO_FORMAT_INVALID";
        break;
    }

    str_parms_add_str(str_parms, AUDIO_PARAMETER_STREAM_SUP_FORMATS, format);
}

static void out_pcm_caps_reply(struct str_parms *query, struct str_parms *reply,
                               const struct out_pcm_caps *caps);

static char *out_get_parameters(const struct audio_stream *stream,
                                const char *keys)
{
    ALOGV("+out_get_parameters(%p) '%s'", stream, keys);

    struct stream_out_common *out = (struct stream_out_common *)stream;
    struct str_parms *query = str_parms_create_str(keys);
    struct str_parms *reply = str_parms_create();
    char *str;

    if (out->hw->type == e_stream_out_pcm &&
            ((struct stream_out_pcm *)out)->follows_track) {
        out_pcm_caps_reply(query, reply, &((struct stream_out_pcm *)out)->caps);
    } else if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_FORMATS)) {
        get_audio_format(reply, out->format);
    }

    str = str_parms_to_str(reply);
    str_parms_destroy(query);
    str_parms_destroy(reply);

    ALOGV("-out_get_parameters(%p) '%s'", stream, keys);
    return str;
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    struct stream_out_common *out = (struct stream_out_common *)stream;

    return out->latency;
}

/*
 * AudioFlinger's volume, a linear factor, in mB, the unit the volume
 * controls are set in by their own dB scale. Silence is far below any
 * control's range, which takes it as its lowest value.
 */
#define VOLUME_MB_SILENCE   (-20000)

static long volume_to_mb(float volume)
{
    if (volume <= 0) {
        return VOLUME_MB_SILENCE;
    }
    return lroundf(fmaxf(2000.0f * log10f(volume), VOLUME_MB_SILENCE));
}

/*
 * Only an output whose volume AudioFlinger leaves to the HAL (a DIRECT
 * one) gets here. Its volume goes on the stream's volume control, as
 * the FM patch's gain goes on the fm stream's: the kernel keeps it and
 * puts it on whenever the stream plays.
 */
static int out_set_volume(struct audio_stream_out *stream, float left, float right)
{
    struct stream_out_common *out = (struct stream_out_common *)stream;
    long l_mb = volume_to_mb(left);
    long r_mb = volume_to_mb(right);

    ALOGV("out_set_volume (%f,%f) -> (%ld,%ld) mB", left, right, l_mb, r_mb);

    return set_hw_volume_mb(out->hw, l_mb, r_mb);
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream,
                                        int64_t *timestamp)
{
    return -ENOSYS;
}

#ifdef TINYHAL_COMPRESS_PLAYBACK
static int out_set_callback(struct audio_stream_out *stream,
                            stream_callback_t callback, void *cookie,
                            async_common_fn_t fn)
{
    struct stream_out_common *out = (struct stream_out_common *)stream;
    async_common_t *async = &out->async_common;

    async->exit = false;
    async->mode = ASYNC_NONE;
    /* the thread reads these as soon as it runs: set them before it does */
    async->stream = stream;
    async->callback = callback;
    async->callback_param = cookie;

    int rv = pthread_cond_init(&(async->cv), NULL);
    if (rv != 0) {
        ALOGE("failed to create async condvar");
        return rv;
    }

    rv = pthread_mutex_init(&(async->mutex), NULL);
    if (rv != 0) {
        ALOGE("failed to create async mutex");
        pthread_cond_destroy(&(async->cv));
        return rv;
    }

    rv = pthread_create(&(async->thread), NULL, fn, async);
    if (rv != 0) {
        ALOGE("failed to create async thread");
        pthread_mutex_destroy(&(async->mutex));
        pthread_cond_destroy(&(async->cv));
        return rv;
    }

    out->use_async = true;
    return 0;
}

static int signal_async_thread(async_common_t *async, enum async_mode mode)
{
    int ret = 0;

    pthread_mutex_lock(&(async->mutex));
    if (async->mode != ASYNC_NONE) {
        ret = -EBUSY;
    } else {
        async->mode = mode;
        pthread_cond_signal(&(async->cv));
    }
    pthread_mutex_unlock(&(async->mutex));
    return ret;
}
#endif /* TINYHAL_COMPRESS_PLAYBACK */

static void do_close_out_common(struct audio_stream_out *stream)
{
    struct stream_out_common *out = (struct stream_out_common *)stream;

#ifdef TINYHAL_COMPRESS_PLAYBACK
    /* Signal the async thread to stop and exit */
    if (out->use_async) {
        pthread_mutex_lock(&(out->async_common.mutex));
        out->async_common.exit = true;
        pthread_cond_signal(&(out->async_common.cv));
        pthread_mutex_unlock(&(out->async_common.mutex));
        // Wait for thread to exit
        pthread_join(out->async_common.thread, NULL);
        pthread_mutex_destroy(&(out->async_common.mutex));
        pthread_cond_destroy(&(out->async_common.cv));
    }
#endif /* TINYHAL_COMPRESS_PLAYBACK */

    release_stream(out->hw);
    free(stream);
}

static int do_init_out_common(struct stream_out_common *out,
                              const struct audio_config *config,
                              audio_devices_t devices)
{
    int ret;

    ALOGV("do_init_out_common rate=%u channels=%x",
                config->sample_rate,
                config->channel_mask);

    out->standby = true;

    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;

    /* Init requested stream config */
    out->format = config->format;
    out->sample_rate = config->sample_rate;
    out->channel_mask = config->channel_mask;
    out->channel_count = audio_channel_count_from_out_mask(out->channel_mask);

    /* Default settings */
    out->frame_size = audio_stream_out_frame_size(&out->stream);
    /* Apply initial route */
    apply_route(out->hw, devices);

    return 0;
}

/*********************************************************************
 * PCM output stream
 *********************************************************************/

static unsigned int out_pcm_cfg_period_count(struct stream_out_pcm *out)
{
    if (out->common.hw->period_count != 0) {
        return out->common.hw->period_count;
    } else {
        return OUT_PERIOD_COUNT_DEFAULT;
    }
}

static unsigned int out_pcm_cfg_period_size(struct stream_out_pcm *out)
{
    if (out->common.hw->period_size != 0) {
        return out->common.hw->period_size;
    } else {
        return OUT_PERIOD_SIZE_DEFAULT;
    }
}

static unsigned int out_pcm_cfg_rate(struct stream_out_pcm *out)
{
    if (out->common.hw->rate != 0) {
        return out->common.hw->rate;
    } else if (out->follows_track) {
        return out->common.sample_rate;
    } else {
        return OUT_RATE_DEFAULT;
    }
}

static unsigned int out_pcm_cfg_channel_count(struct stream_out_pcm *out)
{
    if (out->common.channel_count != 0) {
        return out->common.channel_count;
    } else {
        return OUT_CHANNEL_COUNT_DEFAULT;
    }
}

/* The rates AudioFlinger's profiles know, of which a PCM's range is cut */
static const uint32_t out_pcm_std_rates[OUT_PCM_CAPS_MAX_RATES] = {
    44100, 48000, 88200, 96000, 176400, 192000
};

static int out_pcm_read_caps(const struct hw_stream *hw, struct out_pcm_caps *caps)
{
    struct pcm_params *params;
    unsigned int min, max;
    int i;

    params = pcm_params_get(hw->card_number, hw->device_number, PCM_OUT);
    if (!params) {
        ALOGE("Can't read the parameters of PCM %u:%u",
              hw->card_number, hw->device_number);
        return -ENODEV;
    }

    memset(caps, 0, sizeof(*caps));
    min = pcm_params_get_min(params, PCM_PARAM_RATE);
    max = pcm_params_get_max(params, PCM_PARAM_RATE);
    for (i = 0; i < OUT_PCM_CAPS_MAX_RATES; ++i) {
        if (out_pcm_std_rates[i] >= min && out_pcm_std_rates[i] <= max) {
            caps->rates[caps->num_rates++] = out_pcm_std_rates[i];
        }
    }
    caps->s16 = pcm_params_format_test(params, PCM_FORMAT_S16_LE);
    caps->s24 = pcm_params_format_test(params, PCM_FORMAT_S24_LE);

    /* Only stereo is offered; a PCM that can't take it has nothing to offer */
    if (pcm_params_get_min(params, PCM_PARAM_CHANNELS) > 2 ||
            pcm_params_get_max(params, PCM_PARAM_CHANNELS) < 2) {
        caps->num_rates = 0;
    }
    pcm_params_free(params);

    ALOGV("PCM %u:%u rates %u-%u (%d standard) s16=%d s24=%d",
          hw->card_number, hw->device_number, min, max,
          caps->num_rates, caps->s16, caps->s24);

    return caps->num_rates > 0 && (caps->s16 || caps->s24) ? 0 : -ENODEV;
}

static bool out_pcm_caps_rate(const struct out_pcm_caps *caps, uint32_t rate)
{
    int i;

    for (i = 0; i < caps->num_rates; ++i) {
        if (caps->rates[i] == rate) {
            return true;
        }
    }
    return false;
}

/* 24-bit packed goes to the PCM as S24_LE, unpacked on the way */
static bool out_pcm_caps_format(const struct out_pcm_caps *caps,
                                audio_format_t format)
{
    switch (format) {
    case AUDIO_FORMAT_PCM_16_BIT:
        return caps->s16;
    case AUDIO_FORMAT_PCM_8_24_BIT:
    case AUDIO_FORMAT_PCM_24_BIT_PACKED:
        return caps->s24;
    default:
        return false;
    }
}

/*
 * Check a requested config against the PCM. On a mismatch the config is
 * changed to the nearest one the PCM takes, as audio.h asks of a failed
 * open, and false is returned.
 */
static bool out_pcm_caps_fit(const struct out_pcm_caps *caps,
                             struct audio_config *config)
{
    bool fit = true;

    if (!out_pcm_caps_rate(caps, config->sample_rate)) {
        config->sample_rate = out_pcm_caps_rate(caps, 48000)
                                ? 48000 : caps->rates[0];
        fit = false;
    }
    if (!out_pcm_caps_format(caps, config->format)) {
        config->format = caps->s24 ? AUDIO_FORMAT_PCM_8_24_BIT
                                   : AUDIO_FORMAT_PCM_16_BIT;
        fit = false;
    }
    if (config->channel_mask != AUDIO_CHANNEL_OUT_STEREO) {
        config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
        fit = false;
    }
    return fit;
}

static void out_pcm_caps_reply(struct str_parms *query, struct str_parms *reply,
                               const struct out_pcm_caps *caps)
{
    char list[160];
    size_t len = 0;
    int i;

    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES)) {
        list[0] = '\0';
        for (i = 0; i < caps->num_rates; ++i) {
            len += snprintf(list + len, sizeof(list) - len, "%s%u",
                            i ? "|" : "", caps->rates[i]);
        }
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES, list);
    }

    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_FORMATS)) {
        snprintf(list, sizeof(list), "%s%s%s",
                 caps->s16 ? "AUDIO_FORMAT_PCM_16_BIT" : "",
                 caps->s16 && caps->s24 ? "|" : "",
                 caps->s24 ? "AUDIO_FORMAT_PCM_8_24_BIT|AUDIO_FORMAT_PCM_24_BIT_PACKED" : "");
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_FORMATS, list);
    }

    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_CHANNELS)) {
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_CHANNELS,
                          "AUDIO_CHANNEL_OUT_STEREO");
    }
}

/* Called with the stream's lock held; the device lock is not needed */
static void do_out_pcm_standby(struct stream_out_pcm *out)
{
    ALOGV("+do_out_standby(%p)", out);

    if (!out->common.standby) {
        pcm_close(out->pcm);
        out->pcm = NULL;
        out->common.standby = true;
    }

    ALOGV("-do_out_standby(%p)", out);
}

static int pcm_format_from_android_format(audio_format_t format)
{
    switch (format) {
    case AUDIO_FORMAT_PCM_SUB_16_BIT:
        return PCM_FORMAT_S16_LE;
    case AUDIO_FORMAT_PCM_SUB_8_BIT:
        return PCM_FORMAT_S8;
    case AUDIO_FORMAT_PCM_SUB_32_BIT:
        return PCM_FORMAT_S32_LE;
    case AUDIO_FORMAT_PCM_SUB_8_24_BIT:
        return PCM_FORMAT_S24_LE;
    case AUDIO_FORMAT_PCM_SUB_FLOAT:
        return PCM_FORMAT_INVALID;
    case AUDIO_FORMAT_PCM_SUB_24_BIT_PACKED:
        return PCM_FORMAT_S24_3LE;
    default:
        return PCM_FORMAT_INVALID;
    }
}

static void out_pcm_fill_params(struct stream_out_pcm *out,
                                const struct pcm_config *config)
{
    out->hw_sample_rate = config->rate;
    out->hw_channel_count = config->channels;
    out->common.buffer_size = pcm_frames_to_bytes(out->pcm, config->period_size);

    out->common.latency = (config->period_size *
                           config->period_count * 1000) / config->rate;
}

/* Called with the stream's lock held; the device lock is not needed */
static int start_output_pcm(struct stream_out_pcm *out)
{
    int ret;

    struct pcm_config config = {
        .channels = out_pcm_cfg_channel_count(out),
        .rate = out_pcm_cfg_rate(out),
        .period_size = out_pcm_cfg_period_size(out),
        .period_count = out_pcm_cfg_period_count(out),
        .format = out->common.format == AUDIO_FORMAT_PCM_24_BIT_PACKED
                    ? PCM_FORMAT_S24_LE
                    : pcm_format_from_android_format(out->common.format),
        .start_threshold = 0,
        .stop_threshold = 0,
        .silence_threshold = 0
    };

    ALOGV("+start_output_stream(%p)", out);

    out->pcm = pcm_open(out->common.hw->card_number,
                        out->common.hw->device_number,
                        PCM_OUT | PCM_MONOTONIC,
                        &config);

    if (out->pcm && !pcm_is_ready(out->pcm)) {
        ALOGE("pcm_open(out) failed: %s", pcm_get_error(out->pcm));
        pcm_close(out->pcm);
        out->pcm = NULL;
        return -ENOMEM;
    }

    out_pcm_fill_params(out, &config);

    ALOGV("-start_output_stream(%p)", out);
    return 0;
}

static int out_pcm_standby(struct audio_stream *stream)
{
    struct stream_out_pcm *out = (struct stream_out_pcm *)stream;

    pthread_mutex_lock(&out->common.lock);
    do_out_pcm_standby(out);
    pthread_mutex_unlock(&out->common.lock);

    return 0;
}

/*
 * Write a buffer of the track's frames. 24-bit packed is unpacked into the
 * S24_LE words the PCM takes.
 */
static int out_pcm_write_frames(struct stream_out_pcm *out, const void *buffer,
                                size_t bytes)
{
    const size_t frames = bytes / out->common.frame_size;
    const bool p24 = out->common.format == AUDIO_FORMAT_PCM_24_BIT_PACKED;
    const void *data = buffer;
    size_t len = bytes;
    int ret;

    if (p24) {
        len = frames * out->common.channel_count * sizeof(int32_t);
        if (len > out->scratch_size) {
            void *buf = realloc(out->scratch, len);

            if (!buf) {
                return -ENOMEM;
            }
            out->scratch = buf;
            out->scratch_size = len;
        }
        memcpy_to_q8_23_from_p24(out->scratch, buffer,
                                 frames * out->common.channel_count);
        data = out->scratch;
    }

    ret = pcm_write(out->pcm, data, len);
    if (ret >= 0) {
        out->frames_written += frames;
    }
    return ret;
}

/*
 * The outputs share one back end, and the kernel sets its rate from the
 * first PCM to open it; while another output plays it refuses any other
 * rate. A stream at its track's rate that is refused puts the other
 * outputs in standby, holding their locks so none reopens first, and
 * tries again. They reopen on their next write, at the rate the back
 * end then runs, which the DAMs convert them to.
 *
 * Called with out's lock held; takes the device lock, then the others'.
 * No other path takes an output lock under the device lock.
 */
static int start_output_pcm_alone(struct stream_out_pcm *out, int err)
{
    struct audio_device *adev = out->common.dev;
    struct listnode *node;
    bool stopped = false;
    int ret = err;

    pthread_mutex_lock(&adev->lock);

    list_for_each(node, &adev->pcm_outputs) {
        struct stream_out_pcm *o = node_to_item(node, struct stream_out_pcm, node);

        if (o == out) {
            continue;
        }
        pthread_mutex_lock(&o->common.lock);
        if (!o->common.standby) {
            do_out_pcm_standby(o);
            stopped = true;
        }
    }

    if (stopped) {
        ALOGI("Outputs put in standby for a %u Hz stream", out->common.sample_rate);
        ret = start_output_pcm(out);
    }

    list_for_each(node, &adev->pcm_outputs) {
        struct stream_out_pcm *o = node_to_item(node, struct stream_out_pcm, node);

        if (o != out) {
            pthread_mutex_unlock(&o->common.lock);
        }
    }

    pthread_mutex_unlock(&adev->lock);

    return ret;
}

static ssize_t out_pcm_write(struct audio_stream_out *stream, const void *buffer,
                             size_t bytes)
{
    ALOGV("+out_pcm_write(%p) l=%zu", stream, bytes);

    int ret = 0;
    struct stream_out_pcm *out = (struct stream_out_pcm *)stream;

    /*
     * Check that we are routed to something. Android can send routing
     * commands that tell us to disconnect from everything and in that
     * state we shouldn't issue any write commands because we can't be
     * sure that the driver will accept a write to nowhere
     */
    if (get_current_routes(out->common.hw) == 0) {
        ALOGV("-out_pcm_write(%p) 0 (no routes)", stream);
        return 0;
    }

    pthread_mutex_lock(&out->common.lock);
    if (out->common.standby) {
        ret = start_output_pcm(out);
        if (ret != 0 && out->follows_track) {
            ret = start_output_pcm_alone(out, ret);
        }
        if (ret != 0) {
            goto exit;
        }
        out->common.standby = false;
    }

    ret = out_pcm_write_frames(out, buffer, bytes);
    if (ret < 0) {
        ALOGE("out_pcm_write: %s", pcm_get_error(out->pcm));
        /* Close it; the next write opens the PCM afresh */
        do_out_pcm_standby(out);
    }

exit:
    pthread_mutex_unlock(&out->common.lock);

    /*
     * AudioFlinger doesn't like errors, so a failed write still reports
     * every byte. But it also paces the mixer by how long a write blocks:
     * one that fails at once would bring the next buffer straight back and
     * spin the thread while the PCM keeps failing. The buffer is dropped
     * and the time it would have played is spent here, as the Qualcomm
     * HALs do on a failed write.
     */
    if (ret < 0) {
        const size_t frame_size = out->common.frame_size;
        const uint32_t rate = out->common.sample_rate;

        if (frame_size != 0 && rate != 0) {
            usleep((int64_t)(bytes / frame_size) * 1000000 / rate);
        }
    }

    ALOGV("-out_pcm_write(%p) %d", stream, ret);

    return bytes;
}

static int out_pcm_get_render_position(const struct audio_stream_out *stream,
                                       uint32_t *dsp_frames)
{
    return -ENOSYS;
}

/*
 * The contract (audio.h) is frames presented, not written, with the time
 * they were: what is still queued in the PCM is subtracted, and the time is
 * the PCM's own, taken where hw_ptr was read. Asked while there is no
 * running PCM (standby, or before the first period has played), the last
 * answer is repeated; before any, -ENODATA. The count survives standby.
 */
static int out_pcm_get_presentation_position(const struct audio_stream_out *stream,
                                             uint64_t *frames, struct timespec *timestamp)
{
    struct stream_out_pcm *out = (struct stream_out_pcm *)stream;
    unsigned int avail;
    struct timespec now;
    int ret = -ENODATA;

    if (stream == NULL || frames == NULL || timestamp == NULL) {
        return -EINVAL;
    }

    pthread_mutex_lock(&out->common.lock);

    if (!out->common.standby && out->pcm != NULL &&
            pcm_get_htimestamp(out->pcm, &avail, &now) == 0) {
        const unsigned int size = pcm_get_buffer_size(out->pcm);
        /* avail beyond the buffer is an underrun: nothing left queued */
        const uint64_t queued = (avail < size) ? size - avail : 0;
        const uint64_t presented = (out->frames_written > queued)
                                        ? out->frames_written - queued : 0;

        /* never let the count step back, whatever the driver reports */
        if (!out->presented_valid || presented >= out->presented_frames) {
            out->presented_frames = presented;
            out->presented_time = now;
            out->presented_valid = true;
        }
    }

    if (out->presented_valid) {
        *frames = out->presented_frames;
        *timestamp = out->presented_time;
        ret = 0;
    }

    pthread_mutex_unlock(&out->common.lock);

    ALOGV("%s: %d presented %" PRIu64, __func__, ret, out->presented_frames);
    return ret;
}

static void do_close_out_pcm(struct audio_stream_out *stream)
{
    struct stream_out_pcm *out = (struct stream_out_pcm *)stream;

    pthread_mutex_lock(&out->common.dev->lock);
    list_remove(&out->node);
    pthread_mutex_unlock(&out->common.dev->lock);

    out_pcm_standby(&stream->common);
    free(out->scratch);
    do_close_out_common(stream);
}

static int do_init_out_pcm(struct stream_out_pcm *out,
                           const struct audio_config *config)
{
    out->common.close = do_close_out_pcm;
    out->common.stream.common.standby = out_pcm_standby;
    out->common.stream.write = out_pcm_write;
    out->common.stream.get_render_position = out_pcm_get_render_position;
    out->common.stream.get_presentation_position = out_pcm_get_presentation_position;

    out->common.buffer_size = out_pcm_cfg_period_size(out) * out->common.frame_size;

    out->common.latency = (out_pcm_cfg_period_size(out) *
                out_pcm_cfg_period_count(out) * 1000) / out->common.sample_rate;

    return 0;
}

/********************************************************************
 * Compressed output stream
 ********************************************************************/

#ifdef TINYHAL_COMPRESS_PLAYBACK
static int do_standby_compress_l(struct stream_out_compress *out)
{
    int ret;

    if (out->compress && !out->paused) {
        ALOGV("out_compress_standby(%p) not paused -closing compress\n", out);
        if (out->started) {
            compress_stop(out->compress);
            out->started = false;
        }
        compress_close(out->compress);
        out->compress = NULL;
    }

    return 0;
}

static int out_compress_standby(struct audio_stream *stream)
{
    struct stream_out_compress *out = (struct stream_out_compress *)stream;
    int ret;

    pthread_mutex_lock(&out->common.lock);
    ret = do_standby_compress_l(out);
    pthread_mutex_unlock(&out->common.lock);
    return ret;
}

static int open_output_compress(struct stream_out_compress *out)
{
    struct compr_config config;
    struct compress *cmpr;
    int ret = 0;

    pthread_mutex_lock(&out->common.lock);

    if (!out->compress) {
        config.fragment_size = 0;   /* Don't care */
        config.fragments = 0;
        config.codec = &out->codec;

        /*
         * tinycompress in & out defines are the reverse of tinyalsa
         * For tinycompress COMPRESS_IN=output, COMPRESS_OUT=input
         */
        cmpr = compress_open(out->common.hw->card_number,
                             out->common.hw->device_number,
                             COMPRESS_IN,
                             &config);
        if (!is_compress_ready(cmpr)) {
            ALOGE("Failed to open output compress: %s", compress_get_error(cmpr));
            compress_close(cmpr);
            ret = -EBUSY;
            goto exit;
        }
        compress_set_max_poll_wait(cmpr, MAX_COMPRESS_POLL_WAIT_MS);
        compress_nonblock(cmpr, out->common.use_async);
        out->common.buffer_size = config.fragment_size * config.fragments;
        ALOGV("compressed buffer size=%u", out->common.buffer_size);
        out->compress = cmpr;
    }

exit:
    pthread_mutex_unlock(&out->common.lock);

    return ret;
}

static int start_output_compress(struct stream_out_compress *out)
{
    int ret;

    pthread_mutex_lock(&out->common.lock);

    ret = compress_start(out->compress);

    if (ret < 0) {
        do_standby_compress_l(out);
    } else {
        out->started = true;
        if (out->refresh_gapless_meta) {
            compress_set_gapless_metadata(out->compress,&out->g_data);
            out->refresh_gapless_meta = false;
            out->g_data.encoder_delay = 0;
            out->g_data.encoder_padding = 0;
        }
    }

    pthread_mutex_unlock(&out->common.lock);
    return ret;
}

static ssize_t out_compress_write(struct audio_stream_out *stream,
                                  const void *buffer, size_t bytes)
{
    struct stream_out_compress *out = (struct stream_out_compress *)stream;
    int ret = 0;

    ALOGV("out_compress_write(%p) %zu", stream, bytes);

    ret = open_output_compress(out);

    if (ret < 0) {
        ALOGE("out_compress_write(%p): failed to open: %d", stream, ret);
        return ret;
    }

    ret = compress_write(out->compress, buffer, bytes);

    if (ret >= 0) {
        if (!out->started) {
            if (start_output_compress(out) < 0) {
                ret = -1;
                goto start_failed;
            }
        }

        if (out->common.use_async) {
            if ((unsigned)ret < bytes) {
                /* Not all bytes written */
                signal_async_thread(&out->common.async_common, ASYNC_POLL);
            }
        }
    }
start_failed:
    ALOGE_IF(ret < 0,"out_compress_write(%p) failed: %d\n", stream, ret);
    return ret;
}

static int out_compress_pause(struct audio_stream_out *stream)
{
    struct stream_out_compress *out = (struct stream_out_compress *)stream;
    int ret = -EBADFD;

    ALOGV("out_compress_pause(%p)", stream);

    /* Avoid race condition with standby */
    pthread_mutex_lock(&out->common.lock);

    if (!out->paused && out->compress) {
        out->paused = true;
        ret = compress_pause(out->compress);
    }
    pthread_mutex_unlock(&out->common.lock);
    return ret;
}

static int out_compress_resume(struct audio_stream_out *stream)
{
    struct stream_out_compress *out = (struct stream_out_compress *)stream;
    int ret = -EBADFD;

    ALOGV("out_compress_resume(%p)", stream);

    /* Avoid race condition with standby */
    pthread_mutex_lock(&out->common.lock);
    if (out->paused && out->compress) {
        out->paused = false;
        ret = compress_resume(out->compress);
    }
    pthread_mutex_unlock(&out->common.lock);
    return ret;
}

static int out_compress_drain(struct audio_stream_out *stream,
                              audio_drain_type_t type)
{
    struct stream_out_compress *out = (struct stream_out_compress *)stream;
    int ret = 0;

    ALOGV("out_compress_drain(%p)", stream);

    if (out->common.use_async) {
        ret = signal_async_thread(&out->common.async_common,
                (type == AUDIO_DRAIN_EARLY_NOTIFY)
                    ? ASYNC_EARLY_DRAIN
                    : ASYNC_FULL_DRAIN);
    } else {
        if (type == AUDIO_DRAIN_EARLY_NOTIFY) {
            ret = compress_next_track(out->compress);
            if (ret != 0) {
                return ret;
            }
            ret = compress_partial_drain(out->compress);
        } else {
            ret = compress_drain(out->compress);
        }

        out->started = false;
    }

    return ret;
}

static int out_compress_flush(struct audio_stream_out *stream)
{
    struct stream_out_compress *out = (struct stream_out_compress *)stream;

    ALOGV("out_compress_flush(%p)", stream);

    pthread_mutex_lock(&out->common.lock);
    if (out->compress && out->started) {
        compress_stop(out->compress);
        out->paused = false;
        out->started = false;
    }
    pthread_mutex_unlock(&out->common.lock);
    return 0;
}

static int out_compress_get_render_position(const struct audio_stream_out *stream,
                                            uint32_t *dsp_frames)
{
    struct stream_out_compress *out = (struct stream_out_compress *)stream;
#ifdef TINYCOMPRESS_TSTAMP_IS_LONG
    unsigned long samples;
#else
    unsigned int samples;
#endif
    unsigned int sampling_rate;

    if (dsp_frames) {
        *dsp_frames = 0;

        if (!out->started) {
            ALOGV("out_compress_get_render_position(%p) not started", stream);
            return 0;
        }

        pthread_mutex_lock(&out->common.lock);

        if (out->started) {
            if (compress_get_tstamp(out->compress, &samples, &sampling_rate) == 0) {
                *dsp_frames = samples;
                ALOGV("compress(%p) render position=%u", stream, *dsp_frames);
            }
        }

        pthread_mutex_unlock(&out->common.lock);
    }

    return 0;
}

static void *out_compress_async_fn(void *arg)
{
    async_common_t * const pW = (async_common_t*)arg;
    struct stream_out_compress *out = (struct stream_out_compress *)pW->stream;
    enum async_mode mode;

    for (;;) {
        pthread_mutex_lock(&(pW->mutex));
        ALOGV("async fn wait for work");
        /* A request may be posted before the wait starts, and a wakeup
         * may be spurious: wait on the state, not on the signal */
        while (pW->mode == ASYNC_NONE && !pW->exit) {
            pthread_cond_wait(&(pW->cv), &(pW->mutex));
        }

        if (pW->exit) {
            pthread_mutex_unlock(&(pW->mutex));
            break;
        }

        mode = pW->mode;
        pW->mode = ASYNC_NONE;
        pthread_mutex_unlock(&(pW->mutex));

        switch (mode) {
            case ASYNC_POLL:
                ALOGV("ASYNC_POLL");

                compress_wait(out->compress, MAX_COMPRESS_POLL_WAIT_MS);

                pW->callback(STREAM_CBK_EVENT_WRITE_READY,
                             NULL,
                             pW->callback_param);
                break;

            case ASYNC_EARLY_DRAIN:
            case ASYNC_FULL_DRAIN:
                ALOGV("ASYNC_%s_DRAIN",
                            (mode == ASYNC_EARLY_DRAIN) ? "EARLY" : "FULL");

                if (mode == ASYNC_EARLY_DRAIN) {
                    compress_next_track(out->compress);
                    compress_partial_drain(out->compress);
                } else {
                    compress_drain(out->compress);
                }

                out->started = false;

                pW->callback(STREAM_CBK_EVENT_DRAIN_READY,
                             NULL,
                             pW->callback_param);
                break;

            default:
                break;
        }

    }

    return NULL;
}

static int out_compress_set_callback(struct audio_stream_out *stream,
                                     stream_callback_t callback, void *cookie)
{
    struct stream_out_compress *out = (struct stream_out_compress *)stream;
    int ret = out_set_callback(stream, callback, cookie, out_compress_async_fn);
    return ret;
}

static void out_compress_close(struct audio_stream_out *stream)
{
    struct stream_out_compress *out = (struct stream_out_compress *)stream;

    ALOGV("out_compress_close(%p)", stream);

    out->paused = false;
    out_compress_standby(&stream->common);

    do_close_out_common(stream);
}

static int out_compress_set_parameters(struct audio_stream *stream, const char *kv_pairs)
{
    struct stream_out_compress *out = (struct stream_out_compress *)stream;
    struct str_parms *parms;
    char value[32];
    int ret;
    bool need_refresh_gapless = false;

    ALOGV("+out_compress_set_parameters(%p) '%s' ", stream, kv_pairs);
    parms = str_parms_create_str(kv_pairs);
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_DELAY_SAMPLES,
                            value, sizeof(value));
    if (ret >= 0) {
        out->g_data.encoder_delay= atoi(value);
        need_refresh_gapless = true;
    }

    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_PADDING_SAMPLES,
                            value, sizeof(value));
    if (ret >= 0) {
        out->g_data.encoder_padding= atoi(value);
        need_refresh_gapless = true;
    }

    if (need_refresh_gapless) {
        out->refresh_gapless_meta = true;
    }

    str_parms_destroy(parms);

    out_set_parameters(&out->common.stream.common, kv_pairs);

    ALOGV("-out_compress_set_parameters(%p)", out);

    /*
     * It's meaningless to return an error here - it's not an error if
     * we were sent a parameter we aren't interested in
     */
    return 0;
}

static int do_init_out_compress(struct stream_out_compress *out,
                                const struct audio_config *config)
{
    int ret;

    out->common.close = out_compress_close;
    out->common.stream.common.standby = out_compress_standby;
    out->common.stream.write = out_compress_write;
    out->common.stream.pause = out_compress_pause;
    out->common.stream.resume = out_compress_resume;
    out->common.stream.drain = out_compress_drain;
    out->common.stream.flush = out_compress_flush;
    out->common.stream.get_render_position = out_compress_get_render_position;
    out->common.stream.set_callback = out_compress_set_callback;
    out->common.stream.common.set_parameters = out_compress_set_parameters;

    /* Struct is pre-initialized to 0x00 */
    /*out->common.latency.screen_off = 0;
    out->common.latency.screen_on = 0;

    out->codec.ch_in = 0;
    out->codec.bit_rate = 0;
    out->codec.profile = 0;
    out->codec.level = 0;
    out->codec.ch_mode = 0;
    out->codec.format = 0;*/
    out->codec.align = 1;
    out->codec.rate_control = SND_RATECONTROLMODE_CONSTANTBITRATE
                              | SND_RATECONTROLMODE_VARIABLEBITRATE;

    out->codec.sample_rate = config->sample_rate;

    switch (audio_get_main_format(config->format)) {
        case AUDIO_FORMAT_MP3:
            out->codec.id = SND_AUDIOCODEC_MP3;
            break;
        case AUDIO_FORMAT_AAC:
            out->codec.id = SND_AUDIOCODEC_AAC;
            break;
        case AUDIO_FORMAT_HE_AAC_V1:
            out->codec.id = SND_AUDIOCODEC_AAC;
            out->codec.level = SND_AUDIOMODE_AAC_HE;
            break;
        case AUDIO_FORMAT_HE_AAC_V2:
            out->codec.id = SND_AUDIOCODEC_AAC;
            out->codec.level = SND_AUDIOMODE_AAC_HE;
            break;
        case AUDIO_FORMAT_VORBIS:
            out->codec.id = SND_AUDIOCODEC_VORBIS;
            break;
        default:
            return -EINVAL;
    }

    out->codec.ch_out = audio_channel_count_from_out_mask(config->channel_mask);

    /*
     * Open compress dev to check that it exists and
     * get the buffer size. If it isn't required soon
     * AudioFlinger will call standby
     */
    ret = open_output_compress(out);
    return ret;
}
#endif /* TINYHAL_COMPRESS_PLAYBACK */

/*********************************************************************
 * Input stream common functions
 *********************************************************************/
static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    const struct stream_in_common *in = (struct stream_in_common *)stream;
    uint32_t rate;

    if (in->sample_rate != 0) {
        rate = in->sample_rate;
    } else {
        rate = in->hw->rate;
    }

    ALOGV("in_get_sample_rate=%u", rate);
    return rate;
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    const struct stream_in_common *in = (struct stream_in_common *)stream;

    if (rate == in->sample_rate) {
        return 0;
    } else {
        return -ENOTSUP;
    }
}

static audio_channel_mask_t in_get_channels(const struct audio_stream *stream)
{
    const struct stream_in_common *in = (struct stream_in_common *)stream;
    audio_channel_mask_t mask;

    if (in->channel_mask != 0) {
        mask = in->channel_mask;
    } else {
        mask = IN_CHANNEL_MASK_DEFAULT;
    }

    ALOGV("in_get_channels=0x%x", mask);
    return mask;
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    const struct stream_in_common *in = (struct stream_in_common *)stream;

    return in->format;
}

static int in_set_format(struct audio_stream *stream, audio_format_t format)
{
    return -ENOSYS;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    const struct stream_in_common *in = (struct stream_in_common *)stream;
    ALOGV("in_get_buffer_size(%p): %zu", stream, in->buffer_size);
    return in->buffer_size;
}

static int in_dump(const struct audio_stream *stream, int fd)
{
    return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    return 0;
}

static char *in_get_parameters(const struct audio_stream *stream,
                               const char *keys)
{
    ALOGV("+in_get_parameters(%p) '%s'", stream, keys);

    struct stream_in_common *in = (struct stream_in_common *)stream;
    struct str_parms *query = str_parms_create_str(keys);
    struct str_parms *reply = str_parms_create();
    char *str;

    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_FORMATS)) {
        get_audio_format(reply, in->format);
    }

    str = str_parms_to_str(reply);
    str_parms_destroy(query);
    str_parms_destroy(reply);

    ALOGV("-in_get_parameters(%p) '%s'", stream, keys);
    return str;
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    return 0;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream)
{
    return 0;
}

static int in_add_audio_effect(const struct audio_stream *stream,
                               effect_handle_t effect)
{
    return 0;
}

static int in_remove_audio_effect(const struct audio_stream *stream,
                                  effect_handle_t effect)
{
    return 0;
}

static void do_in_set_read_timestamp(struct stream_in_common *in)
{
    nsecs_t ns = systemTime(SYSTEM_TIME_MONOTONIC);

    /*
     * 0 is used to mean we don't have a timestamp, so if
     * time count wraps to zero change it to 1
     */
    if (ns == 0) {
        ns = 1;
    }

    in->last_read_ns = ns;
}

/*
 * Delay for the time it would have taken to read <bytes> since the last
 * read at the stream sample rate
 */
static void do_in_realtime_delay(struct stream_in_common *in, size_t bytes)
{
    nsecs_t required_interval;
    nsecs_t required_ns;
    nsecs_t elapsed_ns;
    struct timespec ts;

    if (in->last_read_ns != 0) {
        /*
         * Required interval is calculated so that a left shift 19 places
         * converts approximately to nanoseconds. This avoids the overhead
         * of having to do a 64-bit division if we worked entirely in
         * nanoseconds, and of a large multiply by 1000000 to convert
         * milliseconds to 64-bit nanoseconds.
         * (1907 << 19) = 999817216
         */
        required_interval = (1907 * bytes) / (in->frame_size * in->sample_rate);
        required_ns = (nsecs_t)required_interval << 19;
        elapsed_ns = systemTime(SYSTEM_TIME_MONOTONIC) - in->last_read_ns;

        /* Use ~millisecond accuracy to ignore trivial nanosecond differences */
        if (required_interval > (elapsed_ns >> 19)) {
            ts.tv_sec = 0;
            ts.tv_nsec = required_ns - elapsed_ns;
            nanosleep(&ts, NULL);
        }
    }
}

static void do_close_in_common(struct audio_stream *stream)
{
    struct stream_in_common *in = (struct stream_in_common *)stream;

    in->stream.common.standby(stream);

    if (in->hw != NULL) {
        release_stream(in->hw);
    }

    free(stream);
}

static int do_init_in_common(struct stream_in_common *in,
                             const struct audio_config *config,
                             audio_devices_t devices)
{
    in->standby = true;

    in->close = do_close_in_common;
    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;
    in->stream.common.dump = in_dump;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_audio_effect;
    in->stream.common.remove_audio_effect = in_remove_audio_effect;
    in->stream.set_gain = in_set_gain;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;

    /* Init requested stream config */
    in->format = config->format;
    in->sample_rate = config->sample_rate;
    in->channel_mask = config->channel_mask;
    in->channel_count = audio_channel_count_from_in_mask(in->channel_mask);

    in->frame_size = audio_stream_in_frame_size(&in->stream);
    /* The route itself is put on by the stream's first audio patch */
    in->devices = devices;

    return 0;
}

/*********************************************************************
 * PCM input resampler handling
 *********************************************************************/
static int get_next_buffer(struct resampler_buffer_provider *buffer_provider,
                           struct resampler_buffer *buffer)
{
    struct in_resampler *rsp;
    struct stream_in_pcm *in;

    if (buffer_provider == NULL || buffer == NULL) {
        return -EINVAL;
    }

    rsp = (struct in_resampler *)((char *)buffer_provider -
                                   offsetof(struct in_resampler, buf_provider));
    in = (struct stream_in_pcm *)((char *)rsp -
                                   offsetof(struct stream_in_pcm, resampler));

    if (in->pcm == NULL) {
        buffer->raw = NULL;
        buffer->frame_count = 0;
        rsp->read_status = -ENODEV;
        return -ENODEV;
    }

    if (rsp->frames_in == 0) {
        rsp->read_status = pcm_read(in->pcm,
                                    (void*)rsp->buffer,
                                    rsp->in_buffer_size);
        if (rsp->read_status != 0) {
            ALOGE("get_next_buffer() pcm_read error %d", errno);
            buffer->raw = NULL;
            buffer->frame_count = 0;
            return rsp->read_status;
        }
        rsp->frames_in = rsp->in_buffer_frames;
        if ((in->common.channel_count == 1) && (in->hw_channel_count == 2)) {
            unsigned int i;

            /* Discard right channel */
            for (i = 1; i < rsp->frames_in; i++) {
                rsp->buffer[i] = rsp->buffer[i * 2];
            }
        }
    }

    buffer->frame_count = (buffer->frame_count > rsp->frames_in)
                            ? rsp->frames_in
                            : buffer->frame_count;
    buffer->i16 = (int16_t*)rsp->buffer + ((rsp->in_buffer_frames - rsp->frames_in));

    return rsp->read_status;
}

static void release_buffer(struct resampler_buffer_provider *buffer_provider,
                           struct resampler_buffer *buffer)
{
    struct in_resampler *rsp;

    if (buffer_provider == NULL || buffer == NULL) {
        return;
    }

    rsp = (struct in_resampler *)((char *)buffer_provider -
                                  offsetof(struct in_resampler, buf_provider));

    rsp->frames_in -= buffer->frame_count;
}

static ssize_t read_resampled_frames(struct stream_in_pcm *in,
                                     void *buffer, ssize_t frames)
{
    struct in_resampler *rsp = &in->resampler;
    ssize_t frames_wr = 0;

    while (frames_wr < frames) {
        size_t frames_rd = frames - frames_wr;
        rsp->resampler->resample_from_provider(rsp->resampler,
                                               (int16_t *)((char *)buffer +
                                               (frames_wr * in->common.frame_size)),
                                               &frames_rd);
        if (rsp->read_status != 0) {
            return rsp->read_status;
        }

        frames_wr += frames_rd;
    }
    return frames_wr;
}

static int in_resampler_init(struct stream_in_pcm *in, int hw_rate,
                             int channels, size_t hw_fragment)
{
    struct in_resampler *rsp = &in->resampler;
    int ret = 0;

    rsp->in_buffer_size = hw_fragment * channels * in->common.frame_size;
    rsp->in_buffer_frames = rsp->in_buffer_size /
                                        (channels * in->common.frame_size);
    rsp->buffer = malloc(rsp->in_buffer_size);

    if (!rsp->buffer) {
        ret = -ENOMEM;
    } else {
        rsp->buf_provider.get_next_buffer = get_next_buffer;
        rsp->buf_provider.release_buffer = release_buffer;

        ret = create_resampler(hw_rate,
                               in->common.sample_rate,
                               in->common.channel_count,
                               RESAMPLER_QUALITY_DEFAULT,
                               &rsp->buf_provider,
                               &rsp->resampler);
    }

    if (ret < 0) {
        free(rsp->buffer);
        rsp->buffer = NULL;
    }

    return ret;
}

static void in_resampler_free(struct stream_in_pcm *in)
{
    if (in->resampler.resampler) {
        release_resampler(in->resampler.resampler);
        in->resampler.resampler = NULL;
    }

    free(in->resampler.buffer);
    in->resampler.buffer = NULL;
}

/*********************************************************************
 * PCM input stream
 *********************************************************************/

static unsigned int in_pcm_cfg_period_count(struct stream_in_pcm *in)
{
    if (in->common.hw->period_count != 0) {
        return in->common.hw->period_count;
    } else {
        return IN_PERIOD_COUNT_DEFAULT;
    }
}

static unsigned int in_pcm_cfg_period_size(struct stream_in_pcm *in)
{
    if (in->common.hw->period_size != 0) {
        return in->common.hw->period_size;
    } else {
        return IN_PERIOD_SIZE_DEFAULT;
    }
}

static unsigned int in_pcm_cfg_rate(struct stream_in_pcm *in)
{
    if (in->common.hw->rate != 0) {
        return in->common.hw->rate;
    } else {
        return IN_RATE_DEFAULT;
    }
}

static unsigned int in_pcm_cfg_channel_count(struct stream_in_pcm *in)
{
    if (in->common.channel_count != 0) {
        return in->common.channel_count;
    } else {
        return IN_CHANNEL_COUNT_DEFAULT;
    }
}

/* Called with the stream's lock held; the device lock is not needed */
static void do_in_pcm_standby(struct stream_in_pcm *in)
{
    ALOGV("+do_in_pcm_standby");

    if (!in->common.standby) {
        pcm_close(in->pcm);
        in->pcm = NULL;
    }

    in_resampler_free(in);
    in->common.standby = true;

    ALOGV("-do_in_pcm_standby");
}

/*
 * The buffer AudioFlinger reads in: one PCM period, taking resampling
 * into account, in the closest majoring multiple of 16 frames, as
 * audioflinger expects audio buffers to be a multiple of 16 frames
 */
static size_t in_pcm_buffer_size(const struct stream_in_pcm *in,
                                 unsigned int period_size, unsigned int rate)
{
    size_t size = (period_size * in->common.sample_rate) / rate;

    size = ((size + 15) / 16) * 16;
    return size * in->common.frame_size;
}

static void in_pcm_fill_params(struct stream_in_pcm *in,
                               const struct pcm_config *config)
{
    in->hw_sample_rate = config->rate;
    in->hw_channel_count = config->channels;
    in->period_size = config->period_size;
    in->common.buffer_size = in_pcm_buffer_size(in, config->period_size,
                                                config->rate);
}

/* Called with the stream's lock held; the device lock is not needed */
static int do_open_pcm_input(struct stream_in_pcm *in)
{
    struct pcm_config config;
    int ret;

    ALOGV("+do_open_pcm_input");

    if (in->common.hw == NULL) {
        ALOGW("input_source not set");
        ret = -EINVAL;
        goto exit;
    }

    memset(&config, 0, sizeof(config));
    config.channels = in_pcm_cfg_channel_count(in);
    config.rate = in_pcm_cfg_rate(in),
    config.period_size = in_pcm_cfg_period_size(in),
    config.period_count = in_pcm_cfg_period_count(in),
    config.format = pcm_format_from_android_format(in->common.format),
    config.start_threshold = 0;

    in->pcm = pcm_open(in->common.hw->card_number,
                       in->common.hw->device_number,
                       PCM_IN | PCM_MONOTONIC,
                       &config);

    if (!in->pcm || !pcm_is_ready(in->pcm)) {
        ALOGE_IF(in->pcm,"pcm_open(in) failed: %s", pcm_get_error(in->pcm));
        ALOGE_IF(!in->pcm,"pcm_open(in) failed");
        ret = -ENOMEM;
        goto fail;
    }

    in_pcm_fill_params(in, &config);

    ALOGV("input buffer size=0x%zx", in->common.buffer_size);

    /*
     * If the stream rate differs from the PCM rate, we need to
     * create a resampler.
     */
    if (in_get_sample_rate(&in->common.stream.common) != config.rate) {
        ret = in_resampler_init(in, config.rate, config.channels,
                                pcm_frames_to_bytes(in->pcm, config.period_size));
        if (ret < 0) {
            goto fail;
        }
    }
    ALOGV("-do_open_pcm_input");
    return 0;

fail:
    pcm_close(in->pcm);
    in->pcm = NULL;
exit:
    ALOGV("-do_open_pcm_input error:%d", ret);
    return ret;
}

/* Called with the stream's lock held; the device lock is not needed */
static int start_pcm_input_stream(struct stream_in_pcm *in)
{
    int ret = 0;

    if (in->common.standby) {
        ret = do_open_pcm_input(in);
        if (ret == 0) {
            in->common.standby = 0;
        }
    }

    return ret;
}

static int change_input_source_locked(struct stream_in_pcm *in, int new_source,
                                      uint32_t devices)
{
    struct audio_config config;
    const char *stream_name;
    const struct hw_stream *hw = NULL;

    /* Checked first: a new route for a running capture keeps its source */
    if (in->common.input_source == new_source) {
        ALOGV("input source not changed");
        return 0;
    }

    if (!in->common.standby) {
        ALOGE("attempt to change input source while active");
        return -EINVAL;
    }

    /*
     * Special input sources are obtained from the configuration
     * by opening a named stream
     */
    switch (new_source) {
    case AUDIO_SOURCE_VOICE_RECOGNITION:
        /*
         * We should verify here that current frame size, sample rate and
         * channels are compatible
         */

        /*
         * Depends on voice recognition type and state whether we open
         * the voice recognition stream or generic PCM stream
         */
        stream_name = "voice recognition";
        break;

    case AUDIO_SOURCE_UNPROCESSED:
        stream_name = "unprocessed";
        break;

    default:
        stream_name = NULL;
        break;
    }

    if (stream_name) {
        /* Try to open a stream specific to the chosen input source */
        hw = get_named_stream(in->common.dev->cm, stream_name);
        ALOGV_IF(hw != NULL, "Changing input source to %s", stream_name);
    }

    if (!hw) {
        /* Open generic PCM input stream */
        memset(&config, 0, sizeof(config));
        config.sample_rate = in->common.sample_rate;
        config.channel_mask = in->common.channel_mask;
        config.format = in->common.format;
        hw = get_stream(in->common.dev->cm, devices, 0, &config);
        ALOGV_IF(hw != NULL, "Changing to default input source for devices 0x%x",
                        devices);
    }

    if (hw != NULL) {
        /*
         * A normal stream will be in standby and therefore device node
         * is closed when we get here.
         */

        if (in->common.hw != NULL) {
            release_stream(in->common.hw);
        }

        in->common.hw = hw;
        in->common.input_source = new_source;
        return 0;
    } else {
        ALOGV("Could not open new input stream");
        return -EINVAL;
    }
}

static ssize_t do_in_pcm_read(struct audio_stream_in *stream, void *buffer,
                              size_t bytes)
{
    int ret = 0;
    struct stream_in_pcm *in = (struct stream_in_pcm *)stream;
    size_t frames_rq = bytes / in->common.frame_size;

    ALOGV("+do_in_pcm_read %zu", bytes);

    pthread_mutex_lock(&in->common.lock);
    ret = start_pcm_input_stream(in);

    if (ret < 0) {
        goto exit;
    }

    if (in->resampler.resampler != NULL) {
        ret = read_resampled_frames(in, buffer, frames_rq);
    } else {
        ret = pcm_read(in->pcm, buffer, bytes);
    }

    if (ret >= 0) {
        in->frames_read += frames_rq;
    }

    /* Assume any non-negative return is a successful read */
    if (ret >= 0) {
        ret = bytes;
    }

exit:
    pthread_mutex_unlock(&in->common.lock);

    ALOGV("-do_in_pcm_read (%d)", ret);
    return ret;
}

static int in_pcm_standby(struct audio_stream *stream)
{
    struct stream_in_pcm *in = (struct stream_in_pcm *)stream;

    pthread_mutex_lock(&in->common.lock);

    if (in->common.hw != NULL) {
        do_in_pcm_standby(in);
    }

    pthread_mutex_unlock(&in->common.lock);

    return 0;
}

static ssize_t in_pcm_read(struct audio_stream_in *stream, void *buffer,
                           size_t bytes)
{
    struct stream_in_pcm *in = (struct stream_in_pcm *)stream;
    struct audio_device *adev = in->common.dev;
    int ret;

    if (in->common.hw == NULL) {
        ALOGW("in_pcm_read(%p): no input source for stream", stream);
        ret = -EINVAL;
    } else if (get_current_routes(in->common.hw) == 0) {
        ALOGV("in_pcm_read(%p) (no routes)", stream);
        ret = -EINVAL;
    } else {
        ret = do_in_pcm_read(stream, buffer, bytes);
    }

    /*
     * If error, no data or muted, return a buffer of zeros and delay
     * for the time it would take to capture that much audio at the
     * current sample rate. AudioFlinger can't do anything useful with
     * read errors so convert errors into a read of silence
     */
    if ((ret <= 0) || adev->mic_mute) {
        memset(buffer, 0, bytes);

        /* Only delay if we failed to capture any audio */
        if (ret <= 0) {
            do_in_realtime_delay(&in->common, bytes);
        }

        ret = bytes;
    }

    do_in_set_read_timestamp(&in->common);

    return ret;
}

/*
 * Routes the stream to devices, from the given source: the source first,
 * as it may change the config manager stream the route is put on.
 * Called with the stream's lock held.
 */
static int route_input_locked(struct stream_in_pcm *in, int source,
                              uint32_t devices)
{
    int ret;

    ret = change_input_source_locked(in, source, devices);
    if (ret < 0) {
        return ret;
    }

    in->common.devices = devices;

    if (in->common.hw) {
        ALOGV("Apply routing=0x%x to input stream", devices);
        apply_route(in->common.hw, devices);
    }

    return 0;
}

static int in_pcm_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct stream_in_pcm *in = (struct stream_in_pcm *)stream;

    ALOGV("+in_pcm_set_parameters(%p) '%s'", stream, kvpairs);

    /* Routes and the input source come as audio patches */
    pthread_mutex_lock(&in->common.lock);
    stream_invoke_usecases(in->common.hw, kvpairs);
    pthread_mutex_unlock(&in->common.lock);

    ALOGV("-in_pcm_set_parameters(%p)", stream);

    /*
     * It's meaningless to return an error here - it's not an error if
     * we were sent a parameter we aren't interested in
     */
    return 0;
}

static void do_close_in_pcm(struct audio_stream *stream)
{
    struct stream_in_pcm *in = (struct stream_in_pcm *)stream;

    do_close_in_common(stream);
}

/*
 * audio.h: the total of frames received and the CLOCK_MONOTONIC time they
 * were, taken as early in the capture pipeline as possible. So the count
 * is the frames AudioFlinger has read plus those the PCM has captured and
 * not yet handed over, and the time is the PCM's own, taken where hw_ptr
 * was read. Frames still held by the resampler are left out; they are a
 * fraction of a period. With no running PCM the last answer is repeated;
 * before any, -ENOSYS, which the HIDL wrapper takes as "not yet" quietly.
 */
static int in_pcm_get_capture_position(const struct audio_stream_in *stream,
                                       int64_t *frames, int64_t *time)
{
    struct stream_in_pcm *in = (struct stream_in_pcm *)stream;
    unsigned int avail;
    struct timespec now;
    int ret = -ENOSYS;

    if (stream == NULL || frames == NULL || time == NULL) {
        return -EINVAL;
    }

    pthread_mutex_lock(&in->common.lock);

    if (!in->common.standby && in->pcm != NULL &&
            pcm_get_htimestamp(in->pcm, &avail, &now) == 0) {
        /* avail is in the PCM's frames; the count is in AudioFlinger's */
        const uint64_t pending = (in->hw_sample_rate != 0)
                ? (uint64_t)avail * in->common.sample_rate / in->hw_sample_rate
                : avail;
        const uint64_t captured = in->frames_read + pending;

        /* never let the count step back, whatever the driver reports */
        if (!in->captured_valid || captured >= in->captured_frames) {
            in->captured_frames = captured;
            in->captured_time_ns = audio_utils_ns_from_timespec(&now);
            in->captured_valid = true;
        }
    }

    if (in->captured_valid) {
        *frames = in->captured_frames;
        *time = in->captured_time_ns;
        ret = 0;
    }

    pthread_mutex_unlock(&in->common.lock);

    ALOGV("%s: %d captured %" PRIu64, __func__, ret, in->captured_frames);
    return ret;
}

static int do_init_in_pcm(struct stream_in_pcm *in,
                          struct audio_config *config)
{
    in->common.close = do_close_in_pcm;
    in->common.stream.common.standby = in_pcm_standby;
    in->common.stream.common.set_parameters = in_pcm_set_parameters;
    in->common.stream.read = in_pcm_read;
    in->common.stream.get_capture_position = in_pcm_get_capture_position;

    return 0;
}

/*********************************************************************
 * Stream open and close
 *********************************************************************/
static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char *address)
{
    struct audio_device *adev = (struct audio_device *)dev;
    union {
        struct stream_out_common *common;
        struct stream_out_pcm *pcm;
#ifdef TINYHAL_COMPRESS_PLAYBACK
        struct stream_out_compress *compress;
#endif
    } out;
    int ret;

    ALOGV("+adev_open_output_stream: format %d, channel_mask=%04x, sample_rate %u flags 0x%x\n",
          config->format, config->channel_mask, config->sample_rate, flags);

    devices &= AUDIO_DEVICE_OUT_ALL;
    const struct hw_stream *hw = get_stream(adev->cm, devices, flags, config);
    if (!hw) {
        ALOGE("No suitable output stream for devices=0x%x flags=0x%x format=0x%x",
              devices, flags, config->format);
        ret = -EINVAL;
        goto err_fail;
    }

    /* A PCM stream with no rate in the config plays at its track's rate,
     * so the track has to be one its PCM takes */
    struct out_pcm_caps caps;
    const bool follows_track = hw->type == e_stream_out_pcm && hw->rate == 0;

    if (follows_track) {
        ret = out_pcm_read_caps(hw, &caps);
        if (ret == 0 && !out_pcm_caps_fit(&caps, config)) {
            ALOGW("PCM %u:%u can't play rate %u format 0x%x mask 0x%x as is",
                  hw->card_number, hw->device_number, config->sample_rate,
                  config->format, config->channel_mask);
            ret = -EINVAL;
        }
        if (ret != 0) {
            release_stream(hw);
            goto err_fail;
        }
    }

#ifdef TINYHAL_COMPRESS_PLAYBACK
    out.common = calloc(1, hw->type == e_stream_out_pcm
                            ? sizeof(struct stream_out_pcm)
                            : sizeof(struct stream_out_compress));
#else
    out.common = calloc(1, sizeof(struct stream_out_pcm));
#endif

    if (!out.common) {
        ret = -ENOMEM;
        goto err_fail;
    }

    out.common->dev = adev;
    out.common->hw = hw;
    ret = do_init_out_common(out.common, config, devices);
    if (ret < 0) {
        goto err_open;
    }

    if (hw->type == e_stream_out_pcm) {
        out.pcm->follows_track = follows_track;
        if (follows_track) {
            out.pcm->caps = caps;
        }
        ret = do_init_out_pcm(out.pcm, config);
    } else {
#ifdef TINYHAL_COMPRESS_PLAYBACK
        ret = do_init_out_compress(out.compress, config);
#else
        ret = -ENOTSUP;
#endif
    }

    if (ret < 0) {
        goto err_open;
    }

    out.common->io_handle = handle;
    out.common->patch_handle = AUDIO_PATCH_HANDLE_NONE;

    pthread_mutex_lock(&adev->lock);
    list_add_tail(&adev->outputs, &out.common->node);
    if (hw->type == e_stream_out_pcm) {
        list_add_tail(&adev->pcm_outputs, &out.pcm->node);
    }
    pthread_mutex_unlock(&adev->lock);

    /* Update config with initial stream settings */
    config->format = out.common->format;
    config->channel_mask = out.common->channel_mask;
    config->sample_rate = out.common->sample_rate;

    *stream_out = &out.common->stream;
    ALOGV("-adev_open_output_stream=%p", *stream_out);
    return 0;

err_open:
    free(out.common);
    *stream_out = NULL;
err_fail:
    ALOGV("-adev_open_output_stream (%d)", ret);
    return ret;
}

static void fm_apply_tap_l(struct audio_device *adev);

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct stream_out_common *out = (struct stream_out_common *)stream;
    ALOGV("adev_close_output_stream(%p)", stream);

    /* Its patch, if still there, goes with it */
    pthread_mutex_lock(&adev->lock);
    list_remove(&out->node);
    pthread_mutex_unlock(&adev->lock);

    (out->close)(stream);

    /* Its route gone, FM may be taken from another mixer */
    pthread_mutex_lock(&adev->lock);
    fm_apply_tap_l(adev);
    pthread_mutex_unlock(&adev->lock);
}

static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in,
                                  audio_input_flags_t flags,
                                  const char *address,
                                  audio_source_t source)
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct stream_in_pcm *in = NULL;
    int ret;

    ALOGV("+adev_open_input_stream");

    ALOGV("Tinyhal opening input stream format %d, channel_mask=%04x, sample_rate %u"
          " flags 0x%x source 0x%x\n",
          config->format, config->channel_mask, config->sample_rate,
          flags, source);

    *stream_in = NULL;

    devices &= AUDIO_DEVICE_IN_ALL;
    const struct hw_stream *hw = get_stream(adev->cm, devices, 0, config);
    if (!hw) {
        ALOGE("No suitable input stream for devices=0x%x flags=0x%x format=0x%x",
              devices, flags, config->format);
        ret = -EINVAL;
        goto fail;
    }

    in = (struct stream_in_pcm *)calloc(1, sizeof(struct stream_in_pcm));
    if (!in) {
        ret = -ENOMEM;
        goto fail;
    }

    in->common.dev = adev;
    in->common.hw = hw;
    ret = do_init_in_common(&in->common, config, devices);
    if (ret < 0) {
        goto fail;
    }

    ret = do_init_in_pcm(in, config);
    if (ret < 0) {
        goto fail;
    }

    /*
     * The source is known at open (the HIDL wrapper takes it from the
     * stream's sink metadata), so the config manager stream for it is
     * chosen now; the patch that routes the stream brings it again.
     * Failing to find one leaves the generic stream, as before.
     */
    if (change_input_source_locked(in, source, devices) < 0) {
        ALOGW("No input stream for source %d, keeping the generic one",
              source);
    }

    /*
     * AudioFlinger takes the buffer size once, now, and from it decides
     * whether the record thread runs a fast capture, which takes no
     * software effects: it is the chosen stream's period, as the PCM
     * opens with it
     */
    in->common.buffer_size = in_pcm_buffer_size(in, in_pcm_cfg_period_size(in),
                                                in_pcm_cfg_rate(in));

    in->common.io_handle = handle;
    in->common.patch_handle = AUDIO_PATCH_HANDLE_NONE;

    pthread_mutex_lock(&adev->lock);
    list_add_tail(&adev->inputs, &in->common.node);
    pthread_mutex_unlock(&adev->lock);

    *stream_in = &in->common.stream;
    return 0;

fail:
    free(in);
    ALOGV("-adev_open_input_stream (%d)", ret);
    return ret;
}



static void fm_set_tuner_input_l(struct audio_device *adev,
                                 struct stream_in_common *in, bool on);

static void adev_close_input_stream(struct audio_hw_device *dev,
                                    struct audio_stream_in *stream)
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct stream_in_common *in = (struct stream_in_common *)stream;
    ALOGV("adev_close_input_stream(%p)", stream);

    /* Its patch, if still there, goes with it, and its hold on FM */
    pthread_mutex_lock(&adev->lock);
    list_remove(&in->node);
    fm_set_tuner_input_l(adev, in, false);
    pthread_mutex_unlock(&adev->lock);

    (in->close)(&stream->common);
}

/*********************************************************************
 * FM radio
 *
 * FM never passes through a PCM: the BCM4354 plays it into the codec's
 * AIF4, and the codec takes it on to the speakers or headphones, or to
 * its ADC path for a capture from the tuner. Two named "hw" streams of
 * the config route it. "fm" plays it, for the audio patch from the tuner
 * to devices: it goes to those devices and to the fm chain. "fm capture"
 * goes to the fm chain alone, for as long as an input stream captures
 * the tuner. The config manager counts the streams on the chain, so the
 * patch and a capture can come and go in any order.
 *
 * A capture takes FM from DAC1 through a mixer that no output in use
 * plays, or the outputs would play it a second time, straight from DAC1
 * beside what plays the capture: the headphones play the stereo DAC
 * mixers, the speakers the mono ones. "fm capture" has a use-case for
 * each tap, "mono tap" and "stereo tap", "on" and "off": the mono one
 * while headphones or a headset are among the devices the outputs and
 * the patch play on, the stereo one otherwise, none without a capture.
 * Each "off" opens only what its "on" closed, which no output in use
 * shares while that tap is the one on. The taps follow the routes: one
 * that goes is off before a route changes, so no output that joins plays
 * the mixer it was on, and the one that comes is on after.
 *
 * Its volume is the fm stream's control, the kernel's FM Playback Volume,
 * past the taps: the gain the patch's source port is given, in mB, set
 * on the control's own dB scale. DAC1's mixer switches are the fm
 * stream's "mute" use-case's alone: open while the chain is off or the
 * gain is below that scale, closed otherwise -- after the chain is
 * routed, and before it goes. All of it is under the
 * device lock.
 *********************************************************************/

/* The sinks an FM patch can play on: those the fm stream's paths serve */
#define FM_PATCH_SINKS  (AUDIO_DEVICE_OUT_SPEAKER | \
                         AUDIO_DEVICE_OUT_WIRED_HEADSET | \
                         AUDIO_DEVICE_OUT_WIRED_HEADPHONE)

static bool fm_chain_on_l(const struct audio_device *adev)
{
    return adev->fm_patch_handle != AUDIO_PATCH_HANDLE_NONE ||
           adev->fm_captures > 0;
}

static void fm_apply_gain_l(struct audio_device *adev)
{
    const bool below = adev->fm_gain_set && adev->fm_gain_mb < adev->fm_min_mb;

    if (adev->fm_gain_set && !below) {
        set_hw_volume_mb(adev->fm_stream, adev->fm_gain_mb, adev->fm_gain_mb);
    }

    apply_use_case(adev->fm_stream, "mute",
                   (below || !fm_chain_on_l(adev)) ? "on" : "off");
}

/* The outputs that play FM's stereo DAC mixers; the speakers play the mono */
#define FM_TAP_MONO_SINKS  (AUDIO_DEVICE_OUT_WIRED_HEADSET | \
                            AUDIO_DEVICE_OUT_WIRED_HEADPHONE)

/* The tap use-cases; adev->fm_tap is one of them, or NULL */
static const char fm_tap_mono[] = "mono tap";
static const char fm_tap_stereo[] = "stereo tap";

/*
 * The tap for the routes, with one output's about to become new_devices
 * (out NULL for none): NULL without a capture
 */
static const char *fm_tap_for_l(struct audio_device *adev,
                                const struct stream_out_common *out,
                                uint32_t new_devices)
{
    uint32_t devices = new_devices;
    struct listnode *node;

    if (adev->fm_capture_stream == NULL || adev->fm_captures == 0) {
        return NULL;
    }

    list_for_each(node, &adev->outputs) {
        const struct stream_out_common *o =
                node_to_item(node, struct stream_out_common, node);
        if (o != out) {
            devices |= get_current_routes(o->hw);
        }
    }
    if (adev->fm_patch_handle != AUDIO_PATCH_HANDLE_NONE) {
        devices |= adev->fm_devices;
    }
    return (devices & FM_TAP_MONO_SINKS) ? fm_tap_mono : fm_tap_stereo;
}

/* Before a route changes: the tap on goes, unless it is the one to stay */
static void fm_leave_tap_l(struct audio_device *adev, const char *tap)
{
    if (adev->fm_tap != NULL && adev->fm_tap != tap) {
        apply_use_case(adev->fm_capture_stream, adev->fm_tap, "off");
        ALOGV("%s: %s off", __func__, adev->fm_tap);
        adev->fm_tap = NULL;
    }
}

/* After it: the tap for the new routes comes on */
static void fm_join_tap_l(struct audio_device *adev, const char *tap)
{
    if (tap != NULL && adev->fm_tap != tap) {
        apply_use_case(adev->fm_capture_stream, tap, "on");
        ALOGV("%s: %s on", __func__, tap);
        adev->fm_tap = tap;
    }
}

static void fm_apply_tap_l(struct audio_device *adev)
{
    const char *tap = fm_tap_for_l(adev, NULL, 0);

    fm_leave_tap_l(adev, tap);
    fm_join_tap_l(adev, tap);
}

/* Route an output, the tap around it */
static void route_output_l(struct audio_device *adev,
                           struct stream_out_common *out, uint32_t devices)
{
    const char *tap = fm_tap_for_l(adev, out, devices);

    fm_leave_tap_l(adev, tap);
    apply_route(out->hw, devices);
    fm_join_tap_l(adev, tap);
}

static void fm_apply_routes_l(struct audio_device *adev)
{
    const uint32_t play = (adev->fm_patch_handle != AUDIO_PATCH_HANDLE_NONE)
                          ? adev->fm_devices | AUDIO_DEVICE_OUT_FM : 0;
    const uint32_t capture = (adev->fm_captures > 0) ? AUDIO_DEVICE_OUT_FM : 0;

    const char *tap = fm_tap_for_l(adev, NULL, 0);

    /* DAC1 and the tap leave the mixers before the routes, join after */
    if (!fm_chain_on_l(adev)) {
        fm_apply_gain_l(adev);
    }
    fm_leave_tap_l(adev, tap);
    apply_route(adev->fm_stream, play);
    apply_route(adev->fm_capture_stream, capture);
    if (fm_chain_on_l(adev)) {
        fm_apply_gain_l(adev);
    }
    fm_join_tap_l(adev, tap);
}

static void fm_set_tuner_input_l(struct audio_device *adev,
                                 struct stream_in_common *in, bool on)
{
    if (in->on_tuner == on || adev->fm_capture_stream == NULL) {
        return;
    }

    in->on_tuner = on;
    adev->fm_captures += on ? 1 : -1;
    fm_apply_routes_l(adev);
}

static void fm_release_patch_l(struct audio_device *adev)
{
    adev->fm_patch_handle = AUDIO_PATCH_HANDLE_NONE;
    adev->fm_devices = 0;
    fm_apply_routes_l(adev);
}

/*********************************************************************
 * Audio patches
 *
 * From AUDIO_DEVICE_API_VERSION_3_0 on, AudioFlinger routes streams with
 * audio patches instead of "routing" and "input_source" parameters: a
 * patch from a stream's mix to the devices it plays on, or from the device
 * a stream captures from to its mix, carrying the input source. A new
 * route for the stream comes as a patch with the handle of the one it
 * replaces, and the release of a patch takes the stream off its devices,
 * as "routing=0" did. The one patch from a device to devices is FM's,
 * from the tuner (see "FM radio" above).
 *
 * A stream holds the handle of the patch that routes it, the device the
 * FM patch's; nothing else keeps patches. The handles and the stream
 * lists are under the device lock, which is taken before an input's lock
 * and never under one. AudioFlinger waits for these calls from the
 * stream's thread, so they neither sleep nor touch the PCM.
 *********************************************************************/

static struct stream_out_common *find_output_l(struct audio_device *adev,
                                               audio_io_handle_t io_handle)
{
    struct listnode *node;

    list_for_each(node, &adev->outputs) {
        struct stream_out_common *out =
                node_to_item(node, struct stream_out_common, node);
        if (out->io_handle == io_handle) {
            return out;
        }
    }
    return NULL;
}

static struct stream_in_common *find_input_l(struct audio_device *adev,
                                             audio_io_handle_t io_handle)
{
    struct listnode *node;

    list_for_each(node, &adev->inputs) {
        struct stream_in_common *in =
                node_to_item(node, struct stream_in_common, node);
        if (in->io_handle == io_handle) {
            return in;
        }
    }
    return NULL;
}

/*
 * What the patch routes, if this HAL has it: one of *out and *in is set
 * for a stream's patch, neither for the FM patch.
 */
static bool find_patch_l(struct audio_device *adev, audio_patch_handle_t handle,
                         struct stream_out_common **out,
                         struct stream_in_common **in)
{
    struct listnode *node;

    *out = NULL;
    *in = NULL;

    if (handle == AUDIO_PATCH_HANDLE_NONE) {
        return false;
    }

    if (handle == adev->fm_patch_handle) {
        return true;
    }

    list_for_each(node, &adev->outputs) {
        struct stream_out_common *o =
                node_to_item(node, struct stream_out_common, node);
        if (o->patch_handle == handle) {
            *out = o;
            return true;
        }
    }
    list_for_each(node, &adev->inputs) {
        struct stream_in_common *i =
                node_to_item(node, struct stream_in_common, node);
        if (i->patch_handle == handle) {
            *in = i;
            return true;
        }
    }
    return false;
}

static audio_patch_handle_t new_patch_handle_l(struct audio_device *adev)
{
    /* Never AUDIO_PATCH_HANDLE_NONE, nor negative after a wrap */
    if (adev->last_patch_handle <= AUDIO_PATCH_HANDLE_NONE ||
            adev->last_patch_handle == INT32_MAX) {
        adev->last_patch_handle = AUDIO_PATCH_HANDLE_NONE;
    }
    return ++adev->last_patch_handle;
}

/*
 * Takes the handle for a patch to route out, in, or FM (both NULL): the
 * one given back to update, moved from whatever had it -- which keeps its
 * route, as a route set on one stream never undid another's, but for FM,
 * whose patch it was -- or a new one for a handle this HAL does not have
 * (one it never gave, or another module's after AudioFlinger moved the
 * patch). A patch the stream had and AudioFlinger dropped without a
 * release is simply forgotten.
 */
static void take_patch_handle_l(struct audio_device *adev,
                                audio_patch_handle_t *handle,
                                struct stream_out_common *out,
                                struct stream_in_common *in)
{
    struct stream_out_common *old_out;
    struct stream_in_common *old_in;

    if (find_patch_l(adev, *handle, &old_out, &old_in)) {
        if (old_out != NULL) {
            if (old_out != out) {
                old_out->patch_handle = AUDIO_PATCH_HANDLE_NONE;
            }
        } else if (old_in != NULL) {
            if (old_in != in) {
                old_in->patch_handle = AUDIO_PATCH_HANDLE_NONE;
            }
        } else if (out != NULL || in != NULL) {
            fm_release_patch_l(adev);
        }
    } else {
        if (*handle != AUDIO_PATCH_HANDLE_NONE) {
            ALOGW("%s: unknown patch %d, giving a new handle", __func__, *handle);
        }
        *handle = new_patch_handle_l(adev);
    }

    if (out != NULL) {
        out->patch_handle = *handle;
    } else if (in != NULL) {
        in->patch_handle = *handle;
    } else {
        adev->fm_patch_handle = *handle;
    }
}

static int create_fm_patch(struct audio_device *adev,
                           const struct audio_port_config *source,
                           unsigned int num_sinks,
                           const struct audio_port_config *sinks,
                           audio_patch_handle_t *handle)
{
    uint32_t devices = 0;
    unsigned int i;

    if (source->ext.device.type != AUDIO_DEVICE_IN_FM_TUNER) {
        ALOGW("%s: no patch from device 0x%x to devices", __func__,
              source->ext.device.type);
        return -ENOSYS;
    }

    for (i = 0; i < num_sinks; i++) {
        if (sinks[i].type != AUDIO_PORT_TYPE_DEVICE ||
                (sinks[i].ext.device.type & ~FM_PATCH_SINKS) != 0) {
            ALOGE("%s: FM can't play on sink %u (device 0x%x)", __func__, i,
                  sinks[i].type == AUDIO_PORT_TYPE_DEVICE
                          ? sinks[i].ext.device.type : 0);
            return -EINVAL;
        }
        devices |= sinks[i].ext.device.type;
    }

    pthread_mutex_lock(&adev->lock);

    if (adev->fm_stream == NULL || adev->fm_capture_stream == NULL) {
        pthread_mutex_unlock(&adev->lock);
        ALOGE("%s: no fm streams in the config", __func__);
        return -ENOSYS;
    }

    take_patch_handle_l(adev, handle, NULL, NULL);
    adev->fm_devices = devices;
    fm_apply_routes_l(adev);

    ALOGV("%s: patch %d: FM on devices 0x%x", __func__, *handle, devices);

    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int adev_create_audio_patch(struct audio_hw_device *dev,
                                   unsigned int num_sources,
                                   const struct audio_port_config *sources,
                                   unsigned int num_sinks,
                                   const struct audio_port_config *sinks,
                                   audio_patch_handle_t *handle)
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct stream_out_common *out = NULL;
    struct stream_in_common *in = NULL;
    audio_io_handle_t io_handle;
    audio_source_t source = AUDIO_SOURCE_DEFAULT;
    uint32_t devices = 0;
    unsigned int i;
    int ret = 0;

    if (sources == NULL || sinks == NULL || handle == NULL ||
            num_sources != 1 || num_sinks == 0 ||
            num_sinks > AUDIO_PATCH_PORTS_MAX) {
        ALOGE("%s: %u sources, %u sinks", __func__, num_sources, num_sinks);
        return -EINVAL;
    }

    if (sources[0].type == AUDIO_PORT_TYPE_MIX) {
        /* Playback: the stream plays on every sink */
        for (i = 0; i < num_sinks; i++) {
            if (sinks[i].type != AUDIO_PORT_TYPE_DEVICE) {
                ALOGE("%s: sink %u of a playback patch is not a device",
                      __func__, i);
                return -EINVAL;
            }
            devices |= sinks[i].ext.device.type;
        }
        io_handle = sources[0].ext.mix.handle;
    } else if (sources[0].type == AUDIO_PORT_TYPE_DEVICE &&
               sinks[0].type == AUDIO_PORT_TYPE_MIX) {
        /* Capture: one device into one stream, with its input source */
        if (num_sinks != 1) {
            ALOGE("%s: capture patch with %u sinks", __func__, num_sinks);
            return -EINVAL;
        }
        devices = sources[0].ext.device.type;
        io_handle = sinks[0].ext.mix.handle;
        source = sinks[0].ext.mix.usecase.source;
    } else if (sources[0].type == AUDIO_PORT_TYPE_DEVICE) {
        return create_fm_patch(adev, &sources[0], num_sinks, sinks, handle);
    } else {
        ALOGE("%s: source of type %d", __func__, sources[0].type);
        return -EINVAL;
    }

    pthread_mutex_lock(&adev->lock);

    if (sources[0].type == AUDIO_PORT_TYPE_MIX) {
        out = find_output_l(adev, io_handle);
    } else {
        in = find_input_l(adev, io_handle);
    }
    if (out == NULL && in == NULL) {
        ALOGE("%s: no stream with io handle %d", __func__, io_handle);
        ret = -EINVAL;
        goto exit;
    }

    if (out != NULL) {
        route_output_l(adev, out, devices);
    } else {
        pthread_mutex_lock(&in->lock);
        ret = route_input_locked((struct stream_in_pcm *)in, source, devices);
        pthread_mutex_unlock(&in->lock);
        if (ret < 0) {
            ALOGE("%s: io handle %d: source %d devices 0x%x: %d", __func__,
                  io_handle, source, devices, ret);
            goto exit;
        }
        fm_set_tuner_input_l(adev, in, devices == AUDIO_DEVICE_IN_FM_TUNER);
    }

    take_patch_handle_l(adev, handle, out, in);

    ALOGV("%s: patch %d: %s io handle %d devices 0x%x source %d", __func__,
          *handle, out != NULL ? "output" : "input", io_handle, devices, source);

exit:
    pthread_mutex_unlock(&adev->lock);
    return ret;
}

static int adev_release_audio_patch(struct audio_hw_device *dev,
                                    audio_patch_handle_t handle)
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct stream_out_common *out;
    struct stream_in_common *in;

    pthread_mutex_lock(&adev->lock);

    if (!find_patch_l(adev, handle, &out, &in)) {
        /* Its stream is gone, or a newer patch took its place */
        ALOGW("%s: unknown patch %d", __func__, handle);
        pthread_mutex_unlock(&adev->lock);
        return 0;
    }

    if (out != NULL) {
        route_output_l(adev, out, 0);
        out->patch_handle = AUDIO_PATCH_HANDLE_NONE;
    } else if (in != NULL) {
        pthread_mutex_lock(&in->lock);
        in->devices = 0;
        if (in->hw != NULL) {
            apply_route(in->hw, 0);
        }
        pthread_mutex_unlock(&in->lock);
        in->patch_handle = AUDIO_PATCH_HANDLE_NONE;
        fm_set_tuner_input_l(adev, in, false);
    } else {
        fm_release_patch_l(adev);
    }

    ALOGV("%s: patch %d", __func__, handle);

    pthread_mutex_unlock(&adev->lock);
    return 0;
}

/* The HIDL wrapper calls this whatever the version, so it must be set */
static int adev_get_audio_port(struct audio_hw_device *dev,
                               struct audio_port *port)
{
    return -ENOSYS;
}

/*
 * AudioPolicy sends only gains here (AudioPolicyManager::
 * setAudioPortConfig), and of the ports only the FM tuner has one: the
 * volume of FM, joint for both channels, as the policy declares it.
 */
static int adev_set_audio_port_config(struct audio_hw_device *dev,
                                      const struct audio_port_config *config)
{
    struct audio_device *adev = (struct audio_device *)dev;

    if (config == NULL) {
        return -EINVAL;
    }

    if (config->type != AUDIO_PORT_TYPE_DEVICE ||
            config->role != AUDIO_PORT_ROLE_SOURCE ||
            config->ext.device.type != AUDIO_DEVICE_IN_FM_TUNER) {
        ALOGW("%s: port %d has no config to set", __func__, config->id);
        return -ENOSYS;
    }

    if (!(config->config_mask & AUDIO_PORT_CONFIG_GAIN) ||
            !(config->gain.mode & AUDIO_GAIN_MODE_JOINT)) {
        ALOGE("%s: FM tuner config mask 0x%x gain mode 0x%x", __func__,
              config->config_mask, config->gain.mode);
        return -EINVAL;
    }

    pthread_mutex_lock(&adev->lock);

    if (adev->fm_stream == NULL) {
        pthread_mutex_unlock(&adev->lock);
        ALOGE("%s: no fm stream in the config", __func__);
        return -ENOSYS;
    }

    adev->fm_gain_mb = config->gain.values[0];
    adev->fm_gain_set = true;
    fm_apply_gain_l(adev);

    ALOGV("%s: FM gain %ld mB", __func__, adev->fm_gain_mb);

    pthread_mutex_unlock(&adev->lock);
    return 0;
}

/*********************************************************************
 * Global API functions
 *********************************************************************/
static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    struct audio_device *adev = (struct audio_device *)dev;

    ALOGW("adev_set_parameters '%s'", kvpairs);

    if (adev->global_stream != NULL) {
        stream_invoke_usecases(adev->global_stream, kvpairs);
    }

    return 0;
}

static char *adev_get_parameters(const struct audio_hw_device *dev,
                                 const char *keys)
{
    return strdup("");
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    return -ENOSYS;
}

static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    struct audio_device *adev = (struct audio_device *)dev;

    adev->mic_mute = state;

    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    struct audio_device *adev = (struct audio_device *)dev;

    *state = adev->mic_mute;

    return 0;
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev,
                                         const struct audio_config *config)
{
    size_t s = IN_PERIOD_SIZE_DEFAULT *
               audio_bytes_per_sample(config->format) *
               audio_channel_count_from_in_mask(config->channel_mask);

    if (s > IN_PCM_BUFFER_SIZE_DEFAULT) {
        s = IN_PCM_BUFFER_SIZE_DEFAULT;
    }

    return s;
}

static int adev_dump(const audio_hw_device_t *device, int fd)
{
    return 0;
}

static int adev_close(hw_device_t *device)
{
    struct audio_device *adev = (struct audio_device *)device;

    /* What FM's use-cases closed, opened before their streams go */
    if (adev->fm_stream != NULL) {
        fm_leave_tap_l(adev, NULL);
        apply_use_case(adev->fm_stream, "mute", "on");
        release_stream(adev->fm_stream);
        release_stream(adev->fm_capture_stream);
    }
    if (adev->global_stream != NULL) {
        release_stream(adev->global_stream);
    }

    free_audio_config(adev->cm);

    free(device);
    return 0;
}

static int adev_open(const hw_module_t *module, const char *name,
                     hw_device_t **device)
{
    struct audio_device *adev;
    char file_name[80];
    char property[PROPERTY_VALUE_MAX];
    int ret;

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0) {
        return -EINVAL;
    }

    adev = calloc(1, sizeof(struct audio_device));
    if (!adev) {
        return -ENOMEM;
    }

    list_init(&adev->pcm_outputs);
    list_init(&adev->outputs);
    list_init(&adev->inputs);
    adev->last_patch_handle = AUDIO_PATCH_HANDLE_NONE;

    adev->hw_device.common.tag = HARDWARE_DEVICE_TAG;
    adev->hw_device.common.version = AUDIO_DEVICE_API_VERSION_3_0;
    adev->hw_device.common.module = (struct hw_module_t *) module;
    adev->hw_device.common.close = adev_close;

    adev->hw_device.init_check = adev_init_check;
    adev->hw_device.set_voice_volume = adev_set_voice_volume;
    adev->hw_device.set_master_volume = adev_set_master_volume;
    adev->hw_device.set_mode = adev_set_mode;
    adev->hw_device.set_mic_mute = adev_set_mic_mute;
    adev->hw_device.get_mic_mute = adev_get_mic_mute;
    adev->hw_device.set_parameters = adev_set_parameters;
    adev->hw_device.get_parameters = adev_get_parameters;
    adev->hw_device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->hw_device.open_output_stream = adev_open_output_stream;
    adev->hw_device.close_output_stream = adev_close_output_stream;
    adev->hw_device.open_input_stream = adev_open_input_stream;
    adev->hw_device.close_input_stream = adev_close_input_stream;
    adev->hw_device.create_audio_patch = adev_create_audio_patch;
    adev->hw_device.release_audio_patch = adev_release_audio_patch;
    adev->hw_device.get_audio_port = adev_get_audio_port;
    adev->hw_device.set_audio_port_config = adev_set_audio_port_config;
    adev->hw_device.dump = adev_dump;

    property_get("ro.product.device", property, "generic");
    snprintf(file_name, sizeof(file_name), "%s/audio.%s.xml", ETC_PATH, property);

    ALOGV("Reading configuration from %s\n", file_name);
    adev->cm = init_audio_config(file_name);
    if (!adev->cm) {
        ret = -errno;
        ALOGE("Failed to open config file %s (%d)", file_name, ret);
        goto fail;
    }

    /* Optional: the use-cases adev_set_parameters() keys select */
    if (is_named_stream_defined(adev->cm, "global")) {
        adev->global_stream = get_named_stream(adev->cm, "global");
    }

    /* FM needs both its streams, and its volume's scale to know a gain
     * below it */
    adev->fm_patch_handle = AUDIO_PATCH_HANDLE_NONE;
    adev->fm_stream = get_named_stream(adev->cm, "fm");
    adev->fm_capture_stream = get_named_stream(adev->cm, "fm capture");
    if (adev->fm_stream == NULL || adev->fm_capture_stream == NULL) {
        ALOGE("No fm and fm capture streams in the config: no FM");
        if (adev->fm_stream != NULL) {
            release_stream(adev->fm_stream);
        }
        if (adev->fm_capture_stream != NULL) {
            release_stream(adev->fm_capture_stream);
        }
        adev->fm_stream = NULL;
        adev->fm_capture_stream = NULL;
    } else {
        long max_mb;

        ret = get_hw_volume_mb_range(adev->fm_stream, &adev->fm_min_mb, &max_mb);
        if (ret < 0) {
            ALOGE("No dB scale for the fm stream's volume (%d): FM never mutes", ret);
            adev->fm_min_mb = LONG_MIN;
        }
    }

    *device = &adev->hw_device.common;

    return 0;

fail:
    if (adev->cm) {
        free_audio_config(adev->cm);
    }

    free(adev);
    return ret;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "TinyHAL",
        .author = "Richard Fitzgerald <rf@opensource.wolfsonmicro.com>",
        .methods = &hal_module_methods,
    },
};
