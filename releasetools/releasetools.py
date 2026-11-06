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

"""Writes the secure world, and stages the bootloaders, as part of
installing the ROM.

The kernel on this board talks to a secure monitor and expects PSCI 0.2
behind it -- which is why the image carries that in its name, because the
next person to touch this will need to know which of the two worlds it is.
It is the Shield Tablet's TLK built in February 2017: CPU_ON, CPU_OFF,
CPU_SUSPEND and SYSTEM_RESET in the monitor, a monitor vector set on every
CPU as it comes up. Its SYSTEM_OFF is a stub that halts; the kernel powers
the board off through the PMIC and never asks it to. The 2015 TLK this
replaced answers neither PSCI_VERSION nor SYSTEM_RESET, so it cannot stand
behind this kernel.

It is not carried as NVIDIA built it. That TLK copies the encrypted keys
into TZRAM with a length it reads from DRAM, in the word after the boot
arguments' text -- a word the Shield's bootloader writes on every start and
this board's does not: it puts the keys in TZRAM itself and leaves the word
alone. After power-on the word is zero. After a reboot it is whatever the
kernel left there, 0x0a000003 on this kernel, and the copy runs off the end
of TZRAM's 64 KB: the board stops after "Welcome to TLK" until the battery
is flat. The image here refuses a length beyond TZRAM and skips the copy.
It also sends TLK's printf to the UART and prints a letter at each step of
its start, which is how this was found and costs nothing. The original's
sha256 is a0519543...d2fcf2, the patched one 00161e21...593300.

A board whose TOS partition holds something else, or nothing, does not fail
in a way anyone can read: it fails like a kernel, a driver, anything but
firmware. Asking people to flash it separately has not worked, so the
package carries it and writes it.

The bootloaders are carried for the same reason. The TLK fix above answers
how one particular nvtboot behaves, and boards are out there with whichever
MIUI's bootloaders they last had. What is tested is MIUI V9.2.4.0's, so
that is what every board is brought to.

They cannot be written the way TOS is. nvtboot loads fastboot from EBT, or
from its copy in RBL, at the place, length and AES-CMAC hash the BCT records
for it, and it checks the hash on every board, fused or not. An image
written over EBT alone fails the check and nvtboot quietly boots the copy in
RBL; written over both, it finds no bootloader at all and the board gets no
further than RCM. Changing them means changing the BCT with them, and the
only thing here that does that is fastboot itself.

So they are handed to fastboot the way MIUI did it. Xiaomi's own recovery
package for V9.2.4.0 writes a signed blob to USP; on the first start that
is not into recovery, fastboot checks the blob's signature, writes each
image, rehashes the bootloaders into the BCT, clears USP and restarts. The
blob here is that one, unchanged -- the signature is Xiaomi's, so it cannot
be -- taken from miui_MIPAD_V9.2.4.0.KXFCNEK_97354839c6_4.4.zip, sha256
e07800ba...88c14af. It holds EBT and RBL (bootloader.bin), NVC and RMB
(nvtboot.bin), WB0 and DFI, byte for byte what V9.2.4.0's fastboot package
flashes. It holds no TOS, so it does not undo the one above, and no BCT, so
the memory parameters stay the ones the board shipped with.

It is staged only when EBT, NVC or WB0 differ from it. Applying it rewrites
the BCT, and a board that loses power right then is one for RCM: a risk
worth taking once, on a board that needs it, and not on every install.

MSC is refused: it is the boot control block, which recovery writes during
the very install that would be overwriting it.

Both are written in InstallBegin rather than InstallEnd so that a failure
stops the install before the system image has been touched.

Neither is written when the partition already holds the image. A partition
is larger than its image, and past the image a board can still hold most of
an older and larger one, so the whole partition's hash says nothing; what is
compared is the image's length from the start of the partition, which
patch_partition_check reads and hashes on its own. The hash is taken from
the image in the target files at build time, so it follows the image
rather than a constant someone has to remember to change.
"""

import hashlib
import struct

TOS_IMAGE = "install/firmware-update/tos-psci-0.2.img"
BLOB_IMAGE = "install/firmware-update/bootloader-miui-V9.2.4.0.blob"

# The partitions whose images decide whether the blob is staged: the two
# bootloaders and the LP0 resume code. RBL and RMB are copies that nothing
# boots while EBT and NVC are good, and DFI is the USB 3 controller's
# firmware, which no bootloader version changed.
BLOB_CHECKED = ("EBT", "NVC", "WB0")

# Where the partitions can be reached, best first. Every entry was read off
# a device sitting in the recovery this runs in -- TWRP 3.2.1, omni_mocha
# 7.1.2 base -- rather than guessed.
#
# The by-name links are the ones to want: they are addressed by the label in
# the partition table, so the numbering can move and they stay right. What
# is not fixed is the directory holding them, which is named after the
# platform device that owns the eMMC, and that name is the kernel's. A
# board-file kernel like ours calls it sdhci-tegra.3; a device-tree one
# calls it after the register address. That recovery has both, the second a
# symlink to the first, and for TOS both resolve to mmcblk0p6 -- so either
# spelling works there and the pair covers a kernel that only has one of
# them.
#
# Two entries and no third. A flat /dev/block/by-name was in this list and
# came out because that recovery has no such directory. The raw node the two
# links resolve to was in it as well, and came out for a better reason: a
# partition number is not a name. It is true for the table this board ships
# today and says nothing about the table in front of the script -- and being
# wrong there does not fail, it writes over whatever partition carries that
# number. That is the failure by-name exists to make impossible, and there
# is no point reaching for it as a fallback.
#
# So if neither name resolves, the install stops. A board with no by-name
# directory needs a human, not a guess.
BY_NAME = [
    "/dev/block/platform/sdhci-tegra.3/by-name/",
    "/dev/block/platform/700b0600.sdhci/by-name/",
]


def _paths(partition):
    return [directory + partition for directory in BY_NAME]


def _read_image(info, image):
    return info.input_zip.read("INSTALL/firmware-update/" +
                               image.rsplit("/", 1)[1])


def _holds(partition, image):
    """An edify condition: the partition starts with the image."""
    spec = "%d:%s" % (len(image), hashlib.sha1(image).hexdigest())
    return "(" + " || ".join(
        'patch_partition_check("EMMC:%s:%s", "EMMC:%s:%s")'
        % (path, spec, path, spec)
        for path in _paths(partition)) + ")"


def _extract(image, partition):
    """An edify statement writing the image to the partition, or stopping.

    edify's || is short-circuiting, so the first write that succeeds is the
    only one that happens. Failing all of them aborts rather than carrying
    on quietly: a ROM installed onto a board whose firmware was not written
    is the exact state this file exists to prevent.
    """
    attempts = ['package_extract_file("%s", "%s")' % (image, path)
                for path in _paths(partition)]
    attempts.append('abort("%s not found: looked at %s")'
                    % (partition, ", ".join(_paths(partition))))
    return " ||\n    ".join(attempts)


def _blob_images(blob):
    """The images in a signed blob, by the partition each is for.

    The layout is fastboot's, from NVIDIA's recovery_utils.c: a header with
    the magic, the size of what is signed and the size of the signature,
    then an update header whose entries name a partition and give the
    image's offset and length, both counted from the end of the first
    header. Anything else is not the blob this was written for, and stops
    the build.
    """
    signed = struct.Struct("<20sII")
    magic, _, _ = signed.unpack_from(blob, 0)
    if magic != b"-SIGNED-BY-SIGNBLOB-":
        raise ValueError("not a signed blob")
    base = signed.size

    magic, _, _, entries, count = struct.unpack_from("<16sIIII", blob, base)
    if magic != b"MSM-RADIO-UPDATE":
        raise ValueError("no update header in the blob")

    images = {}
    entry = struct.Struct("<4sIII")
    for i in range(count):
        name, offset, length, _ = entry.unpack_from(
            blob, base + entries + entry.size * i)
        images[name.rstrip(b"\0").decode()] = \
            blob[base + offset:base + offset + length]
    return images


def _write_tos(info):
    image = _read_image(info, TOS_IMAGE)
    info.script.AppendExtra(
        'if ' + _holds("TOS", image) + ' then\n'
        '  ui_print("The secure world in TOS is already this one.");\n'
        'else\n'
        '  ui_print("Writing the secure world (PSCI 0.2) to TOS...");\n'
        '  ' + _extract(TOS_IMAGE, "TOS") + ';\n'
        'endif;')


def _stage_bootloaders(info):
    images = _blob_images(_read_image(info, BLOB_IMAGE))
    present = " && ".join(_holds(partition, images[partition])
                          for partition in BLOB_CHECKED)
    info.script.AppendExtra(
        'if ' + present + ' then\n'
        '  ui_print("The bootloaders are already MIUI V9.2.4.0\'s.");\n'
        'else\n'
        '  ui_print("Staging MIUI V9.2.4.0\'s bootloaders in USP...");\n'
        '  ui_print("They are installed on the next start, which then");\n'
        '  ui_print("restarts the tablet once by itself.");\n'
        '  ' + _extract(BLOB_IMAGE, "USP") + ';\n'
        'endif;')


def FullOTA_InstallBegin(info):
    _write_tos(info)
    _stage_bootloaders(info)


# No incremental counterpart on purpose: an incremental arrives on a board a
# full package has already installed, where the images are already in place.
