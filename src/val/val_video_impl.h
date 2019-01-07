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

#pragma once
#include "device_capability.h"
#include "driElements.h"
#include "logging.h"
#include <ls2-helpers/ls2-helpers.hpp>
#include <unordered_map>
#include <val_api.h>
#include <vector>

class SinkInfo
{
public:
    unsigned planeId;
    unsigned crtcId;
    unsigned connId;
    bool connected = false;

    SinkInfo(unsigned _planeId, unsigned _crtcId, unsigned _connId)
    {
        planeId = _planeId;
        crtcId  = _crtcId;
        connId  = _connId;
    }
};

class val_video_impl : public VAL_Video
{
private:
    std::vector<VAL_PLANE_T> logicalPlanes;
    std::vector<unsigned int> physicalPlanes;
    std::unordered_map<VAL_VIDEO_WID_T, SinkInfo *> videoSinks;
    DeviceCapability &mDeviceCapability;
    DRIElements driElements;

    bool isValidSink(VAL_VIDEO_WID_T wId);
    bool isSinkConnected(VAL_VIDEO_WID_T wId);
    void updatePlanes();
    bool isValidMode(VAL_VIDEO_SIZE_T win);

public:
    val_video_impl(DeviceCapability &capability);
    ~val_video_impl() {}

    bool connect(VAL_VIDEO_WID_T wId, VAL_VSC_INPUT_SRC_INFO_T vscInput, VAL_VSC_OUTPUT_MODE_T outputmode,
                 unsigned int *planeId);
    bool disconnect(VAL_VIDEO_WID_T wId);
    bool applyScaling(VAL_VIDEO_WID_T wId, VAL_VIDEO_RECT_T srcInfo, bool adaptive, VAL_VIDEO_RECT_T inRegion,
                      VAL_VIDEO_RECT_T outRegion);
    bool setDualVideo(bool enable);
    bool setCompositionParams(std::vector<VAL_WINDOW_INFO_T> zOrder);
    bool setWindowBlanking(VAL_VIDEO_WID_T wId, bool blank, VAL_VIDEO_RECT_T inRegion, VAL_VIDEO_RECT_T outRegion);

    bool setDisplayResolution(VAL_VIDEO_SIZE_T, uint8_t);
    std::vector<VAL_VIDEO_SIZE_T> getSupportedResolutions(uint8_t dispIndex = 0);
    VAL_VIDEO_RECT_T getDisplayResolution();

    bool getDeviceCapabilities(VAL_VIDEO_SIZE_T &minDownscaleSize, VAL_VIDEO_SIZE_T &maxUpscaleSize); // Deprecated
    std::vector<VAL_PLANE_T> getVideoPlanes();
    bool setParam(std::string control, pbnjson::JValue param) { return true; };
    pbnjson::JValue getParam(std::string control, pbnjson::JValue param);
};
