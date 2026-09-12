
/*
 * Copyright (C) 2017-2018 Artyom Bambalov <artem-bambalov@yandex.ru>
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

#include <cutils/log.h>
#include <cutils/properties.h>
#include <openssl/md5.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define TAG "conn_init"

#define MAC_PARTITION     "/dev/block/platform/700b0600.sdhci/by-name/BKB"
#define MAC_PARTITION_OLD "/dev/block/platform/sdhci-tegra.3/by-name/BKB"

#define BT_MAC_PROP1 "persist.service.bdroid.bdaddr"
#define BT_MAC_PROP2 "ro.boot.btmacaddr"

#define NVRAM_SRC        "/vendor/etc/mocha_nvram.txt"
#define NVRAM_DIR        "/data/vendor/wifi"
#define NVRAM_DST        NVRAM_DIR "/nvram.txt"
#define NVRAM_PATH_PARAM "/sys/module/bcmdhd/parameters/nvram_path"


#define BT_MAC_TAG   "XIAOMIBT!"
#define WIFI_MAC_TAG "XIAOMIWF!"

/*
 * BKB layout: two records of twenty-two bytes, each a nine byte tag followed
 * by the six bytes of the address, least significant byte first.
 */
#define TAG_LEN      9
#define ADDR_LEN     6
#define ADDR_OFF     TAG_LEN
#define REC_LEN      22
#define BT_REC_OFF   0
#define WIFI_REC_OFF 22
#define BKB_LEN      (WIFI_REC_OFF + REC_LEN)

#define ADDR_STR_LEN 18 /* "xx:xx:xx:xx:xx:xx" plus the terminator */
#define MD5_HEX_LEN  (2 * MD5_DIGEST_LENGTH + 1)

static int read_bkb(unsigned char *buf, size_t len)
{
    static const char *paths[] = { MAC_PARTITION, MAC_PARTITION_OLD };
    size_t i;

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        FILE *fp = fopen(paths[i], "r");
        size_t got;

        if (fp == NULL) {
            ALOGI("%s: %s is not there, error: %d", TAG, paths[i], errno);
            continue;
        }

        got = fread(buf, sizeof(char), len, fp);
        fclose(fp);

        if (got == len)
            return 0;

        ALOGE("%s: %s gave %zu bytes out of %zu", TAG, paths[i], got, len);
    }

    return -1;
}

static int addr_from_bkb(const unsigned char *bkb, size_t off, const char *tag,
                         unsigned char addr[ADDR_LEN])
{
    const unsigned char *rec = bkb + off;
    int i;

    if (memcmp(rec, tag, TAG_LEN) != 0) {
        ALOGI("%s: no %s tag in the partition", TAG, tag);
        return -1;
    }

    for (i = 0; i < ADDR_LEN; i++)
        addr[i] = rec[ADDR_OFF + ADDR_LEN - 1 - i];

    ALOGI("%s: %s was found", TAG, tag);
    return 0;
}

static int serial_md5(char hex[MD5_HEX_LEN])
{
    char serial[PROPERTY_VALUE_MAX] = { 0 };
    unsigned char digest[MD5_DIGEST_LENGTH];
    int i;

    if (property_get("ro.serialno", serial, "") <= 0) {
        ALOGE("%s: ro.serialno is empty, nothing to derive an address from", TAG);
        return -1;
    }

    MD5((const unsigned char *) serial, strlen(serial), digest);

    for (i = 0; i < MD5_DIGEST_LENGTH; i++)
        snprintf(hex + 2 * i, 3, "%02x", digest[i]);

    return 0;
}

static unsigned char hex_octet(const char *hex, int off)
{
    char pair[3] = { hex[off], hex[off + 1], '\0' };

    return (unsigned char) strtoul(pair, NULL, 16);
}

/*
 * The slicing follows macaddr.sh offset for offset, the overlapping 16 and 17
 * included: addresses earlier builds have already handed out must stay what
 * they were, or every pairing and every MAC reservation goes with them.
 */
static void bt_addr_from_serial(const char *hex, unsigned char addr[ADDR_LEN])
{
    static const int off[ADDR_LEN] = { 3, 16, 17, 9, 11, 13 };
    int i;

    for (i = 0; i < ADDR_LEN; i++)
        addr[i] = hex_octet(hex, off[i]);

    /*
     * The one departure from the script, and a forced one. The first octet
     * came out of the hash untouched, and its low bit is the group bit: an
     * address carrying it does not identify a device at all. Clear it and
     * raise the neighbour, which says the address is locally administered
     * rather than issued by IEEE -- plainly true of one computed from a
     * serial number.
     */
    addr[0] = (addr[0] & ~0x01) | 0x02;
}

static void wifi_addr_from_serial(const char *hex, unsigned char addr[ADDR_LEN])
{
    static const int off[] = { 7, 9, 11, 14 };
    size_t i;

    /* The Xiaomi prefix, fixed here exactly as macaddr.sh fixed it. */
    addr[0] = 0x0c;
    addr[1] = 0x1d;

    for (i = 0; i < sizeof(off) / sizeof(off[0]); i++)
        addr[i + 2] = hex_octet(hex, off[i]);
}

static void addr_to_str(const unsigned char addr[ADDR_LEN], char out[ADDR_STR_LEN])
{
    snprintf(out, ADDR_STR_LEN, "%02x:%02x:%02x:%02x:%02x:%02x",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

/*
 * Give wifi its own address.
 *
 * The stock nvram carries macaddr=00:90:4c:16:70:01 -- a Broadcom sample,
 * the same on every one of these tablets. The driver this board builds looks
 * for an address in three places, and says so itself in
 * dhd_linux_platdev.c: a userspace command, then of_get_mac_address() on a
 * node named android,bcmdhd_wlan, then the chip's OTP. This board's device
 * tree has no such node and the OTP is blank, so what it ends up with is
 * whatever nvram said, which is that sample.
 *
 * So the nvram is copied with the address line replaced, and the driver is
 * pointed at the copy through the module parameter it exposes for exactly
 * this -- the same way the firmware path is already handled on this board
 * (WIFI_DRIVER_FW_PATH_PARAM in BoardConfig.mk). The parameter is read when
 * the firmware is loaded, which is long after this service has run, so the
 * address is in place before the chip is first brought up.
 *
 * The copy lives under /data because it has to be written, and the reader is
 * the kernel: when this board stops running permissive, the kernel domain
 * will need to be allowed to read it.
 */
static int write_nvram_copy(const char *src, const char *dst, const char *addr)
{
    FILE *in, *out;
    char line[256];
    int replaced = 0;

    in = fopen(src, "r");
    if (in == NULL) {
        ALOGE("%s: can't read %s, error: %d", TAG, src, errno);
        return -1;
    }

    out = fopen(dst, "w");
    if (out == NULL) {
        ALOGE("%s: can't write %s, error: %d", TAG, dst, errno);
        fclose(in);
        return -1;
    }

    while (fgets(line, sizeof(line), in) != NULL) {
        if (strncmp(line, "macaddr=", 8) == 0) {
            fprintf(out, "macaddr=%s\n", addr);
            replaced = 1;
        } else {
            fputs(line, out);
        }
    }

    /* A stock nvram without the line at all is still worth an address. */
    if (replaced == 0)
        fprintf(out, "macaddr=%s\n", addr);

    fclose(in);
    if (fclose(out) != 0) {
        ALOGE("%s: can't finish %s, error: %d", TAG, dst, errno);
        return -1;
    }

    if (chmod(dst, 0644) != 0)
        ALOGE("%s: can't chmod %s, error: %d", TAG, dst, errno);

    return 0;
}

static int write_str_file(const char *path, const char *value)
{
    FILE *fp = fopen(path, "w");

    if (fp == NULL) {
        ALOGE("%s: can't open %s, error: %d", TAG, path, errno);
        return -1;
    }

    fprintf(fp, "%s", value);

    if (fclose(fp) != 0) {
        ALOGE("%s: can't write %s, error: %d", TAG, path, errno);
        return -1;
    }

    return 0;
}

static void set_wifi_addr(const char *addr)
{
    if (mkdir(NVRAM_DIR, 0771) != 0 && errno != EEXIST) {
        ALOGE("%s: can't create %s, error: %d", TAG, NVRAM_DIR, errno);
        return;
    }

    if (write_nvram_copy(NVRAM_SRC, NVRAM_DST, addr) != 0)
        return;

    if (write_str_file(NVRAM_PATH_PARAM, NVRAM_DST) == 0)
        ALOGI("%s: wifi nvram is %s", TAG, NVRAM_DST);
}

int main(void)
{
    unsigned char bkb[BKB_LEN] = { 0 };
    unsigned char bt[ADDR_LEN] = { 0 };
    unsigned char wifi[ADDR_LEN] = { 0 };
    char hex[MD5_HEX_LEN] = { 0 };
    char bt_str[ADDR_STR_LEN];
    char wifi_str[ADDR_STR_LEN];
    int have_bt = -1;
    int have_wifi = -1;

    if (read_bkb(bkb, sizeof(bkb)) == 0) {
        have_bt = addr_from_bkb(bkb, BT_REC_OFF, BT_MAC_TAG, bt);
        have_wifi = addr_from_bkb(bkb, WIFI_REC_OFF, WIFI_MAC_TAG, wifi);
    }

    /*
     * An empty or broken BKB is a working state rather than an exception: on
     * part of these tablets the partition is blank from the factory. The
     * addresses are then derived from the serial number, which is always
     * there.
     */
    if (have_bt != 0 || have_wifi != 0) {
        if (serial_md5(hex) != 0)
            return 0;

        if (have_bt != 0) {
            bt_addr_from_serial(hex, bt);
            ALOGI("%s: bt address derived from the serial number", TAG);
        }

        if (have_wifi != 0) {
            wifi_addr_from_serial(hex, wifi);
            ALOGI("%s: wifi address derived from the serial number", TAG);
        }
    }

    addr_to_str(bt, bt_str);
    addr_to_str(wifi, wifi_str);

    /*
     * The addresses are handed over as properties, not as files.
     *
     * This used to remount /system read-write, write both addresses into
     * /vendor/etc, and set the properties only if that had worked. On this
     * release it never works: /system carries /vendor and is mounted
     * read-only, and remounting it is not something a service does any more.
     * The remount failed, the function returned before setting anything, and
     * the Bluetooth HAL then found no address at all and killed itself --
     *
     *     Abort message: 'Open: No Bluetooth Address!'
     *
     * once every few seconds, until init gave up and rebooted the board.
     *
     * The file was never needed for this. hardware/interfaces/bluetooth/1.0/
     * default/bluetooth_address.cc looks in three places in order: the file
     * named by ro.bt.bdaddr_path, then ro.boot.btmacaddr, then
     * persist.service.bdroid.bdaddr. The last two carry the address as a
     * string and want nothing from the filesystem, and they were being set
     * inside the same branch as the file write for no reason.
     *
     * So the address goes out as properties and the writable-media question
     * does not arise. ro.bt.bdaddr_path is deliberately left unset: with no
     * file to point it at, leaving it empty makes the HAL fall through to the
     * properties rather than fail on an unreadable path.
     */
    property_set(BT_MAC_PROP2, bt_str);
    property_set(BT_MAC_PROP1, bt_str);

    set_wifi_addr(wifi_str);

    ALOGI("%s: wifi %s, bt %s", TAG, wifi_str, bt_str);

    return 0;
}
