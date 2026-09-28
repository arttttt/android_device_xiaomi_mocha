/*
   Copyright (C) 2018 Artyom Bambalov <artem-bambalov@yandex.ru>
 */

#define LOG_TAG "mocha_init"

#include <android-base/logging.h>
#include <string.h>
#include <sys/sysinfo.h>

#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>

/*
 * R took property_set() away from vendor init libraries, and the header that
 * declared it, property_service.h, now pulls in android-base/format.h and fmt
 * with it. This runs inside init while it loads the boot properties, before
 * the property service answers, so the value goes straight into the property
 * area, the way LineageOS device trees do it on 18.1.
 */
static void property_override(char const prop[], char const value[])
{
    prop_info *pi = (prop_info *) __system_property_find(prop);
    if (pi)
        __system_property_update(pi, value, strlen(value));
    else
        __system_property_add(prop, strlen(prop), value, strlen(value));
}

char const* heapstartsize;
char const* heapgrowthlimit;
char const* heapsize;
char const* heapminfree;

void get_dalvik_heap_props()
{
    struct sysinfo sys;

    sysinfo(&sys);

    if (sys.totalram > 2048ull * 1024 * 1024) {
        LOG(VERBOSE) << "3Gb RAM device";
        heapstartsize = "8m";
        heapgrowthlimit = "288m";
        heapsize = "768m";
        heapminfree = "512k";
    } else {
        LOG(VERBOSE) << "2Gb RAM device";
        heapstartsize = "16m";
        heapgrowthlimit = "192m";
        heapsize = "512m";
        heapminfree = "2m";
    }
}

void vendor_load_properties()
{
    get_dalvik_heap_props();

    property_override("dalvik.vm.heapstartsize", heapstartsize);
    property_override("dalvik.vm.heapgrowthlimit", heapgrowthlimit);
    property_override("dalvik.vm.heapsize", heapsize);
    property_override("dalvik.vm.heaptargetutilization", "0.75");
    property_override("dalvik.vm.heapminfree", heapminfree);
    property_override("dalvik.vm.heapmaxfree", "8m");
}
