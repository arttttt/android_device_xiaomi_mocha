#
# Copyright (C) 2026 Artem Bambalov
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

"""Writes the secure world's image as part of installing the ROM.

The kernel on this board talks to a secure monitor and expects PSCI 0.1
behind it. A device whose TOS partition holds something else, or nothing,
does not get a diagnosable failure -- it gets a boot that goes wrong in ways
that look like anything but firmware. Asking people to flash it separately
has not worked, so the ROM carries it.

Only TOS. The two partitions that usually keep it company in scripts like
this are deliberately left alone:

  MSC is the boot control block, which recovery writes during the very
  install that would be overwriting it.

  USP stages a bootloader update, applied on the next boot. A mismatched
  one is how a board stops accepting our unsigned boot images, or stops
  booting at all. That does not belong in a ROM package.

Written in InstallBegin rather than InstallEnd so that a failure stops the
install before the system image has been touched.

The write is unconditional. Guarding it on the partition's current contents
was considered and dropped: the image is 1.42 MB in a 4 MB partition, and
the tail beyond it still holds 449444 bytes of an older, larger image on
this device -- so a hash of the partition says nothing portable, and a hash
of a slice is not something edify can take.
"""

TOS_IMAGE = "install/firmware-update/tos.img"
TOS_PARTITION = "/dev/block/platform/sdhci-tegra.3/by-name/TOS"


def _write_tos(info):
    info.script.AppendExtra('ui_print("Writing the secure world to TOS...");')
    info.script.AppendExtra(
        'package_extract_file("%s", "%s");' % (TOS_IMAGE, TOS_PARTITION))


def FullOTA_InstallBegin(info):
    _write_tos(info)


# No incremental counterpart on purpose: an incremental arrives on a device
# that a full package has already installed, so the image it would write is
# the one already there.
