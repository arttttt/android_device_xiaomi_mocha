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

# This takes the name the vendor blob used to answer to, and the blob is
# installed beside it as gralloc.nvidia instead. hw_get_module_by_class walks
# ro.hardware, ro.product.board, ro.board.platform and ro.arch in that order:
# ro.hardware is tn8 here and no gralloc.tn8 exists, ro.product.board is empty,
# so ro.board.platform is what this name answers to. Naming it for the platform
# rather than the board also means anything that reaches for
# /vendor/lib/hw/gralloc.tegra.so by path finds the wrapper, which is the
# point of wrapping at all.

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := gralloc.tegra
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_VENDOR_MODULE := true
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := gralloc_tegra.cpp

LOCAL_SHARED_LIBRARIES := \
    libdl \
    libhardware \
    liblog

LOCAL_CFLAGS := -Wall -Wextra -Werror

include $(BUILD_SHARED_LIBRARY)
