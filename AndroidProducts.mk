#
# Copyright (C) 2014 The CyanogenMod Project
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

# From P on, a device tree has to name its product makefiles here: the build
# system no longer discovers them by scanning, and without this the only
# symptom is
#   error: Can not locate config makefile for product "lineage_mocha".
# which says nothing about where it looked.
PRODUCT_MAKEFILES := \
    $(LOCAL_DIR)/lineage_mocha.mk
