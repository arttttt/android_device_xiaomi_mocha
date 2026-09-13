#
# Copyright (C) 2015 The Android Open Source Project
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

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_VENDOR_MODULE := true

# android.hardware.gatekeeper@1.0-service carries no crypto of its own: it
# looks the implementation up with hw_get_module_by_class(), which walks
# ro.hardware, ro.product.board, ro.board.platform and ro.arch in that order
# before trying "default". ro.hardware is tn8 here and ro.product.board is
# empty, so ro.board.platform is what this name answers to -- and platform is
# the honest scope, since nothing below is specific to one board.
LOCAL_MODULE := gatekeeper.tegra
LOCAL_MODULE_RELATIVE_PATH := hw

LOCAL_CFLAGS := -Wall -Wextra -Werror -Wunused
LOCAL_SRC_FILES := \
	module.cpp \
	SoftGateKeeperDevice.cpp

LOCAL_SHARED_LIBRARIES := \
	libbinder \
	libgatekeeper \
	liblog \
	libhardware \
	libbase \
	libutils \
	libcrypto \
	libhidlbase \
	libhidltransport \
	libhwbinder \
	android.hardware.gatekeeper@1.0 \

LOCAL_STATIC_LIBRARIES := libscrypt_static
LOCAL_C_INCLUDES := external/scrypt/lib/crypto
include $(BUILD_SHARED_LIBRARY)
