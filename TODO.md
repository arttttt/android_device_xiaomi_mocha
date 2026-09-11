# TODO

## HIDL: thermal

- [ ] **thermal** → binderized — blocked on hardware access:
    - Pull `thermalhal.<ro.hardware>.xml` from stock (`/system/etc/` or `/vendor/etc/`).
    - Dump `/sys/class/thermal/thermal_zone*/type` layout.
    - Vendor `LineageOS/android_hardware_nvidia_thermal` (branch `lineage-15.1`) into `hardware/nvidia/thermal/`, drop entry from `lineage.dependencies`, swap `@1.0-impl` for `@1.0-service`, add XML via `PRODUCT_COPY_FILES`, flip manifest.
    - Fallback if stock XML missing: synthesise from `Smoke-kernel-mocha` DT thermal-zones.

## Audio policy: on-device verification

Migration to XSD XML is done (`media/audio_policy_configuration.xml`,
`USE_XML_AUDIO_POLICY_CONF := 1`). Needs device-side checks once mocha
is reachable:

- All four modules (primary / a2dp / usb / r_submix) register with
  audiopolicyservice — check `dumpsys media.audio_policy` after boot.
- Routing through tinyhal paths in `media/audio.mocha.xml` still works
  for speaker, headphone, headset, internal mic.
- Bluetooth A2DP output streams.
- Volume curves from the AOSP-stock `audio_policy_volumes.xml` and
  `default_volume_tables.xml` (pulled via `PRODUCT_COPY_FILES` from
  `frameworks/av/services/audiopolicy/config/`) give acceptable levels.

## Sepolicy: on-device verification

Treble private/public/vendor split is done. The three-way classification:

- `public/` — type declarations visible from both sides: `device.te` (dev
  types), `file.te` (file/sysfs types), `property.te` (prop types),
  `service.te` (service types), `te_macros`.
- `private/` — augmentations to AOSP platform/system domains
  (system_server, zygote, surfaceflinger, cameraserver, mediaserver,
  init, kernel, platform_app, priv_app, etc.).
- `vendor/` — new NVIDIA/mocha domains (ussrd, bt_loader, gpsd, rpx,
  charon, ctload, …), HAL-side augments (wifi_hal_default), mocha bits
  (conn_wifi), plus `file_contexts`, `genfs_contexts`,
  `property_contexts`, `service_contexts`, `seapp_contexts`,
  `mac_permissions.xml`, `keys.conf`, `certs/`.

Likely surprises on first build/boot:

- Type-visibility errors: if a `private/` rule references a type that
  ended up in `vendor/`, the build fails with `type foo_t not defined`.
  Fix is usually to promote the type declaration to `public/`.
- `neverallow` violations from AOSP's strictened 8.1 CTS — the previous
  flat BOARD_SEPOLICY_DIRS was lenient; the split enforces more.
- `dumpsys` / `settings` may report extra denials (`logcat | grep avc`).

## Init.rc: dead code in init.tegra.rc

Found during init.cal.rc audit but left intact for safety. Needs
device-side verification before cleanup:

- `initfiles/init.tegra.rc:29-36` — mkdir `/mnt/factory`, `/factory`,
  `/factory/wifi_config`, `/mnt/usercalib`. mocha's fstab has no
  USRCL/FACTORY partitions, so nothing is mounted there — these mkdir's
  create empty unused directories at boot.
- `initfiles/init.tegra.rc:44-53` — chown/chmod on `/usercalib`, creating
  `/usercalib/lost+found`, `restorecon_recursive /usercalib`. Same story.
- `initfiles/init.tegra.rc:131-140` — permissions on
  `/sys/class/invensense/mpu/{akm89xx,bmpX80}/...`. These sysfs paths
  come from the **Invensense MPL userspace blob** (MotionProLink), which
  mocha doesn't ship. Only the in-kernel mpu6515 driver is present,
  exposing different paths (`/sys/class/invensense/mpu/...` top-level,
  which *are* used in lines 116-130 and should stay).

Low-risk to drop these chunks once someone can boot and confirm nothing
regresses. High-value for cleanliness: `init.tegra.rc` becomes a coherent
mocha file instead of a tn8-inheritance hodgepodge.

## Manifest coverage (out of scope for now)

- `camera.provider@2.4`, `gatekeeper@1.0`, `dumpstate@1.0` —
  adding manifest entries only makes sense once the matching HAL services
  exist on device. camera in particular has no HAL wired at all right now;
  gatekeeper would need either the tegra blob or the AOSP software
  fallback; dumpstate default impl is a stub. All require HAL-setup work
  before any manifest change.
