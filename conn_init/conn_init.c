
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
#include <private/android_filesystem_config.h>

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define TAG "conn_init"

#define MAC_PARTITION     "/dev/block/platform/700b0600.sdhci/by-name/BKB"
#define MAC_PARTITION_OLD "/dev/block/platform/sdhci-tegra.3/by-name/BKB"

#define BT_MAC_PROP  "ro.bt.bdaddr_path"
#define BT_MAC_PROP1 "persist.service.bdroid.bdaddr"
#define BT_MAC_PROP2 "ro.boot.btmacaddr"

#define WIFI_MAC_FILE "/vendor/etc/mocha_macaddr.txt"
#define BT_MAC_FILE   "/vendor/etc/mocha_btmacaddr.txt"

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

static uid_t id_of(const char *name, uid_t fallback)
{
    struct passwd *pw = getpwnam(name);

    return pw != NULL ? pw->pw_uid : fallback;
}

/*
 * Owner and mode are set outright rather than left to whatever umask the
 * service happens to inherit: the file is read from two different domains,
 * and there is nothing here worth guessing at.
 */
static int write_addr_file(const char *path, const char *addr, uid_t uid,
                           gid_t gid, mode_t mode)
{
    FILE *fp = fopen(path, "w");

    if (fp == NULL) {
        ALOGE("%s: can't open %s, error: %d", TAG, path, errno);
        return -1;
    }

    fprintf(fp, "%s\n", addr);
    fclose(fp);

    if (chown(path, uid, gid) != 0)
        ALOGE("%s: can't chown %s, error: %d", TAG, path, errno);

    if (chmod(path, mode) != 0)
        ALOGE("%s: can't chmod %s, error: %d", TAG, path, errno);

    return 0;
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
     * /system goes read-write only here, once there is something to write,
     * and goes back immediately afterwards.
     */
    if (system("mount -o remount,rw /system") != 0) {
        ALOGE("%s: can't remount /system read-write, error: %d", TAG, errno);
        return 0;
    }

    write_addr_file(WIFI_MAC_FILE, wifi_str, 0, 0, 0644);

    if (write_addr_file(BT_MAC_FILE, bt_str, id_of("bluetooth", AID_BLUETOOTH),
                        id_of("bluetooth", AID_BLUETOOTH), 0600) == 0) {
        property_set(BT_MAC_PROP, BT_MAC_FILE);
        property_set(BT_MAC_PROP1, bt_str);
        property_set(BT_MAC_PROP2, bt_str);
    }

    if (system("mount -o remount,ro /system") != 0)
        ALOGE("%s: can't remount /system read-only again, error: %d", TAG, errno);

    ALOGI("%s: wifi %s, bt %s", TAG, wifi_str, bt_str);

    return 0;
}
