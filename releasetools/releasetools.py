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
behind it -- which is why the image carries that in its name, because the
next person to touch this will need to know which of the two worlds it is.
A board whose TOS partition holds something else, or nothing, does not fail
in a way anyone can read: it fails like a kernel, a driver, anything but
firmware. Asking people to flash it separately has not worked, so the
package carries it and writes it.

Only TOS. Scripts of this shape usually write two partitions beside it, and
both are refused here:

  MSC is the boot control block, which recovery writes during the very
  install that would be overwriting it.

  USP stages a bootloader update, applied on the next boot. A mismatched
  one is how a board stops accepting unsigned boot images, or stops
  booting. That does not belong in a ROM package.

Written in InstallBegin rather than InstallEnd so that a failure stops the
install before the system image has been touched.

The write is unconditional. Guarding it on the partition's current contents
was considered and dropped: the image is 1.42 MB in a 4 MB partition, and
past it a device can still hold most of an older and larger image -- so a
hash of the partition says nothing portable, and edify cannot hash a slice.
"""

TOS_IMAGE = "install/firmware-update/tos-psci-0.1.img"

# Where TOS can be reached, best first. Every entry was read off a device
# sitting in the recovery this runs in -- TWRP 3.2.1, omni_mocha 7.1.2 base
# -- rather than guessed.
#
# The by-name links are the ones to want: they are addressed by the label in
# the partition table, so the numbering can move and they stay right. What
# is not fixed is the directory holding them, which is named after the
# platform device that owns the eMMC, and that name is the kernel's. A
# board-file kernel like ours calls it sdhci-tegra.3; a device-tree one
# calls it after the register address. That recovery has both, the second a
# symlink to the first, and both resolve to mmcblk0p6 -- so either spelling
# works there and the pair covers a kernel that only has one of them.
#
# Two entries and no third. A flat /dev/block/by-name was in this list and
# came out because that recovery has no such directory. The raw node the two
# links resolve to was in it as well, and came out for a better reason: a
# partition number is not a name. It is true for the table this board ships
# today and says nothing about the table in front of the script -- and being
# wrong there does not fail, it writes 1.42 MB over whatever partition six
# happens to be. That is the failure by-name exists to make impossible, and
# there is no point reaching for it as a fallback.
#
# So if neither name resolves, the install stops. A board with no by-name
# directory needs a human, not a guess.
TOS_PARTITIONS = [
    "/dev/block/platform/sdhci-tegra.3/by-name/TOS",
    "/dev/block/platform/700b0600.sdhci/by-name/TOS",
]


def _write_tos(info):
    """Tries each path in turn and stops the install if none of them took it.

    edify's || is short-circuiting, so the first write that succeeds is the
    only one that happens. Failing all of them aborts rather than carrying
    on quietly: a ROM installed onto a board whose secure world was not
    written is the exact state this file exists to prevent.
    """
    info.script.AppendExtra(
        'ui_print("Writing the secure world (PSCI 0.1) to TOS...");')

    attempts = ['package_extract_file("%s", "%s")' % (TOS_IMAGE, partition)
                for partition in TOS_PARTITIONS]
    attempts.append('abort("TOS not found: looked at ' +
                    ', '.join(TOS_PARTITIONS) + '")')

    info.script.AppendExtra(' ||\n    '.join(attempts) + ';')


def FullOTA_InstallBegin(info):
    _write_tos(info)


# No incremental counterpart on purpose: an incremental arrives on a board a
# full package has already installed, where the image is already in place.
