// Copyright (c) 2017-2019 LG Electronics, Inc.
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
#include "val_settings_impl.h"

using namespace std::placeholders;

VAL_ControlSettings_Impl::VAL_ControlSettings_Impl() {}

bool VAL_ControlSettings_Impl::configureVideoSettings(const std::string control, const VAL_VIDEO_WID_T winID,
                                                      const int32_t controlVal[])
{
    return true;
}
