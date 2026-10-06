/*
 * Copyright (C) 2026 Artem Bambalov
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

/*
 * The codec's echo cancelling and noise suppression, offered to apps as
 * the platform's AcousticEchoCanceler and NoiseSuppressor.
 *
 * The work is the RT5671's voice DSP's: this library processes nothing.
 * Its effects are flagged as hardware tunnelled and as doing no
 * processing, so AudioFlinger never hands them a buffer, and gives each
 * one, once enabled, to the input stream it is on; the HAL reads the
 * effect's type and records through the DSP mode it asks for. Disabling
 * one takes it back off the stream, and the HAL records without it.
 *
 * Qualcomm's libqcomvoiceprocessing works the same way; the effect types
 * are the platform's, the implementations this library's own.
 */

#define LOG_TAG "mocha_voice_processing"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <log/log.h>
#include <hardware/audio_effect.h>

#define VP_FLAGS (EFFECT_FLAG_TYPE_PRE_PROC | EFFECT_FLAG_DEVICE_IND | \
                  EFFECT_FLAG_HW_ACC_TUNNEL | EFFECT_FLAG_NO_PROCESS)

static const effect_descriptor_t aec_descriptor = {
    /* type: SL_IID_ANDROIDACOUSTICECHOCANCELLATION */
    { 0x7b491460, 0x8d4d, 0x11e0, 0xbd61, { 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b } },
    { 0xa26b33f6, 0x39f2, 0x4a7d, 0x88d7, { 0x0a, 0x9e, 0xff, 0x77, 0x4b, 0x22 } },
    EFFECT_CONTROL_API_VERSION,
    VP_FLAGS,
    0,
    0,
    "Acoustic Echo Canceler",
    "Realtek RT5671 voice DSP",
};

static const effect_descriptor_t ns_descriptor = {
    /* type: SL_IID_ANDROIDNOISESUPPRESSION */
    { 0x58b4b260, 0x8e06, 0x11e0, 0xaa8e, { 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b } },
    { 0x62d49dfd, 0x1d6c, 0x4f23, 0x8e23, { 0xa5, 0x98, 0x5f, 0x99, 0xe9, 0x0a } },
    EFFECT_CONTROL_API_VERSION,
    VP_FLAGS,
    0,
    0,
    "Noise Suppression",
    "Realtek RT5671 voice DSP",
};

static const effect_descriptor_t *descriptors[] = {
    &aec_descriptor,
    &ns_descriptor,
};

#define NUM_DESCRIPTORS (sizeof(descriptors) / sizeof(descriptors[0]))

struct vp_effect {
    /* First: the effect_handle_t AudioFlinger and the HAL hold points here */
    const struct effect_interface_s *itfe;
    const effect_descriptor_t *descriptor;
    effect_config_t config;
    bool enabled;
};

static const effect_descriptor_t *find_descriptor(const effect_uuid_t *uuid)
{
    size_t i;

    for (i = 0; i < NUM_DESCRIPTORS; i++) {
        if (memcmp(&descriptors[i]->uuid, uuid, sizeof(*uuid)) == 0) {
            return descriptors[i];
        }
    }
    return NULL;
}

static int vp_process(effect_handle_t self, audio_buffer_t *in,
                      audio_buffer_t *out)
{
    /* Flagged as doing no processing: AudioFlinger does not call this */
    return -ENODATA;
}

static int reply_status(uint32_t *size, void *data, int status)
{
    if (data == NULL || size == NULL || *size < sizeof(int)) {
        return -EINVAL;
    }
    *(int *)data = status;
    *size = sizeof(int);
    return 0;
}

static int vp_command(effect_handle_t self, uint32_t cmd, uint32_t cmd_size,
                      void *cmd_data, uint32_t *reply_size, void *reply_data)
{
    struct vp_effect *effect = (struct vp_effect *)self;

    if (effect == NULL) {
        return -EINVAL;
    }

    switch (cmd) {
    case EFFECT_CMD_INIT:
    case EFFECT_CMD_SET_PARAM:
        return reply_status(reply_size, reply_data, 0);

    case EFFECT_CMD_SET_CONFIG:
        if (cmd_data == NULL || cmd_size != sizeof(effect_config_t)) {
            return -EINVAL;
        }
        effect->config = *(effect_config_t *)cmd_data;
        return reply_status(reply_size, reply_data, 0);

    case EFFECT_CMD_GET_CONFIG:
        if (reply_data == NULL || reply_size == NULL ||
                *reply_size < sizeof(effect_config_t)) {
            return -EINVAL;
        }
        *(effect_config_t *)reply_data = effect->config;
        *reply_size = sizeof(effect_config_t);
        return 0;

    case EFFECT_CMD_RESET:
        return 0;

    case EFFECT_CMD_ENABLE:
    case EFFECT_CMD_DISABLE:
        effect->enabled = cmd == EFFECT_CMD_ENABLE;
        ALOGV("%s %s", effect->descriptor->name,
              effect->enabled ? "enabled" : "disabled");
        return reply_status(reply_size, reply_data, 0);

    case EFFECT_CMD_GET_PARAM: {
        /* The DSP's settings are the kernel's tables; none are offered */
        effect_param_t *p = (effect_param_t *)reply_data;

        if (cmd_data == NULL || reply_data == NULL || reply_size == NULL ||
                cmd_size < sizeof(effect_param_t) ||
                *reply_size < sizeof(effect_param_t)) {
            return -EINVAL;
        }
        memcpy(reply_data, cmd_data, sizeof(effect_param_t) +
               ((effect_param_t *)cmd_data)->psize);
        p->status = -ENOSYS;
        p->vsize = 0;
        *reply_size = sizeof(effect_param_t);
        return 0;
    }

    case EFFECT_CMD_SET_DEVICE:
    case EFFECT_CMD_SET_INPUT_DEVICE:
    case EFFECT_CMD_SET_VOLUME:
    case EFFECT_CMD_SET_AUDIO_MODE:
    case EFFECT_CMD_SET_AUDIO_SOURCE:
        return 0;

    default:
        return -EINVAL;
    }
}

static int vp_get_descriptor(effect_handle_t self,
                             effect_descriptor_t *descriptor)
{
    struct vp_effect *effect = (struct vp_effect *)self;

    if (effect == NULL || descriptor == NULL) {
        return -EINVAL;
    }
    *descriptor = *effect->descriptor;
    return 0;
}

static const struct effect_interface_s vp_interface = {
    vp_process,
    vp_command,
    vp_get_descriptor,
    NULL,
};

static int lib_create(const effect_uuid_t *uuid, int32_t session_id,
                      int32_t io_id, effect_handle_t *handle)
{
    const effect_descriptor_t *descriptor;
    struct vp_effect *effect;

    if (uuid == NULL || handle == NULL) {
        return -EINVAL;
    }

    descriptor = find_descriptor(uuid);
    if (descriptor == NULL) {
        return -ENOENT;
    }

    effect = calloc(1, sizeof(*effect));
    if (effect == NULL) {
        return -ENOMEM;
    }

    effect->itfe = &vp_interface;
    effect->descriptor = descriptor;
    *handle = (effect_handle_t)effect;

    ALOGV("%s created, session %d io %d", descriptor->name, session_id, io_id);
    return 0;
}

static int lib_release(effect_handle_t handle)
{
    if (handle == NULL) {
        return -EINVAL;
    }
    free(handle);
    return 0;
}

static int lib_get_descriptor(const effect_uuid_t *uuid,
                              effect_descriptor_t *descriptor)
{
    const effect_descriptor_t *found;

    if (uuid == NULL || descriptor == NULL) {
        return -EINVAL;
    }

    found = find_descriptor(uuid);
    if (found == NULL) {
        return -EINVAL;
    }
    *descriptor = *found;
    return 0;
}

__attribute__((visibility("default")))
audio_effect_library_t AUDIO_EFFECT_LIBRARY_INFO_SYM = {
    .tag = AUDIO_EFFECT_LIBRARY_TAG,
    .version = EFFECT_LIBRARY_API_VERSION,
    .name = "Mocha voice processing",
    .implementor = "Artem Bambalov",
    .create_effect = lib_create,
    .release_effect = lib_release,
    .get_descriptor = lib_get_descriptor,
};
