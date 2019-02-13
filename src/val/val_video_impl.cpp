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

#include "val_video_impl.h"
#include "driElements.h"
#include <algorithm>
#include <cinttypes>
#include <unordered_set>
#include <val/val_video.h>

VAL_VIDEO_RECT_T val_video_impl::getDisplayResolution() { return VAL_VIDEO_RECT_T{0, 0, 1920, 1280}; }

val_video_impl::val_video_impl(DeviceCapability &deviceCapability)
    : mDeviceCapability(deviceCapability),
      driElements(mDeviceCapability.getMaxResolution(), [this](void) { this->updatePlanes(); })
{
    const std::set<std::string> &planeNames = mDeviceCapability.getPlaneNames();
    int wid                                 = 0;

    for (auto &pstr : planeNames) {
        // TODO: window id should come from config file
        logicalPlanes.push_back(VAL_PLANE_T{(VAL_VIDEO_WID_T)wid++, pstr, mDeviceCapability.getMinResolution(),
                                            mDeviceCapability.getMaxResolution()});
    }

    // Acquire a pool of physical plane Ids (one per logical planes)
    std::vector<unsigned int> pplaneList = driElements.getPlanes();

    auto physicalPlaneId = pplaneList.begin();
    for (VAL_PLANE_T &plane : logicalPlanes) {
        if (physicalPlaneId != pplaneList.end()) {
            physicalPlanes.push_back(*physicalPlaneId);
            uint32_t crtcId = driElements.getCrtcId(*physicalPlaneId);
            uint32_t connId = driElements.getConnId(*physicalPlaneId);
            LOG_DEBUG("plane Name / wId : %s / %d, plane id : %d, crtc id : %d, conn id : %d", plane.planeName.c_str(),
                      plane.wId, *physicalPlaneId, crtcId, connId);
            videoSinks.insert(std::make_pair(plane.wId, new SinkInfo(*physicalPlaneId, crtcId, connId)));
            physicalPlaneId++;
        } else {
            LOG_DEBUG("insert dummy videoSinks for logical id of planes %d", plane.wId);
            videoSinks.insert(std::make_pair(plane.wId, new SinkInfo(0, 0, 0)));
        }
    }
    updatePlanes();
}

bool val_video_impl::getDeviceCapabilities(VAL_VIDEO_SIZE_T &minDownscaleSize, VAL_VIDEO_SIZE_T &maxUpscaleSize)
{
    // TODO::vc4 crtc property has max and min resolution.(0x0-2048x2048)
    // although tvservice/userland does not return this no matter what is connected

    maxUpscaleSize   = mDeviceCapability.getMaxResolution();
    minDownscaleSize = mDeviceCapability.getMinResolution();
    return true;
}

std::vector<VAL_PLANE_T> val_video_impl::getVideoPlanes() { return logicalPlanes; }

bool val_video_impl::isValidSink(VAL_VIDEO_WID_T wId)
{
    if (videoSinks.find(wId) == videoSinks.end()) {
        LOG_ERROR("INVALID_SINK", 0, "Invalid sink %d", wId);
        return false;
    }

    return true;
}

bool val_video_impl::isSinkConnected(VAL_VIDEO_WID_T wId)
{
    if (!isValidSink(wId)) {
        return false;
    }

    return videoSinks[wId]->connected;
}

bool val_video_impl::connect(VAL_VIDEO_WID_T wId, VAL_VSC_INPUT_SRC_INFO_T vscInput, VAL_VSC_OUTPUT_MODE_T outputmode,
                             unsigned int *planeId)
{
    if (!isValidSink(wId)) {
        return false;
    }

    if (isSinkConnected(wId)) {
        LOG_DEBUG("Sink %d already connected", wId);
        return true;
    }

    *planeId                   = videoSinks[wId]->planeId;
    videoSinks[wId]->connected = true;
    return true;
}

bool val_video_impl::disconnect(VAL_VIDEO_WID_T wId)
{
    LOG_DEBUG("disconnect called for wId %d", wId);
    if (!isSinkConnected(wId)) {
        LOG_DEBUG("Sink %d is not connected", wId);
        return false;
    }
    videoSinks[wId]->connected = false;
    return true;
    if (!driElements.setPlaneProperties(SET_PLANE_FB_T, videoSinks[wId]->planeId, 0)) {
        LOG_ERROR(MSGID_VIDEO_DISCONNECT_FAILED, 0, "Faild to  set properties for wId %d", wId);
        return false;
    }
    return true;
}

typedef struct {
    /* Signed dest location allows it to be partially off screen */
    int32_t crtc_x, crtc_y;
    uint32_t crtc_w, crtc_h;

    /* Source values are 16.16 fixed point */
    uint32_t src_x, src_y;
    uint32_t src_h, src_w;
} scale_param_t;

scale_param_t scale_param;

bool val_video_impl::applyScaling(VAL_VIDEO_WID_T wId, VAL_VIDEO_RECT_T srcInfo, bool adaptive,
                                  VAL_VIDEO_RECT_T inputRegion, VAL_VIDEO_RECT_T outputRegion)
{
    LOG_DEBUG("applyScaling called with srcInfo {x:%u, y:%u, w:%u, h:%u},"
              "inputRegion {x:%u, y:%u, w:%u, h:%u}, outputRegion {x:%u, y:%u, w:%u, h:%u}",
              srcInfo.x, srcInfo.y, srcInfo.w, srcInfo.h, inputRegion.x, inputRegion.y, inputRegion.w, inputRegion.h,
              outputRegion.x, outputRegion.y, outputRegion.w, outputRegion.h);

    if (!isSinkConnected(wId)) {
        LOG_DEBUG("Sink %d is not connected", wId);
        return false;
    }

    // 1 is used as a work around value for special purposes.
    if (1 < outputRegion.h && outputRegion.h < 12) {
        LOG_DEBUG("RPi h/w constraints. Height of %u is too low. It's not supported."
                  " This causes an error in the kernel and does not work properly",
                  outputRegion.h);
        return false;
    }

    scale_param = {outputRegion.x, outputRegion.y, outputRegion.w, outputRegion.h,
                   inputRegion.x,  inputRegion.y,  inputRegion.h,  inputRegion.w};

    LOG_DEBUG("Calling setPlaneProperties with scale_params %d, %d, %d, %d %d %d %d %d ", scale_param.crtc_x,
              scale_param.crtc_y, scale_param.crtc_w, scale_param.crtc_h, scale_param.src_x, scale_param.src_y,
              scale_param.src_w, scale_param.src_h);
    if (!driElements.setPlaneProperties(SET_SCALING_T, videoSinks[wId]->planeId, (uint64_t)&scale_param)) {
        LOG_ERROR(MSGID_VIDEO_SCALING_FAILED, 0, "Failed to apply scaling for plane %d", videoSinks[wId]->planeId);
        return true;
    }

    return true;
}

bool val_video_impl::setDualVideo(bool enable)
{ // Do nothing.
    return true;
}

bool val_video_impl::setCompositionParams(std::vector<VAL_WINDOW_INFO_T> zOrder)
{

    for (size_t i = 0; i < zOrder.size(); ++i) {
        LOG_DEBUG("zorder %d  for wId %d", i, zOrder[i].wId);
        if (!isValidSink(zOrder[i].wId)) {
            return false;
        }
    }
    uint64_t zarg = 0;
    for (size_t i = 0, p = physicalPlanes.size() - 1; i < zOrder.size() && p >= 0; ++i) {
        zarg = zarg << 16;
        zarg |= zOrder[i].wId;
    }

    uint16_t plane = driElements.getPlaneBase(); // plane must be any valid plane id.

    if (!driElements.setPlaneProperties(SET_Z_ORDER_T, plane, zarg)) {
        LOG_ERROR(MSGID_SET_ZORDER_FAILED, 0, "Failed to apply zorder for sink");
        return false;
    }
    return true;
}

bool val_video_impl::setWindowBlanking(VAL_VIDEO_WID_T wId, bool blank, VAL_VIDEO_RECT_T inputRegion,
                                       VAL_VIDEO_RECT_T outputRegion)
{
#if 0
    if (!isSinkConnected(wId)) {
        LOG_ERROR(MSGID_VIDEO_BLANKING_FAILED, 0, "Sink %d is not connected", wId);
        return false;
    }
#endif
    return true;

    if (blank) {
        /*FIX:PLAT-48894 Scaling is not working sometimes in Youtube app
          We cannot initialize the values from the same object which is being constructed */
        scale_param_t scale_param{outputRegion.x, outputRegion.y, outputRegion.w, outputRegion.h, 0, 0, 0, 0};

        LOG_DEBUG("Calling setPlaneProperties with scale_params %d, %d, %d, %d %d %d %d %d ", scale_param.crtc_x,
                  scale_param.crtc_y, scale_param.crtc_w, scale_param.crtc_h, scale_param.src_x, scale_param.src_y,
                  scale_param.src_h, scale_param.src_w);

        if (!driElements.setPlaneProperties(SET_SCALING_T, videoSinks[wId]->planeId, (uint64_t)&scale_param)) {
            LOG_ERROR(MSGID_VIDEO_BLANKING_FAILED, 0, "Failed to blank wId %d", wId);
            return false;
        }
    } else {
        scale_param_t scale_param{outputRegion.x, outputRegion.y, outputRegion.w, outputRegion.h,
                                  inputRegion.x,  inputRegion.y,  inputRegion.h,  inputRegion.w};

        LOG_DEBUG("Calling setPlaneProperties with scale_params %d, %d, %d, %d %d %d %d %d ", scale_param.crtc_x,
                  scale_param.crtc_y, scale_param.crtc_w, scale_param.crtc_h, scale_param.src_x, scale_param.src_y,
                  scale_param.src_w, scale_param.src_h);

        if (!driElements.setPlaneProperties(SET_SCALING_T, videoSinks[wId]->planeId, (uint64_t)&scale_param)) {
            LOG_ERROR(MSGID_VIDEO_UNBLANKING_FAILED, 0, "Failed to apply scaling for plane %d",
                      videoSinks[wId]->planeId);
            return false;
        }
    }

    return true;
}

bool val_video_impl::setDisplayResolution(VAL_VIDEO_SIZE_T win, uint8_t display_path)
{
    uint16_t numDisplay;

    if (!isValidMode(win)) {
        LOG_ERROR(MSGID_MODE_CHANGE_FAILED, 0, "Invalid resolution specified %dx%d ", win.w, win.h);
        return false;
    }

    numDisplay = driElements.getSupportedNumConnector();
    if (numDisplay <= display_path) {
        LOG_ERROR(MSGID_MODE_CHANGE_FAILED, 0, "Invalid display path specified %d ", display_path);
        return false;
    }
    if (driElements.changeMode(win.w, win.h, display_path)) {

        LOG_ERROR(MSGID_MODE_CHANGE_FAILED, 0, "Resolution change failed %dx%d ", win.w, win.h);
        return false;
    }
    return true;
}

void val_video_impl::updatePlanes() // callback function
{
    VAL_VIDEO_SIZE_T min = {};
    VAL_VIDEO_SIZE_T max = {};
    for (auto &p : this->logicalPlanes) {
        if (driElements.getModeRange(videoSinks[p.wId]->crtcId, min, max)) {
            if (mDeviceCapability.getMaxResolution().h >= max.h || mDeviceCapability.getMaxResolution().w >= max.w) {
                p.maxSizeT = max;
            }
        }
    }
}

bool val_video_impl::isValidMode(VAL_VIDEO_SIZE_T win)
{
    // TODO::what other checks?
    if (win.w <= mDeviceCapability.getMaxResolution().w && win.h <= mDeviceCapability.getMaxResolution().h &&
        win.w >= mDeviceCapability.getMinResolution().w && win.h >= mDeviceCapability.getMinResolution().h) {
        return true;
    }
    return false;
}

std::vector<VAL_VIDEO_SIZE_T> val_video_impl::getSupportedResolutions(uint8_t dispIndex)
{
    std::vector<VAL_VIDEO_SIZE_T> modes = driElements.getSupportedModes(dispIndex);
    modes.erase(std::remove_if(modes.begin(), modes.end(), [this](VAL_VIDEO_SIZE_T &m) { return !isValidMode(m); }),
                modes.end());
    return modes;
}

pbnjson::JValue val_video_impl::getParam(std::string control, pbnjson::JValue param)
{
    int ret       = false;
    int wId_param = 0;
    VAL_VIDEO_WID_T wId;

    LSHelpers::JsonParser parser{param};

    parser.get("wId", wId_param).optional(true);
    LOG_DEBUG("getParam control : %s for wId:%d", control.c_str(), wId_param);

    if (control == VAL_CTRL_DRM_RESOURCES) {
        int planeId = 0;
        int crtcId  = 0;
        int connId  = 0;

        wId = static_cast<VAL_VIDEO_WID_T>(wId_param);

        if (videoSinks.find(wId) != videoSinks.end()) {
            planeId = videoSinks[wId]->planeId;
            crtcId  = videoSinks[wId]->crtcId;
            connId  = videoSinks[wId]->connId;

            ret = true;
            return pbnjson::JValue{{"returnValue", ret}, {"planeId", planeId}, {"crtcId", crtcId}, {"connId", connId}};
        }
    } else if (control == VAL_CTRL_NUM_CONNECTOR) {
        int numConnector = 0;

        numConnector = driElements.getSupportedNumConnector();
        if (numConnector > 0) {
            ret = true;
            return pbnjson::JValue{{"returnValue", ret}, {"numConnector", numConnector}};
        }
    } else {
        LOG_DEBUG("Not supported control : %s", control.c_str());
        ret = false;
    }

    parser.finishParseOrThrow();

    return pbnjson::JValue{{"returnValue", ret}};
}
