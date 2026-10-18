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

LOCAL_PATH := $(call my-dir)

# Packages the inherited product lists and this board does without. Make
# cannot take a package back out of an inherited PRODUCT_PACKAGES, so this
# module, which installs nothing, overrides them instead.
#
# TeleService: there is no modem. Every handheld product has it on S --
# generic_no_telephony.mk inherits handheld_system.mk, which lists it -- and
# with it comes a Phone and a RIL even with the overlay saying the board has
# no voice, SMS, data or SIM slot: PhoneFactory counts modems through the
# context-less TelephonyManager.getDefault(), which never reads the overlay.
# Without it the "phone" and "isub" services are not there and callers take
# their no-telephony paths. Telecom, which in-app calls use, is a separate
# package and stays.
include $(CLEAR_VARS)
LOCAL_MODULE := RemovePackages
LOCAL_MODULE_CLASS := APPS
LOCAL_MODULE_TAGS := optional
LOCAL_OVERRIDES_PACKAGES := \
    TeleService
LOCAL_UNINSTALLABLE_MODULE := true
LOCAL_CERTIFICATE := PRESIGNED
LOCAL_SRC_FILES := /dev/null
include $(BUILD_PREBUILT)
