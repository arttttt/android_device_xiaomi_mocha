# Audio
#
# Music and video go to the deep buffer output: AudioPolicyManager gives
# STREAM_MUSIC the deep buffer flag only when this is set. That output has
# 20 ms periods on its own DAM input, so playback no longer shares the
# primary output's short periods with every other sound.
PRODUCT_PROPERTY_OVERRIDES += \
    audio.deep_buffer.media=true

# DRM
PRODUCT_PROPERTY_OVERRIDES += \
    drm.service.enabled=true

# FM
#
# De-emphasis of the FM receiver, in microseconds: it has to match the
# transmitters' -- 50 in Europe, Russia and most of the world, 75 in the
# Americas and Korea. The FM HAL sets it on the chip when the radio starts.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.vendor.fm.deemphasis=50

# Graphics
PRODUCT_PROPERTY_OVERRIDES += \
    ro.opengles.version=196610 \
    ro.sf.lcd_density=320 \
    ro.zygote.disable_gl_preload=true

# Do not compress what the display cannot read.
#
# The allocator hands the graphics processor memory it may compress: beside the
# pixels it keeps a smaller record of each tile and writes only that where it
# can. The processor understands the arrangement and gains from it. This
# display controller does not -- it reads the record as though it were pixels,
# which shows as a regular grid laid over a recognisable picture.
#
# So every buffer going to the panel had to be flattened first, which is a
# whole pass over the screen, on the same processor the applications need for
# their own drawing, on every frame. Not compressing in the first place costs
# the processor some memory bandwidth and saves everything else: measured here
# as forty frames a second becoming forty-eight, and the flattening pass
# disappearing from the frame entirely.
#
# The Pixel C, the nearest relative of this board, does the same.
PRODUCT_PROPERTY_OVERRIDES += \
    persist.tegra.compression=0 \
    persist.tegra.decompression=disabled

# Latch without waiting, and tell SurfaceFlinger the truth about time.
#
# Android 9 refuses to latch a frame whose drawing the GPU has not yet
# finished, and re-asks a full refresh later. On this GPU a transition's
# frames are routinely still drawing at that moment, and with a queue of
# three buffers the one refusal freezes two of its slots: the application
# runs out of buffers and stands still, which cascades into losing every
# second or third tick of an animation. Android 7 never had the refusal --
# built without USE_HWC2 the check compiled to "yes" -- which is most of
# why it felt smooth. Latching unsignalled hands the wait to consumers
# that overlap it with their own work: the GPU waits before it reads, the
# kernel waits before it flips. Measured on the recents transition:
# skipped ticks fell from 32 in eight runs to one or two, and the wait
# for a buffer from tens of milliseconds to under one.
PRODUCT_PROPERTY_OVERRIDES += \
    debug.sf.latch_unsignaled=1

# With the refusal gone the present fence is honest again. Withholding it
# (the previous answer here) kept SurfaceFlinger from skipping frames, but
# also starved its model of when the panel refreshes: the model was reset
# every half second, rebuilt from a couple of hardware samples, and
# wandered -- transitions came out a different length every run. The
# composer now hands over this flip's own fence unconditionally; the
# debug-era vendor.hwc.fence switch is gone with the debugging it served.
#
# The honest fence would wake the very check the old answer was dodging:
# a frame whose fence has not signalled by the next wake-up is dropped
# whole, with no grace at all. Later Android softened that check to a
# grace period; here it is simply turned off, and a late frame is shown
# late instead of not at all.
PRODUCT_PROPERTY_OVERRIDES += \
    debug.sf.disable_backpressure=1

# The panel runs at sixty or, with its porch stretched, at thirty, and the
# composer offers both as one seamless group. Which one is the framework's
# call: after two and a half seconds with no new frame it takes the slowest
# rate its policy allows, and a touch puts it back at the fastest for as
# long as the finger is down and half a second after. Both timers are off
# unless set. A second was too short: the pause between the launcher's
# animation and an opening application's first frames outlasts it, and
# the application opened at thirty hertz.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.surface_flinger.set_idle_timer_ms=2500 \
    ro.surface_flinger.set_touch_timer_ms=500

# Input
#
# Touch resampling off, as ro.input.noresample=1 had it before. R reads the
# switch under a new name with the sense turned round (InputTransport.cpp),
# so the old one had quietly stopped doing anything.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.input.resampling=0

# Media
PRODUCT_PROPERTY_OVERRIDES += \
    media.stagefright.thumbnail.prefer_hw_codecs=true

# Radio
PRODUCT_PROPERTY_OVERRIDES += \
    ro.radio.noril=yes

# Storage
#
# sdcardfs under emulated storage, as ro.sys.sdcardfs=true asked for before.
# vold on R reads this name instead; true is its default, so this only says
# the choice out loud.
PRODUCT_PROPERTY_OVERRIDES += \
    external_storage.sdcardfs.enabled=true

# USB
#
# On Q this is not the list of functions to compose -- UsbDeviceManager owns
# that -- it is only where the framework keeps whether adb survives a reboot:
# it reads containsFunction(prop, "adb") at startup and rewrites the property
# itself when adb is turned on or off. Naming mtp here achieves nothing at the
# framework end and does real damage at init's, which takes the value
# literally and composes an mtp,adb gadget behind the HAL's back.
PRODUCT_PROPERTY_OVERRIDES += \
    persist.sys.usb.config=adb

# Wifi
#
# Wi-Fi Direct has no network device of its own here. The driver offers a P2P
# device instead, which the supplicant creates beside wlan0 as p2p-dev-wlan0.
# Both the Wi-Fi HAL and the framework take the name from this property, and
# left unset they ask for p2p0, which does not exist.
PRODUCT_PROPERTY_OVERRIDES += \
    wifi.interface=wlan0 \
    wifi.direct.interface=p2p-dev-wlan0 \
    persist.debug.wfd.enable=1

# Wi-Fi country. Without a SIM, R takes the country from ro.boot.wificountrycode
# and from nothing else: left unset, the framework has no country, the settings
# offer the hotspot 2.4 GHz only, and a 5 GHz hotspot is refused ("Failed to
# set country code, required for setting up soft ap in 5GHz"). It could come
# as androidboot.wificountrycode on the kernel cmdline, but that is baked into
# boot.img; here it is a system property, and a cmdline value would still win.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.boot.wificountrycode=US
