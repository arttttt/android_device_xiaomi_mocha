/* mixer.c
**
** Copyright 2011, The Android Open Source Project
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * Neither the name of The Android Open Source Project nor the names of
**       its contributors may be used to endorse or promote products derived
**       from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY The Android Open Source Project ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL The Android Open Source Project BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
** DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>
#include <poll.h>
#include <math.h>

#include <sys/ioctl.h>

#include <linux/ioctl.h>

#ifndef __force
#define __force
#endif

#ifndef __bitwise
#define __bitwise
#endif

#ifndef __user
#define __user
#endif

#include <sound/asound.h>

#include <tinyalsa/mixer.h>
#include <tinyalsa/plugin.h>

#include "mixer_io.h"

/** A mixer control.
 * @ingroup libtinyalsa-mixer
 */
struct mixer_ctl {
    /** The mixer that the mixer control belongs to */
    struct mixer *mixer;
    /** Information on the control's value (i.e. type, number of values) */
    struct snd_ctl_elem_info info;
    /** A list of string representations of enumerated values (only valid for enumerated controls) */
    char **ename;
    /** Pointer to the group that the control belongs to */
    struct mixer_ctl_group *grp;
};

struct mixer_ctl_group {
    /** A continuous array of mixer controls */
    struct mixer_ctl *ctl;
    /** The number of mixer controls */
    unsigned int count;
    /** The number of events associated with this group */
    unsigned int event_cnt;
    /** The operations corresponding to this group */
    const struct mixer_ops *ops;
    /** Private data for storing group specific data */
    void *data;
};

/** A mixer handle.
 * @ingroup libtinyalsa-mixer
 */
struct mixer {
    /** File descriptor for the card */
    int fd;
    /** Card information */
    struct snd_ctl_card_info card_info;
    /* Hardware (kernel interface) mixer control group */
    struct mixer_ctl_group *h_grp;
    /* Virtual (Plugin interface) mixer control group */
    struct mixer_ctl_group *v_grp;
    /* Total count of mixer controls from both  groups */
    unsigned int total_count;
    /* Flag to track if card information is already retrieved */
    bool is_card_info_retrieved;

};

static void mixer_cleanup_control(struct mixer_ctl *ctl)
{
    unsigned int m;

    if (ctl->ename) {
        unsigned int max = ctl->info.value.enumerated.items;
        for (m = 0; m < max; m++)
            free(ctl->ename[m]);
        free(ctl->ename);
    }
}

static void mixer_grp_close(struct mixer *mixer, struct mixer_ctl_group *grp)
{
    unsigned int n;

    if (!grp)
        return;

    if (grp->ctl) {
        for (n = 0; n < grp->count; n++)
            mixer_cleanup_control(&grp->ctl[n]);
        free(grp->ctl);
    }

    free(grp);

    mixer->is_card_info_retrieved = false;
}

/** Closes a mixer returned by @ref mixer_open.
 * @param mixer A mixer handle.
 * @ingroup libtinyalsa-mixer
 */
void mixer_close(struct mixer *mixer)
{
    if (!mixer)
        return;

    if (mixer->fd >= 0 && mixer->h_grp)
        mixer->h_grp->ops->close(mixer->h_grp->data);
    mixer_grp_close(mixer, mixer->h_grp);

#ifdef TINYALSA_USES_PLUGINS
    if (mixer->v_grp)
        mixer->v_grp->ops->close(mixer->v_grp->data);
    mixer_grp_close(mixer, mixer->v_grp);
#endif

    free(mixer);

    /* TODO: verify frees */
}

static void *mixer_realloc_z(void *ptr, size_t curnum, size_t newnum, size_t size)
{
        int8_t *newp;

        newp = realloc(ptr, size * newnum);
        if (!newp)
            return NULL;

        memset(newp + (curnum * size), 0, (newnum - curnum) * size);
        return newp;
}

static int add_controls(struct mixer *mixer, struct mixer_ctl_group *grp)
{
    struct snd_ctl_elem_list elist;
    struct snd_ctl_elem_id *eid = NULL;
    struct mixer_ctl *ctl;
    const unsigned int old_count = grp->count;
    unsigned int new_count;
    unsigned int n;

    memset(&elist, 0, sizeof(elist));
    if (grp->ops->ioctl(grp->data, SNDRV_CTL_IOCTL_ELEM_LIST, &elist) < 0)
        goto fail;

    if (old_count == elist.count)
        return 0; /* no new controls return unchanged */

    if (old_count > elist.count)
        return -1; /* driver has removed controls - this is bad */

    ctl = mixer_realloc_z(grp->ctl, old_count, elist.count,
                          sizeof(struct mixer_ctl));
    if (!ctl)
        goto fail;

    grp->ctl = ctl;

    /* ALSA drivers are not supposed to remove or re-order controls that
     * have already been created so we know that any new controls must
     * be after the ones we have already collected
     */
    new_count = elist.count;
    elist.space = new_count - old_count; /* controls we haven't seen before */
    elist.offset = old_count; /* first control we haven't seen */

    eid = calloc(elist.space, sizeof(struct snd_ctl_elem_id));
    if (!eid)
        goto fail;

    elist.pids = eid;

    if (grp->ops->ioctl(grp->data, SNDRV_CTL_IOCTL_ELEM_LIST, &elist) < 0)
        goto fail;

    for (n = old_count; n < new_count; n++) {
        struct snd_ctl_elem_info *ei = &grp->ctl[n].info;
        ei->id.numid = eid[n - old_count].numid;
        if (grp->ops->ioctl(grp->data, SNDRV_CTL_IOCTL_ELEM_INFO, ei) < 0)
            goto fail_extend;
        ctl[n].mixer = mixer;
        ctl[n].grp = grp;
    }

    grp->count = new_count;
    mixer->total_count += (new_count - old_count);

    free(eid);
    return 0;

fail_extend:
    /* cleanup the control we failed on but leave the ones that were already
     * added. Also no advantage to shrinking the resized memory block, we
     * might want to extend the controls again later
     */
    mixer_cleanup_control(&ctl[n]);

    grp->count = n;   /* keep controls we successfully added */
    mixer->total_count += (n - old_count);
    /* fall through... */
fail:
    free(eid);
    return -1;
}

static int mixer_grp_open(struct mixer *mixer, unsigned int card, bool is_hw)
{
    struct mixer_ctl_group *grp = NULL;
    const struct mixer_ops *ops = NULL;
    void *data = NULL;
    int fd, ret;

    grp = calloc(1, sizeof(*grp));
    if (!grp)
        return -ENOMEM;

    if (is_hw) {
        mixer->fd = -1;
        fd = mixer_hw_open(card, &data, &ops);
        if (fd < 0) {
            ret = fd;
            goto err_open;
        }
        mixer->fd = fd;
        mixer->h_grp = grp;
    }
#ifdef TINYALSA_USES_PLUGINS
    else {
        ret = mixer_plugin_open(card, &data, &ops);
        if (ret < 0)
            goto err_open;
        mixer->v_grp = grp;
    }
#endif
    grp->ops = ops;
    grp->data = data;

    if (!mixer->is_card_info_retrieved) {
        ret = grp->ops->ioctl(data, SNDRV_CTL_IOCTL_CARD_INFO,
                              &mixer->card_info);
        if (ret < 0)
            goto err_card_info;
        mixer->is_card_info_retrieved = true;
    }

    ret = add_controls(mixer, grp);
    if (ret < 0)
        goto err_card_info;

    return 0;

err_card_info:
    grp->ops->close(grp->data);

err_open:
    free(grp);
    return ret;

}

/** Opens a mixer for a given card.
 * @param card The card to open the mixer for.
 * @returns An initialized mixer handle.
 * @ingroup libtinyalsa-mixer
 */
struct mixer *mixer_open(unsigned int card)
{
    struct mixer *mixer = NULL;
    int h_status, v_status = -1;

    mixer = calloc(1, sizeof(*mixer));
    if (!mixer)
        goto fail;

    h_status = mixer_grp_open(mixer, card, true);

#ifdef TINYALSA_USES_PLUGINS
    v_status = mixer_grp_open(mixer, card, false);
#endif

    /* both hw and virtual should fail for mixer_open to fail */
    if (h_status < 0 && v_status < 0)
        goto fail;

    return mixer;

fail:
    if (mixer)
        mixer_close(mixer);

    return NULL;
}

/** Some controls may not be present at boot time, e.g. controls from runtime
 * loadable DSP firmware. This function adds any new controls that have appeared
 * since mixer_open() or the last call to this function. This assumes a well-
 * behaved codec driver that does not delete controls that already exists, so
 * any added controls must be after the last one we already saw. Scanning only
 * the new controls is much faster than calling mixer_close() then mixer_open()
 * to re-scan all controls.
 *
 * NOTE: this invalidates any struct mixer_ctl pointers previously obtained
 * from mixer_get_ctl() and mixer_get_ctl_by_name(). Either refresh all your
 * stored pointers after calling mixer_update_ctls(), or (better) do not
 * store struct mixer_ctl pointers, instead lookup the control by name or
 * id only when you are about to use it. The overhead of lookup by id
 * using mixer_get_ctl() is negligible.
 * @param mixer An initialized mixer handle.
 * @returns 0 on success, -1 on failure
 */
int mixer_add_new_ctls(struct mixer *mixer)
{
    int rc1 = 0, rc2 = 0;

    if (!mixer)
        return 0;

    /* add the h_grp controls */
    if (mixer->h_grp)
        rc1 = add_controls(mixer, mixer->h_grp);

#ifdef TINYALSA_USES_PLUGINS
    /* add the v_grp controls */
    if (mixer->v_grp)
        rc2 = add_controls(mixer, mixer->v_grp);
#endif

    if (rc1 < 0)
        return rc1;
    if (rc2 < 0)
        return rc2;

    return 0;
}

/** Gets the name of the mixer's card.
 * @param mixer An initialized mixer handle.
 * @returns The name of the mixer's card.
 * @ingroup libtinyalsa-mixer
 */
const char *mixer_get_name(const struct mixer *mixer)
{
    if (!mixer) {
        return NULL;
    }

    return (const char *)mixer->card_info.name;
}

/** Gets the number of mixer controls for a given mixer.
 * @param mixer An initialized mixer handle.
 * @returns The number of mixer controls for the given mixer.
 * @ingroup libtinyalsa-mixer
 */
unsigned int mixer_get_num_ctls(const struct mixer *mixer)
{
    if (!mixer)
        return 0;

    return mixer->total_count;
}

/** Gets the number of mixer controls, that go by a specified name, for a given mixer.
 * @param mixer An initialized mixer handle.
 * @param name The name of the mixer control
 * @returns The number of mixer controls, specified by @p name, for the given mixer.
 * @ingroup libtinyalsa-mixer
 */
unsigned int mixer_get_num_ctls_by_name(const struct mixer *mixer, const char *name)
{
    struct mixer_ctl_group *grp;
    unsigned int n;
    unsigned int count = 0;
    struct mixer_ctl *ctl;

    if (!mixer || !name) {
        return 0;
    }

    if (mixer->h_grp) {
        grp = mixer->h_grp;
        ctl = grp->ctl;

        for (n = 0; n < grp->count; n++)
            if (!strcmp(name, (char*) ctl[n].info.id.name))
                count++;
    }
#ifdef TINYALSA_USES_PLUGINS
    if (mixer->v_grp) {
        grp = mixer->v_grp;
        ctl = grp->ctl;

        for (n = 0; n < grp->count; n++)
            if (!strcmp(name, (char*) ctl[n].info.id.name))
                count++;
    }
#endif

    return count;
}

/** Subscribes for the mixer events.
 * @param mixer A mixer handle.
 * @param subscribe value indicating subscribe or unsubscribe for events
 * @returns On success, zero.
 *  On failure, non-zero.
 * @ingroup libtinyalsa-mixer
 */
int mixer_subscribe_events(struct mixer *mixer, int subscribe)
{
    struct mixer_ctl_group *grp;

    if (!mixer) {
        return -EINVAL;
    }

    if (mixer->h_grp) {
        grp = mixer->h_grp;
        if (grp->ops->ioctl(grp->data, SNDRV_CTL_IOCTL_SUBSCRIBE_EVENTS, &subscribe) < 0)
            return -1;
    }

#ifdef TINYALSA_USES_PLUGINS
    if (mixer->v_grp) {
        grp = mixer->v_grp;
        if (grp->ops->ioctl(grp->data, SNDRV_CTL_IOCTL_SUBSCRIBE_EVENTS, &subscribe) < 0)
            return -1;
    }
#endif
    return 0;
}

/** Wait for mixer events.
 * @param mixer A mixer handle.
 * @param timeout timout value
 * @returns On success, 1.
 *  On failure, -errno.
 *  On timeout, 0
 * @ingroup libtinyalsa-mixer
 */
int mixer_wait_event(struct mixer *mixer, int timeout)
{
    struct pollfd *pfd;
    struct mixer_ctl_group *grp;
    int count = 0, num_fds = 0, i, ret = 0;

    if (!mixer) {
        return -EINVAL;
    }

    if (mixer->fd >= 0)
        num_fds++;

#ifdef TINYALSA_USES_PLUGINS
    if (mixer->v_grp)
        num_fds++;
#endif

    pfd = (struct pollfd *)calloc(num_fds, sizeof(struct pollfd));
    if (!pfd)
        return -ENOMEM;

    if (mixer->fd >= 0) {
        pfd[count].fd = mixer->fd;
        pfd[count].events = POLLIN | POLLOUT | POLLERR | POLLNVAL;
        count++;
    }

#ifdef TINYALSA_USES_PLUGINS
    if (mixer->v_grp) {
        grp = mixer->v_grp;
        if (!grp->ops->get_poll_fd(grp->data, pfd, count)) {
            pfd[count].events = POLLIN | POLLERR | POLLNVAL;
            count++;
        }
    }
#endif

    if (!count)
        goto exit;

    for (;;) {
        int err;
        err = poll(pfd, count, timeout);
        if (err < 0) {
            ret = -errno;
            goto exit;
        }
        if (!err)
            goto exit;

        for (i = 0; i < count; i++) {
            if (pfd[i].revents & (POLLERR | POLLNVAL)) {
                ret = -EIO;
                goto exit;
            }
            if (pfd[i].revents & (POLLIN | POLLOUT)) {
                if ((i == 0) && mixer->fd >= 0) {
                    grp = mixer->h_grp;
                    grp->event_cnt++;
                }
#ifdef TINYALSA_USES_PLUGINS
                 else {
                    grp = mixer->v_grp;
                    grp->event_cnt++;
                }
#endif
                ret = 1;
                goto exit;
            }
        }
    }
exit:
    free(pfd);
    return ret;
}

/** Consume a mixer event.
 * If mixer_subscribe_events has been called,
 * mixer_wait_event will identify when a control value has changed.
 * This function will clear a single event from the mixer so that
 * further events can be alerted.
 *
 * @param mixer A mixer handle.
 * @returns 1 on success. 0, if no pending event. -errno on failure.
 * @ingroup libtinyalsa-mixer
 */
int mixer_consume_event(struct mixer *mixer)
{
    struct mixer_ctl_event ev;
    if (!mixer) {
        return -EINVAL;
    }

    return mixer_read_event(mixer, &ev);
}

/** Read a mixer control event.
 * Try to read an control event from mixer.
 *
 * @param mixer A mixer handle.
 * @param event Output parameter. If there is an event read form the mixer, this function will fill
 * the event data into it.
 * @returns 1 on success. 0, if no pending event. -errno on failure.
 * @ingroup libtinyalsa-mixer
 */
int mixer_read_event(struct mixer *mixer, struct mixer_ctl_event *event)
{
    struct mixer_ctl_group *grp = NULL;
    struct snd_ctl_event ev;
    ssize_t bytes = 0;

    if (!mixer || !event) {
        return -EINVAL;
    }

    if (mixer->h_grp) {
        if (mixer->h_grp->event_cnt > 0) {
            grp = mixer->h_grp;
        }
    }
#ifdef TINYALSA_USES_PLUGINS
    if (mixer->v_grp) {
        if (mixer->v_grp->event_cnt > 0) {
            grp = mixer->v_grp;
        }
    }
#endif
    if (grp) {
        grp->event_cnt--;
        bytes = grp->ops->read_event(grp->data, &ev, sizeof(ev));

        if (bytes < 0) {
            return -errno;
        }

        if (bytes == sizeof(*event)) {
            memcpy(event, &ev, sizeof(*event));
            return 1;
        }
    }

    return 0;
}

static unsigned int mixer_grp_get_count(struct mixer_ctl_group *grp)
{
    if (!grp)
        return 0;

    return grp->count;
}

/** Gets a mixer control handle, by the mixer control's id.
 * For non-const access, see @ref mixer_get_ctl
 * @param mixer An initialized mixer handle.
 * @param id The control's id in the given mixer.
 * @returns A handle to the mixer control.
 * @ingroup libtinyalsa-mixer
 */
const struct mixer_ctl *mixer_get_ctl_const(const struct mixer *mixer, unsigned int id)
{
    unsigned int h_count;

    if (!mixer || (id >= mixer->total_count))
        return NULL;

    h_count = mixer_grp_get_count(mixer->h_grp);

    if (id < h_count)
        return mixer->h_grp->ctl + id;
#ifdef TINYALSA_USES_PLUGINS
    else {
        unsigned int v_count = mixer_grp_get_count(mixer->v_grp);
	    if ((id - h_count) < v_count)
            return mixer->v_grp->ctl + (id - h_count);
    }
#endif

    return NULL;
}

/** Gets a mixer control handle, by the mixer control's id.
 * For const access, see @ref mixer_get_ctl_const
 * @param mixer An initialized mixer handle.
 * @param id The control's id in the given mixer.
 * @returns A handle to the mixer control.
 * @ingroup libtinyalsa-mixer
 */
struct mixer_ctl *mixer_get_ctl(struct mixer *mixer, unsigned int id)
{
    unsigned int h_count;

    if (!mixer || (id >= mixer->total_count))
        return NULL;

    h_count = mixer_grp_get_count(mixer->h_grp);

    if (id < h_count)
        return mixer->h_grp->ctl + id;
#ifdef TINYALSA_USES_PLUGINS
    else {
        unsigned int v_count = mixer_grp_get_count(mixer->v_grp);
	    if ((id - h_count) < v_count)
            return mixer->v_grp->ctl + (id - h_count);
    }
#endif
    return NULL;
}

/** Gets the first instance of mixer control handle, by the mixer control's name.
 * @param mixer An initialized mixer handle.
 * @param name The control's name in the given mixer.
 * @returns A handle to the mixer control.
 * @ingroup libtinyalsa-mixer
 */
struct mixer_ctl *mixer_get_ctl_by_name(struct mixer *mixer, const char *name)
{
    if (!mixer || !name) {
        return NULL;
    }

    return mixer_get_ctl_by_name_and_index(mixer, name, 0);
}

/** Gets an instance of mixer control handle, by the mixer control's name and index.
 *  For instance, if two controls have the name of 'Volume', then and index of 1 would return the second control.
 * @param mixer An initialized mixer handle.
 * @param name The control's name in the given mixer.
 * @param index The control's index.
 * @returns A handle to the mixer control.
 * @ingroup libtinyalsa-mixer
 */
struct mixer_ctl *mixer_get_ctl_by_name_and_index(struct mixer *mixer,
                                                  const char *name,
                                                  unsigned int index)
{
    struct mixer_ctl_group *grp;
    unsigned int n;
    struct mixer_ctl *ctl;

    if (!mixer || !name) {
        return NULL;
    }

    if (mixer->h_grp) {
        grp = mixer->h_grp;
        ctl = grp->ctl;

        for (n = 0; n < grp->count; n++)
            if (!strcmp(name, (char*) ctl[n].info.id.name)) {
                if (index == 0) {
                    return ctl + n;
                } else {
                    index--;
                }
            }
    }

#ifdef TINYALSA_USES_PLUGINS
    if (mixer->v_grp) {
        grp = mixer->v_grp;
        ctl = grp->ctl;

        for (n = 0; n < grp->count; n++)
            if (!strcmp(name, (char*) ctl[n].info.id.name)) {
                if (index == 0) {
                    return ctl + n;
                } else {
                    index--;
                }
            }
    }
#endif
    return NULL;
}

/** Updates the control's info.
 * This is useful for a program that may be idle for a period of time.
 * @param ctl An initialized control handle.
 * @ingroup libtinyalsa-mixer
 */
void mixer_ctl_update(struct mixer_ctl *ctl)
{
    struct mixer_ctl_group *grp;

    if (!ctl)
        return;

    grp  = ctl->grp;
    grp->ops->ioctl(grp->data, SNDRV_CTL_IOCTL_ELEM_INFO, &ctl->info);
}

/** Checks the control for TLV Read/Write access.
 * @param ctl An initialized control handle.
 * @returns On success, non-zero.
 *  On failure, zero.
 * @ingroup libtinyalsa-mixer
 */
int mixer_ctl_is_access_tlv_rw(const struct mixer_ctl *ctl)
{
    if (!ctl) {
        return 0;
    }

    return (ctl->info.access & SNDRV_CTL_ELEM_ACCESS_TLV_READWRITE);
}

/** Gets the control's ID.
 * @param ctl An initialized control handle.
 * @returns On success, the control's ID is returned.
 *  On error, UINT_MAX is returned instead.
 * @ingroup libtinyalsa-mixer
 */
unsigned int mixer_ctl_get_id(const struct mixer_ctl *ctl)
{
    if (!ctl)
        return UINT_MAX;

    /* numid values start at 1, return a 0-base value that
     * can be passed to mixer_get_ctl()
     */
    return ctl->info.id.numid - 1;
}

/** Gets the name of the control.
 * @param ctl An initialized control handle.
 * @returns On success, the name of the control.
 *  On error, NULL.
 * @ingroup libtinyalsa-mixer
 */
const char *mixer_ctl_get_name(const struct mixer_ctl *ctl)
{
    if (!ctl)
        return NULL;

    return (const char *)ctl->info.id.name;
}

/** Gets the value type of the control.
 * @param ctl An initialized control handle
 * @returns On success, the type of mixer control.
 *  On failure, it returns @ref MIXER_CTL_TYPE_UNKNOWN
 * @ingroup libtinyalsa-mixer
 */
enum mixer_ctl_type mixer_ctl_get_type(const struct mixer_ctl *ctl)
{
    if (!ctl)
        return MIXER_CTL_TYPE_UNKNOWN;

    switch (ctl->info.type) {
    case SNDRV_CTL_ELEM_TYPE_BOOLEAN:    return MIXER_CTL_TYPE_BOOL;
    case SNDRV_CTL_ELEM_TYPE_INTEGER:    return MIXER_CTL_TYPE_INT;
    case SNDRV_CTL_ELEM_TYPE_ENUMERATED: return MIXER_CTL_TYPE_ENUM;
    case SNDRV_CTL_ELEM_TYPE_BYTES:      return MIXER_CTL_TYPE_BYTE;
    case SNDRV_CTL_ELEM_TYPE_IEC958:     return MIXER_CTL_TYPE_IEC958;
    case SNDRV_CTL_ELEM_TYPE_INTEGER64:  return MIXER_CTL_TYPE_INT64;
    default:                             return MIXER_CTL_TYPE_UNKNOWN;
    };
}

/** Gets the string that describes the value type of the control.
 * @param ctl An initialized control handle
 * @returns On success, a string describing type of mixer control.
 * @ingroup libtinyalsa-mixer
 */
const char *mixer_ctl_get_type_string(const struct mixer_ctl *ctl)
{
    if (!ctl)
        return "";

    switch (ctl->info.type) {
    case SNDRV_CTL_ELEM_TYPE_BOOLEAN:    return "BOOL";
    case SNDRV_CTL_ELEM_TYPE_INTEGER:    return "INT";
    case SNDRV_CTL_ELEM_TYPE_ENUMERATED: return "ENUM";
    case SNDRV_CTL_ELEM_TYPE_BYTES:      return "BYTE";
    case SNDRV_CTL_ELEM_TYPE_IEC958:     return "IEC958";
    case SNDRV_CTL_ELEM_TYPE_INTEGER64:  return "INT64";
    default:                             return "Unknown";
    };
}

/** Gets the number of values in the control.
 * @param ctl An initialized control handle
 * @returns The number of values in the mixer control
 * @ingroup libtinyalsa-mixer
 */
unsigned int mixer_ctl_get_num_values(const struct mixer_ctl *ctl)
{
    if (!ctl)
        return 0;

    return ctl->info.count;
}

static int percent_to_int(const struct snd_ctl_elem_info *ei, int percent)
{
    if ((percent > 100) || (percent < 0)) {
        return -EINVAL;
    }

    int range = (ei->value.integer.max - ei->value.integer.min);

    return ei->value.integer.min + (range * percent) / 100;
}

static int int_to_percent(const struct snd_ctl_elem_info *ei, int value)
{
    int range = (ei->value.integer.max - ei->value.integer.min);

    if (range == 0)
        return 0;

    return ((value - ei->value.integer.min) * 100) / range;
}

/** Gets a percentage representation of a specified control value.
 * @param ctl An initialized control handle.
 * @param id The index of the value within the control.
 * @returns On success, the percentage representation of the control value.
 *  On failure, -EINVAL is returned.
 * @ingroup libtinyalsa-mixer
 */
int mixer_ctl_get_percent(const struct mixer_ctl *ctl, unsigned int id)
{
    if (!ctl || (ctl->info.type != SNDRV_CTL_ELEM_TYPE_INTEGER))
        return -EINVAL;

    return int_to_percent(&ctl->info, mixer_ctl_get_value(ctl, id));
}

/** Sets the value of a control by percent, specified by the value index.
 * @param ctl An initialized control handle.
 * @param id The index of the value to set
 * @param percent A percentage value between 0 and 100.
 * @returns On success, zero is returned.
 *  On failure, non-zero is returned.
 * @ingroup libtinyalsa-mixer
 */
int mixer_ctl_set_percent(struct mixer_ctl *ctl, unsigned int id, int percent)
{
    if (!ctl || (ctl->info.type != SNDRV_CTL_ELEM_TYPE_INTEGER))
        return -EINVAL;

    return mixer_ctl_set_value(ctl, id, percent_to_int(&ctl->info, percent));
}

/** Gets the value of a control.
 * @param ctl An initialized control handle.
 * @param id The index of the control value.
 * @returns On success, the specified value is returned.
 *  On failure, -EINVAL is returned.
 * @ingroup libtinyalsa-mixer
 */
int mixer_ctl_get_value(const struct mixer_ctl *ctl, unsigned int id)
{
    struct mixer_ctl_group *grp;
    struct snd_ctl_elem_value ev;
    int ret;

    if (!ctl || (id >= ctl->info.count))
        return -EINVAL;

    grp = ctl->grp;
    memset(&ev, 0, sizeof(ev));
    ev.id.numid = ctl->info.id.numid;
    ret = grp->ops->ioctl(grp->data, SNDRV_CTL_IOCTL_ELEM_READ, &ev);
    if (ret < 0)
        return ret;

    switch (ctl->info.type) {
    case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
        return !!ev.value.integer.value[id];

    case SNDRV_CTL_ELEM_TYPE_INTEGER:
        return ev.value.integer.value[id];

    case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
        return ev.value.enumerated.item[id];

    case SNDRV_CTL_ELEM_TYPE_BYTES:
        return ev.value.bytes.data[id];

    default:
        return -EINVAL;
    }

    return 0;
}

/** Gets the contents of a control's value array.
 * @param ctl An initialized control handle.
 * @param array A pointer to write the array data to.
 *  The size of this array must be equal to the number of items in the array
 *  multiplied by the size of each item.
 * @param count The number of items in the array.
 *  This parameter must match the number of items in the control.
 *  The number of items in the control may be accessed via @ref mixer_ctl_get_num_values
 * @returns On success, zero.
 *  On failure, non-zero.
 * @ingroup libtinyalsa-mixer
 */
int mixer_ctl_get_array(const struct mixer_ctl *ctl, void *array, size_t count)
{
    struct mixer_ctl_group *grp;
    struct snd_ctl_elem_value ev;
    int ret = 0;
    size_t size;
    void *source;

    if (!ctl || !array || count == 0) {
        return -EINVAL;
    }

    grp = ctl->grp;

    if (count > ctl->info.count)
        return -EINVAL;

    memset(&ev, 0, sizeof(ev));
    ev.id.numid = ctl->info.id.numid;

    switch (ctl->info.type) {
    case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
    case SNDRV_CTL_ELEM_TYPE_INTEGER:
        ret = grp->ops->ioctl(grp->data, SNDRV_CTL_IOCTL_ELEM_READ, &ev);
        if (ret < 0)
            return ret;
        size = sizeof(ev.value.integer.value[0]);
        source = ev.value.integer.value;
        break;

    case SNDRV_CTL_ELEM_TYPE_BYTES:
        /* check if this is new bytes TLV */
        if (mixer_ctl_is_access_tlv_rw(ctl)) {
            struct snd_ctl_tlv *tlv;
            int ret;

            if (count > SIZE_MAX - sizeof(*tlv))
                return -EINVAL;

            tlv = calloc(1, sizeof(*tlv) + count);
            if (!tlv)
                return -ENOMEM;

            tlv->numid = ctl->info.id.numid;
            tlv->length = count;
            ret = grp->ops->ioctl(grp->data, SNDRV_CTL_IOCTL_TLV_READ, tlv);

            source = tlv->tlv;
            memcpy(array, source, count);

            free(tlv);

            return ret;
        } else {
            ret = grp->ops->ioctl(grp->data, SNDRV_CTL_IOCTL_ELEM_READ, &ev);
            if (ret < 0)
                return ret;
            size = sizeof(ev.value.bytes.data[0]);
            source = ev.value.bytes.data;
            break;
        }

    case SNDRV_CTL_ELEM_TYPE_IEC958:
        size = sizeof(ev.value.iec958);
        source = &ev.value.iec958;
        break;

    default:
        return -EINVAL;
    }

    memcpy(array, source, size * count);

    return 0;
}

/** Sets the value of a control, specified by the value index.
 * @param ctl An initialized control handle.
 * @param id The index of the value within the control.
 * @param value The value to set.
 *  This must be in a range specified by @ref mixer_ctl_get_range_min
 *  and @ref mixer_ctl_get_range_max.
 * @returns On success, zero is returned.
 *  On failure, non-zero is returned.
 * @ingroup libtinyalsa-mixer
 */
int mixer_ctl_set_value(struct mixer_ctl *ctl, unsigned int id, int value)
{
    struct mixer_ctl_group *grp;
    struct snd_ctl_elem_value ev;
    int ret;

    if (!ctl || id >= ctl->info.count) {
        return -EINVAL;
    }

    grp = ctl->grp;
    memset(&ev, 0, sizeof(ev));
    ev.id.numid = ctl->info.id.numid;
    ret = grp->ops->ioctl(grp->data, SNDRV_CTL_IOCTL_ELEM_READ, &ev);
    if (ret < 0)
        return ret;

    switch (ctl->info.type) {
    case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
        ev.value.integer.value[id] = !!value;
        break;

    case SNDRV_CTL_ELEM_TYPE_INTEGER:
        ev.value.integer.value[id] = value;
        break;

    case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
        ev.value.enumerated.item[id] = value;
        break;

    case SNDRV_CTL_ELEM_TYPE_BYTES:
        ev.value.bytes.data[id] = value;
        break;

    default:
        return -EINVAL;
    }

    return grp->ops->ioctl(grp->data, SNDRV_CTL_IOCTL_ELEM_WRITE, &ev);
}

/** Sets the contents of a control's value array.
 * @param ctl An initialized control handle.
 * @param array The array containing control values.
 * @param count The number of values in the array.
 *  This must match the number of values in the control.
 *  The number of values in a control may be accessed via @ref mixer_ctl_get_num_values
 * @returns On success, zero.
 *  On failure, non-zero.
 * @ingroup libtinyalsa-mixer
 */
int mixer_ctl_set_array(struct mixer_ctl *ctl, const void *array, size_t count)
{
    struct mixer_ctl_group *grp;
    struct snd_ctl_elem_value ev;
    size_t size;
    void *dest;

    if (!ctl || !array || count == 0) {
        return -EINVAL;
    }

    grp = ctl->grp;

    if (count > ctl->info.count)
        return -EINVAL;

    memset(&ev, 0, sizeof(ev));
    ev.id.numid = ctl->info.id.numid;

    switch (ctl->info.type) {
    case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
    case SNDRV_CTL_ELEM_TYPE_INTEGER:
        size = sizeof(ev.value.integer.value[0]);
        dest = ev.value.integer.value;
        break;

    case SNDRV_CTL_ELEM_TYPE_BYTES:
        /* check if this is new bytes TLV */
        if (mixer_ctl_is_access_tlv_rw(ctl)) {
            struct snd_ctl_tlv *tlv;
            int ret = 0;

            if (count > SIZE_MAX - sizeof(*tlv))
                return -EINVAL;

            tlv = calloc(1, sizeof(*tlv) + count);
            if (!tlv)
                return -ENOMEM;

            tlv->numid = ctl->info.id.numid;
            tlv->length = count;
            memcpy(tlv->tlv, array, count);

            ret = grp->ops->ioctl(grp->data, SNDRV_CTL_IOCTL_TLV_WRITE, tlv);
            free(tlv);

            return ret;
        } else {
            size = sizeof(ev.value.bytes.data[0]);
            dest = ev.value.bytes.data;
        }
        break;

    case SNDRV_CTL_ELEM_TYPE_IEC958:
        size = sizeof(ev.value.iec958);
        dest = &ev.value.iec958;
        break;

    default:
        return -EINVAL;
    }

    memcpy(dest, array, size * count);

    return grp->ops->ioctl(grp->data, SNDRV_CTL_IOCTL_ELEM_WRITE, &ev);
}

/** Gets the minimum value of an control.
 * The control must have an integer type.
 * The type of the control can be checked with @ref mixer_ctl_get_type.
 * @param ctl An initialized control handle.
 * @returns On success, the minimum value of the control.
 *  On failure, -EINVAL.
 * @ingroup libtinyalsa-mixer
 */
int mixer_ctl_get_range_min(const struct mixer_ctl *ctl)
{
    if (!ctl || ctl->info.type != SNDRV_CTL_ELEM_TYPE_INTEGER) {
        return -EINVAL;
    }

    return ctl->info.value.integer.min;
}

/** Gets the maximum value of an control.
 * The control must have an integer type.
 * The type of the control can be checked with @ref mixer_ctl_get_type.
 * @param ctl An initialized control handle.
 * @returns On success, the maximum value of the control.
 *  On failure, -EINVAL.
 * @ingroup libtinyalsa-mixer
 */
int mixer_ctl_get_range_max(const struct mixer_ctl *ctl)
{
    if (!ctl || ctl->info.type != SNDRV_CTL_ELEM_TYPE_INTEGER) {
        return -EINVAL;
    }

    return ctl->info.value.integer.max;
}

/*
 * The dB scale a driver gives an integer control, as its TLV data. Gains
 * are in 1/100 dB, the unit of the TLV itself. What follows reads the
 * scale as alsa-lib's snd_tlv_get_dB_range() and snd_tlv_convert_from_dB()
 * do, rounding to the nearest value.
 */
#ifndef SNDRV_CTL_TLVT_CONTAINER
#define SNDRV_CTL_TLVT_CONTAINER        0
#define SNDRV_CTL_TLVT_DB_SCALE         1
#define SNDRV_CTL_TLVT_DB_LINEAR        2
#define SNDRV_CTL_TLVT_DB_RANGE         3
#define SNDRV_CTL_TLVT_DB_MINMAX        4
#define SNDRV_CTL_TLVT_DB_MINMAX_MUTE   5
#endif

/* Words of TLV read at most: a dB range of a few dozen items fits */
#define MIXER_TLV_MAX_WORDS 256

/* A TLV item's type, its length in bytes, then its data */
#define TLV_TYPE(tlv)   ((tlv)[0])
#define TLV_BYTES(tlv)  ((tlv)[1])
/* Rounded up without an addition that could wrap on a bad length */
#define TLV_WORDS(tlv)  ((size_t)2 + TLV_BYTES(tlv) / sizeof(unsigned int) + \
                         (TLV_BYTES(tlv) % sizeof(unsigned int) != 0))

static int mixer_ctl_read_tlv(const struct mixer_ctl *ctl, struct snd_ctl_tlv *tlv)
{
    struct mixer_ctl_group *grp = ctl->grp;
    int ret;

    if (ctl->info.type != SNDRV_CTL_ELEM_TYPE_INTEGER ||
            !(ctl->info.access & SNDRV_CTL_ELEM_ACCESS_TLV_READ)) {
        return -ENXIO;
    }

    tlv->numid = ctl->info.id.numid;
    tlv->length = MIXER_TLV_MAX_WORDS * sizeof(unsigned int);
    ret = grp->ops->ioctl(grp->data, SNDRV_CTL_IOCTL_TLV_READ, tlv);
    if (ret < 0) {
        return (ret == -1 && errno != 0) ? -errno : ret;
    }

    /* The kernel leaves length as it was: the data's own header sizes it */
    if (TLV_WORDS(tlv->tlv) > MIXER_TLV_MAX_WORDS) {
        return -ENOSPC;
    }
    return 0;
}

/* The first dB item in a TLV of the given words, looking into containers */
static const unsigned int *tlv_find_db(const unsigned int *tlv, size_t words)
{
    while (words >= 2 && TLV_WORDS(tlv) <= words) {
        switch (TLV_TYPE(tlv)) {
        case SNDRV_CTL_TLVT_CONTAINER: {
            const unsigned int *db = tlv_find_db(tlv + 2, TLV_WORDS(tlv) - 2);
            if (db) {
                return db;
            }
            break;
        }
        case SNDRV_CTL_TLVT_DB_SCALE:
        case SNDRV_CTL_TLVT_DB_LINEAR:
        case SNDRV_CTL_TLVT_DB_RANGE:
        case SNDRV_CTL_TLVT_DB_MINMAX:
        case SNDRV_CTL_TLVT_DB_MINMAX_MUTE:
            return tlv;
        default:
            break;
        }
        words -= TLV_WORDS(tlv);
        tlv += TLV_WORDS(tlv);
    }
    return NULL;
}

/* The dB range of one dB item over the values rmin..rmax */
static int tlv_db_range(const unsigned int *tlv, int64_t rmin, int64_t rmax,
                        int64_t *min_db, int64_t *max_db)
{
    switch (TLV_TYPE(tlv)) {
    case SNDRV_CTL_TLVT_DB_SCALE:
        if (TLV_BYTES(tlv) < 2 * sizeof(unsigned int)) {
            return -EINVAL;
        }
        *min_db = (int)tlv[2];
        *max_db = *min_db + (int64_t)(tlv[3] & 0xffff) * (rmax - rmin);
        return 0;
    case SNDRV_CTL_TLVT_DB_LINEAR:
    case SNDRV_CTL_TLVT_DB_MINMAX:
    case SNDRV_CTL_TLVT_DB_MINMAX_MUTE:
        if (TLV_BYTES(tlv) < 2 * sizeof(unsigned int)) {
            return -EINVAL;
        }
        *min_db = (int)tlv[2];
        *max_db = (int)tlv[3];
        return 0;
    case SNDRV_CTL_TLVT_DB_RANGE: {
        /* Items of value range and dB item: rmin, rmax, type, bytes, data */
        const unsigned int *item = tlv + 2;
        size_t words = TLV_WORDS(tlv) - 2;
        bool found = false;

        while (words >= 4 && 2 + TLV_WORDS(item + 2) <= words) {
            int64_t lo, hi;
            int ret = tlv_db_range(item + 2, (int)item[0], (int)item[1], &lo, &hi);
            if (ret < 0) {
                return ret;
            }
            if (!found || lo < *min_db) {
                *min_db = lo;
            }
            if (!found || hi > *max_db) {
                *max_db = hi;
            }
            found = true;
            words -= 2 + TLV_WORDS(item + 2);
            item += 2 + TLV_WORDS(item + 2);
        }
        return found ? 0 : -EINVAL;
    }
    default:
        return -EINVAL;
    }
}

static int64_t round_div(int64_t num, int64_t den)
{
    /* nearest, halves away from zero, for a positive divisor */
    return (num >= 0) ? (num + den / 2) / den : -((-num + den / 2) / den);
}

/* The value, in rmin..rmax, of one dB item nearest to the gain */
static int tlv_db_to_value(const unsigned int *tlv, int64_t rmin, int64_t rmax,
                           int64_t db, int64_t *value)
{
    int64_t min_db, max_db;
    int ret;

    if (TLV_TYPE(tlv) == SNDRV_CTL_TLVT_DB_RANGE) {
        const unsigned int *item = tlv + 2;
        size_t words = TLV_WORDS(tlv) - 2;
        int64_t prev_max_db = 0;
        int64_t prev_value = 0;
        bool have_prev = false;

        while (words >= 4 && 2 + TLV_WORDS(item + 2) <= words) {
            const int64_t imin = (int)item[0], imax = (int)item[1];

            ret = tlv_db_range(item + 2, imin, imax, &min_db, &max_db);
            if (ret < 0) {
                return ret;
            }
            if (db < min_db) {
                /* Below this item: the item before, if nearer, else its start */
                *value = (have_prev && db - prev_max_db < min_db - db) ? prev_value : imin;
                return 0;
            }
            if (db <= max_db) {
                return tlv_db_to_value(item + 2, imin, imax, db, value);
            }
            prev_max_db = max_db;
            prev_value = imax;
            have_prev = true;
            words -= 2 + TLV_WORDS(item + 2);
            item += 2 + TLV_WORDS(item + 2);
        }
        if (!have_prev) {
            return -EINVAL;
        }
        *value = prev_value;
        return 0;
    }

    ret = tlv_db_range(tlv, rmin, rmax, &min_db, &max_db);
    if (ret < 0) {
        return ret;
    }
    if (db <= min_db) {
        *value = rmin;
        return 0;
    }
    if (db >= max_db) {
        *value = rmax;
        return 0;
    }

    switch (TLV_TYPE(tlv)) {
    case SNDRV_CTL_TLVT_DB_SCALE: {
        const int64_t step = tlv[3] & 0xffff;
        *value = rmin + round_div(db - min_db, step);
        break;
    }
    case SNDRV_CTL_TLVT_DB_LINEAR: {
        /* Linear in amplitude between the two ends */
        const double lo = pow(10.0, min_db / 2000.0);
        const double hi = pow(10.0, max_db / 2000.0);
        const double a = pow(10.0, db / 2000.0);
        *value = rmin + llround((a - lo) / (hi - lo) * (double)(rmax - rmin));
        break;
    }
    default:
        /* DB_MINMAX, DB_MINMAX_MUTE: linear in dB between the two ends */
        *value = rmin + round_div((db - min_db) * (rmax - rmin), max_db - min_db);
        break;
    }

    if (*value < rmin) {
        *value = rmin;
    } else if (*value > rmax) {
        *value = rmax;
    }
    return 0;
}

/** Gets the gain range of an integer control from the dB scale its driver
 * gives it.
 * @param ctl An initialized control handle.
 * @param min_db Set to the gain of the control's minimum value, in 1/100 dB.
 * @param max_db Set to the gain of the control's maximum value, in 1/100 dB.
 * @returns On success, zero.
 *  -ENXIO if the control has no dB scale, other negative errno on failure.
 * @ingroup libtinyalsa-mixer
 */
int mixer_ctl_get_db_range(const struct mixer_ctl *ctl, long *min_db, long *max_db)
{
    unsigned int buf[(sizeof(struct snd_ctl_tlv) / sizeof(unsigned int)) + MIXER_TLV_MAX_WORDS];
    struct snd_ctl_tlv *tlv = (struct snd_ctl_tlv *)buf;
    const unsigned int *db;
    int64_t lo, hi;
    int ret;

    if (!ctl || !min_db || !max_db) {
        return -EINVAL;
    }

    ret = mixer_ctl_read_tlv(ctl, tlv);
    if (ret < 0) {
        return ret;
    }

    db = tlv_find_db(tlv->tlv, TLV_WORDS(tlv->tlv));
    if (!db) {
        return -ENXIO;
    }

    ret = tlv_db_range(db, ctl->info.value.integer.min, ctl->info.value.integer.max,
                       &lo, &hi);
    if (ret < 0) {
        return ret;
    }

    *min_db = lo;
    *max_db = hi;
    return 0;
}

/** Gets the value of an integer control nearest to a gain, from the dB
 * scale its driver gives it. Gains outside the scale give its ends.
 * @param ctl An initialized control handle.
 * @param db The gain, in 1/100 dB.
 * @param value Set to the control's value for the gain.
 * @returns On success, zero.
 *  -ENXIO if the control has no dB scale, other negative errno on failure.
 * @ingroup libtinyalsa-mixer
 */
int mixer_ctl_get_value_for_db(const struct mixer_ctl *ctl, long db, int *value)
{
    unsigned int buf[(sizeof(struct snd_ctl_tlv) / sizeof(unsigned int)) + MIXER_TLV_MAX_WORDS];
    struct snd_ctl_tlv *tlv = (struct snd_ctl_tlv *)buf;
    const unsigned int *item;
    int64_t v;
    int ret;

    if (!ctl || !value) {
        return -EINVAL;
    }

    ret = mixer_ctl_read_tlv(ctl, tlv);
    if (ret < 0) {
        return ret;
    }

    item = tlv_find_db(tlv->tlv, TLV_WORDS(tlv->tlv));
    if (!item) {
        return -ENXIO;
    }

    ret = tlv_db_to_value(item, ctl->info.value.integer.min,
                          ctl->info.value.integer.max, db, &v);
    if (ret < 0) {
        return ret;
    }

    *value = (int)v;
    return 0;
}

/** Get the number of enumerated items in the control.
 * @param ctl An initialized control handle.
 * @returns The number of enumerated items in the control.
 * @ingroup libtinyalsa-mixer
 */
unsigned int mixer_ctl_get_num_enums(const struct mixer_ctl *ctl)
{
    if (!ctl) {
        return 0;
    }

    return ctl->info.value.enumerated.items;
}

static int mixer_ctl_fill_enum_string(struct mixer_ctl *ctl)
{
    struct mixer_ctl_group *grp = ctl->grp;
    struct snd_ctl_elem_info tmp;
    unsigned int m;
    char **enames;

    if (ctl->ename) {
        return 0;
    }

    enames = calloc(ctl->info.value.enumerated.items, sizeof(char*));
    if (!enames)
        goto fail;
    for (m = 0; m < ctl->info.value.enumerated.items; m++) {
        memset(&tmp, 0, sizeof(tmp));
        tmp.id.numid = ctl->info.id.numid;
        tmp.value.enumerated.item = m;
        if (grp->ops->ioctl(grp->data, SNDRV_CTL_IOCTL_ELEM_INFO, &tmp) < 0)
            goto fail;
        enames[m] = strdup(tmp.value.enumerated.name);
        if (!enames[m])
            goto fail;
    }
    ctl->ename = enames;
    return 0;

fail:
    if (enames) {
        for (m = 0; m < ctl->info.value.enumerated.items; m++) {
            if (enames[m]) {
                free(enames[m]);
            }
        }
        free(enames);
    }
    return -1;
}

/** Gets the string representation of an enumerated item.
 * @param ctl An initialized control handle.
 * @param enum_id The index of the enumerated value.
 * @returns A string representation of the enumerated item.
 * @ingroup libtinyalsa-mixer
 */
const char *mixer_ctl_get_enum_string(struct mixer_ctl *ctl,
                                      unsigned int enum_id)
{
    if (!ctl || ctl->info.type != SNDRV_CTL_ELEM_TYPE_ENUMERATED ||
            enum_id >= ctl->info.value.enumerated.items) {
        return NULL;
    }

    if (mixer_ctl_fill_enum_string(ctl) < 0) {
        return NULL;
    }

    return (const char *)ctl->ename[enum_id];
}

/** Set an enumeration value by string value.
 * @param ctl An enumerated mixer control.
 * @param string The string representation of an enumeration.
 * @returns On success, zero.
 *  On failure, zero.
 * @ingroup libtinyalsa-mixer
 */
int mixer_ctl_set_enum_by_string(struct mixer_ctl *ctl, const char *string)
{
    struct mixer_ctl_group *grp;
    unsigned int i, num_enums;
    struct snd_ctl_elem_value ev;
    int ret;

    if (!ctl || !string || ctl->info.type != SNDRV_CTL_ELEM_TYPE_ENUMERATED) {
        return -EINVAL;
    }

    if (mixer_ctl_fill_enum_string(ctl) < 0) {
        return -EINVAL;
    }

    grp = ctl->grp;
    num_enums = ctl->info.value.enumerated.items;
    for (i = 0; i < num_enums; i++) {
        if (!strcmp(string, ctl->ename[i])) {
            memset(&ev, 0, sizeof(ev));
            ev.value.enumerated.item[0] = i;
            ev.id.numid = ctl->info.id.numid;
            ret = grp->ops->ioctl(grp->data, SNDRV_CTL_IOCTL_ELEM_WRITE, &ev);
            if (ret < 0)
                return ret;
            return 0;
        }
    }

    return -EINVAL;
}
