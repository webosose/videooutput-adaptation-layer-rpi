// Copyright (c) 2017-2018 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "device_capability.h"
#include <val_api.h>

class val_impl : public VAL
{
public:
    val_impl() : mDevCap(getConfigFilePath()) {}
    ~val_impl() {}

    bool initialize();
    bool deinitialize();
    VAL_DEVICE_TYPE_T getDevice() { return VAL_DEV_RPI; }

private:
    DeviceCapability mDevCap;
    std::string getConfigFilePath();
};
