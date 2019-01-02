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

#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <glib.h>
#include <inttypes.h>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <vector>

#include "driElements.h"
#include "edid.h"
#include <drm_fourcc.h>
#include <val/val_video.h>

#define DRM_MODULE "vc4"

DRIElements::DRIElements(VAL_VIDEO_SIZE_T defMode, std::function<void()> p)
    : mValCallBack(p), mInitialMode(defMode), mConfiguredMode(defMode)
{
    mUDev = new UDev([this](std::string node) { updateDevice(node); });
    loadResources();

    auto devPair = mDeviceList.begin();
    if (devPair != mDeviceList.end()) {
        DriDevice &device = devPair->second;

        updateDevice(devPair->first);

        for (auto &crtc : device.crtcList) {
            LOG_DEBUG("\n set Active mode for crtc_id : %d(crtc_index : %d)", crtc.mCrtc->crtc_id, crtc.crtc_index);
            device.setActiveMode(crtc, static_cast<uint32_t>(crtc.max.w), static_cast<uint32_t>(crtc.max.h));
        }
        mPrimaryDev = devPair->first;
    }
    setupDevicePolling();
}

void DRIElements::loadResources()
{
    std::vector<std::string> uDevices = mUDev->getDeviceList();
    for (auto node : uDevices) {
        std::string udevNode = node;
        if (udevNode.find("card") == udevNode.npos) {
            continue;
        }

        // mDeviceList.emplace(udevNode, DriDevice{});
        mDeviceList.emplace(std::piecewise_construct, std::make_tuple(udevNode), std::make_tuple());
        DriDevice &device  = mDeviceList[udevNode];
        device.deviceName  = udevNode;
        device.drmModuleFd = open(udevNode.c_str(), O_RDWR | O_CLOEXEC);
        if (device.drmModuleFd < 0) {
            // THROW_FATAL_EXCEPTION("Failed to open the card %d", udevNode);
            LOG_ERROR(MSGID_DEVICE_ERROR, 0, "Failed to open  %d", udevNode);
        }

        /*
        // Below should be enabled if videooutputd uses primary planes
        // without DRM_CLIENT_CAP_UNIVERSAL_PLANES, videooutputd cannot receive information about primary planes
        if (drmSetClientCap(device.drmModuleFd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) == 0)
        {
                LOG_DEBUG(MSGID_DEVICE_STATUS,0,"DRM_CLIENT_CAP_UNIVERSAL_PLANES is supported");
        }
        else
        {
                LOG_DEBUG(MSGID_DEVICE_ERROR,0,"DRM_CLIENT_CAP_UNIVERSAL_PLANES is not supported");
        }
        */
        drmModeResPtr res = drmModeGetResources(device.drmModuleFd);
        if (!res) {
            LOG_ERROR(MSGID_DEVICE_ERROR, 0, "Failed to get drm resources for %s", device.deviceName);
        }
        // build crtc list
        for (int i = 0; i < res->count_crtcs; i++) {
            DrmCrtc drmCrtc(drmModeGetCrtc(device.drmModuleFd, res->crtcs[i]), static_cast<uint32_t>(i));
            device.crtcList.push_back(drmCrtc);
        }
        // build connector list
        for (int i = 0; i < res->count_connectors; i++) {
            drmModeConnector *connector = drmModeGetConnector(device.drmModuleFd, res->connectors[i]);
            DrmConnector drmConnector(device.drmModuleFd, connector);
            device.connectorList.push_back(drmConnector);
        }

        // build encoder list
        for (int i = 0; i < res->count_encoders; i++) {
            DrmEncoder drmEncoder(drmModeGetEncoder(device.drmModuleFd, res->encoders[i]));
            device.encoderList.push_back(drmEncoder);
        }

        // build plane list
        drmModePlaneResPtr planeRes = drmModeGetPlaneResources(device.drmModuleFd);

        if (!planeRes) {
            LOG_ERROR(MSGID_DEVICE_ERROR, 0, "drmModeGetPlaneResources failed: %s\n", strerror(errno));
        }

        for (size_t i = 0; i < planeRes->count_planes; i++) {
            drmModePlane *plane = drmModeGetPlane(device.drmModuleFd, planeRes->planes[i]);
            if (getPlaneType(device.drmModuleFd, plane->plane_id) != CURSOR) {
                DrmPlane drmPlane(plane);
                device.planeList.push_back(drmPlane);
            }
        }
    }
}

void DRIElements::updateDevice(std::string name) // callback from udev
{

    LOG_DEBUG("Update device called \n************************\n");
    VAL_VIDEO_SIZE_T maxSize, minSize;
    auto devPair = mDeviceList.find(name);
    if (devPair != mDeviceList.end()) {
        VAL_VIDEO_SIZE_T confMode;
        DriDevice &device = devPair->second;
        if (mConfiguredMode.w != mInitialMode.w || mConfiguredMode.h != mInitialMode.h) {
            confMode.w = mInitialMode.w;
            confMode.h = mInitialMode.h;
        } else {
            confMode.w = mConfiguredMode.w;
            confMode.h = mConfiguredMode.h;
        }

        device.setupDevice(confMode);

        mValCallBack();

    } else {
        LOG_ERROR(MSGID_DEVICE_ERROR, 0, "Cannot handle new DRM device detected %s", name.c_str());
    }
}

int DRIElements::changeMode(uint32_t width, uint32_t height, uint8_t display_path, uint32_t vRefresh)
{
    // RPI has Single card, so use device
    DriDevice &device = mDeviceList[mPrimaryDev];

    // find connector based on display path
    // It is assumed that the connectorList stores the display in order from the primary.
    //(display_path 0 means primary display, 1 means secondary display.)
    uint8_t dIdx     = 0;
    uint32_t crtc_id = 0;
    for (auto &conn : device.connectorList) {
        if (conn.isPlugged() && (dIdx == display_path)) {
            crtc_id = conn.crtc_id;
            break;
        }
        dIdx++;
    }

    for (auto &crtc : device.crtcList) {
        if (crtc.mCrtc->crtc_id == crtc_id) {
            if (!device.setActiveMode(crtc, width, height)) {
                // TODO:: Once set this value is not used .. remove it?
                crtc.max.w = width;
                crtc.max.h = height;
                // change mConfigResolution instead
                mConfiguredMode.h = height;
                mConfiguredMode.w = width;
                return true;
            }
            break;
        }
    }

    return false;
}

bool DRIElements::getModeRange(uint32_t crtcId, VAL_VIDEO_SIZE_T &minSize, VAL_VIDEO_SIZE_T &maxSize)
{
    DriDevice &device = mDeviceList[mPrimaryDev];
    auto crtc         = std::find_if(device.crtcList.begin(), device.crtcList.end(),
                             [crtcId](DrmCrtc &c) { return c.mCrtc->crtc_id == crtcId; });
    if (crtc != device.crtcList.end()) {
        minSize.w = crtc->min.w;
        minSize.h = crtc->min.h;
        maxSize.w = crtc->max.w;
        maxSize.h = crtc->max.h;
        return true;
    }
    return false;
}

int DriDevice::geModeRange(VAL_VIDEO_SIZE_T &minSize, VAL_VIDEO_SIZE_T &maxSize)
{
    // Get the min and max from the first valid connector to notify val
    for (auto conn = connectorList.begin(); conn != connectorList.end(); ++conn) {
        if (conn->isPlugged()) {
            LOG_DEBUG("isPlugged returned true ");
            DrmDisplayMode min, max;
            conn->getModeRange(min, max);
            maxSize.w = max.mModeInfoPtr->hdisplay;
            maxSize.h = max.mModeInfoPtr->vdisplay;

            minSize.w = min.mModeInfoPtr->hdisplay;
            minSize.h = min.mModeInfoPtr->vdisplay;
            LOG_DEBUG("\n max: %d x %d min: %d x %d", maxSize.w, maxSize.h, minSize.w, minSize.h);

            return 0;
        }
    }
    return -1;
}

int DriDevice::setupDevice(VAL_VIDEO_SIZE_T &confMode)
{
    if (!hasDumbBuffChecked) {
        hasDumbBuff();
        hasDumbBuffChecked = true;
    }

    for (auto &conn : connectorList) {
        uint32_t crtcId = 0;
        uint32_t connId = conn.mConnectorPtr->connector_id;
        if (!conn.isPlugged())
            continue;

        crtcId = findCrtc(conn);

        if (!crtcId) {
            LOG_ERROR(MSGID_DEVICE_ERROR, 0, "no valid crtc for connector %d", conn.mConnectorPtr->connector_id);
            continue;
        }

        // associate crtcId to connector. used if planes are updated based on connector name
        conn.setCrtcId(crtcId);
        DrmDisplayMode min, max;
        conn.getModeRange(min, max);
        // associate the connector to crtcz
        auto crtc =
            std::find_if(crtcList.begin(), crtcList.end(), [crtcId](DrmCrtc &c) { return c.mCrtc->crtc_id == crtcId; });
        if (crtc != crtcList.end()) {
            if (crtc->connectors.size())
                crtc->connectors.clear();
            crtc->connectors.insert(connId);

            if ((max.mModeInfoPtr->hdisplay < confMode.w || max.mModeInfoPtr->vdisplay < confMode.h) &&
                max.mModeInfoPtr->hdisplay != 0 && max.mModeInfoPtr->vdisplay != 0) {
                crtc->max.w = max.mModeInfoPtr->hdisplay;
                crtc->max.h = max.mModeInfoPtr->vdisplay;
            } else {
                crtc->max.w = confMode.w;
                crtc->max.h = confMode.h;
            }
            crtc->min.w = min.mModeInfoPtr->hdisplay;
            crtc->min.h = min.mModeInfoPtr->vdisplay;
        }
    }
    return 0;
}

uint32_t DriDevice::findCrtc(DrmConnector &conn)
{
    drmModeEncoder *enc = nullptr;
    int32_t crtc        = 0;
    /* try the currently conected encoder+crtc */
    if (conn.mConnectorPtr->encoder_id) {
        enc = drmModeGetEncoder(drmModuleFd, conn.mConnectorPtr->encoder_id);
    }
    if (enc) {
        if (enc->crtc_id) {
            crtc = enc->crtc_id;
        }
        drmModeFreeEncoder(enc);
        return crtc;
    }

    drmModeResPtr res = drmModeGetResources(drmModuleFd);

    /* if connector does not have encoder+crtc connected*/
    for (int i = 0; i < conn.mConnectorPtr->count_encoders; i++) {
        enc = drmModeGetEncoder(drmModuleFd, conn.mConnectorPtr->encoders[i]);
        if (!enc) {
            LOG_DEBUG("encoder associated with connector not found");
            continue;
        }
        for (int j = 0; j < res->count_crtcs; j++) {
            if (!(enc->possible_crtcs & (1 << j))) {
                continue;
            }
            crtc = res->crtcs[j];
            if (crtc >= 0) {
                break;
            }
        }
        drmModeFreeEncoder(enc);
    }
    drmModeFreeResources(res);
    return crtc;
}

uint32_t DriDevice::findCrtc(uint32_t planeId)
{
    uint32_t crtc_id = 0;
    auto plane       = std::find_if(planeList.begin(), planeList.end(),
                              [planeId](DrmPlane &p) { return planeId == p.mDrmPlane->plane_id; });

    uint32_t crtc_index = ffs(plane->mDrmPlane->possible_crtcs) - 1;

    auto crtc =
        std::find_if(crtcList.begin(), crtcList.end(), [crtc_index](DrmCrtc &c) { return crtc_index == c.crtc_index; });

    if (crtc != crtcList.end())
        crtc_id = crtc->mCrtc->crtc_id;

    return crtc_id;
}

uint32_t DriDevice::findConnector(uint32_t planeId)
{
    uint32_t conn_id = 0;
    uint32_t crtc_id = findCrtc(planeId);

    if (crtc_id) {
        auto conn = std::find_if(connectorList.begin(), connectorList.end(),
                                 [crtc_id](DrmConnector &c) { return crtc_id == c.crtc_id; });
        if (conn != connectorList.end())
            conn_id = conn->mConnectorPtr->connector_id;
    }

    return conn_id;
}

int DriDevice::setActiveMode(DrmCrtc &crtc, const uint32_t width, const uint32_t height, const uint32_t vRefresh)
{
    std::stringstream modeStr;
    modeStr << width << "x" << height;
    LOG_DEBUG("\n setActiveMode to %s", modeStr.str().c_str());
    // If there are no connectors dont set mode.
    if (!crtc.connectors.size()) {
        LOG_INFO(MSGID_DEVICE_STATUS, 0, "No connectors set for crtc %d", crtc.mCrtc->crtc_id);
        return -1;
    }
    LOG_DEBUG("connectors has been set for crtc %d", crtc.mCrtc->crtc_id);

    DrmDisplayMode mode;
    // Check that all connectors connected to this crtc supports this mode.
    // Currently there is only 1 connector.
    for (auto connId : crtc.connectors) {
        auto conn = std::find_if(connectorList.begin(), connectorList.end(),
                                 [connId](DrmConnector &c) { return c.mConnectorPtr->connector_id == connId; });
        if (!conn->isPlugged()) {
            LOG_DEBUG("ignoring unused connector %d", connId);
            continue;
        }

        if (!conn->isModeSupported(modeStr.str())) {
            LOG_ERROR(MSGID_INVALID_DISPLAY_MODE, 0, "Mode %s is not supported by %d", modeStr.str(),
                      conn->mConnectorPtr->connector_id);
            return -1;
        }

        if (!mode.mModeInfoPtr)
            mode = conn->getMode(modeStr.str());
    }

    if (!mode.mModeInfoPtr) {
        LOG_ERROR(MSGID_DISPLAY_NOT_CONNECTED, 0,
                  "cannot get a valid mode object or connector not connected for crtc %d", crtc.mCrtc->crtc_id);
        return -1;
    }

    // create a new Fb if current fb size is different
    if (crtc.createScanoutFb(drmModuleFd, width, height)) // create failed
    {
        return -1;
    }

    uint32_t *conn_ids = (uint32_t *)calloc(crtc.connectors.size(), sizeof(uint32_t));
    int index          = 0;
    for (auto connId : crtc.connectors) {
        conn_ids[index++] = connId;
    }
    LOG_DEBUG("crtc id : %d, scanout fb Id : %d", crtc.mCrtc->crtc_id, crtc.scanout_fbId);
    for (int idx = 0; idx < (int)crtc.connectors.size(); idx++) {
        LOG_DEBUG("conn_idx[%d] = %d", idx, conn_ids[idx]);
    }
    int ret = drmModeSetCrtc(drmModuleFd, crtc.mCrtc->crtc_id, crtc.scanout_fbId, 0, 0, conn_ids,
                             crtc.connectors.size(), mode.mModeInfoPtr);
    if (ret) {
        LOG_ERROR(MSGID_DRM_MODESET_ERROR, 0, "Failed to set mode %d", ret);
    }
    return 0;
}

int DriDevice::hasDumbBuff()
{
    uint64_t has_dumb;

    if (drmGetCap(drmModuleFd, DRM_CAP_DUMB_BUFFER, &has_dumb) < 0) {
        THROW_FATAL_EXCEPTION("drm device  does not support dumb buffers!\n");
        return -EOPNOTSUPP;
    }
    if (!has_dumb) {
        THROW_FATAL_EXCEPTION("drm device  does not support dumb buffers!\n");
        return -EOPNOTSUPP;
    }
    return 0;
}

int DrmCrtc::createScanoutFb(const int fd, const uint32_t width, const uint32_t height)
{

    uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
    unsigned int fb_id;

    int ret;
    if (boHandle && scanout_fbId != 0) {
        drmModeRmFB(fd, scanout_fbId);
        bo_destroy(boHandle);
    }

    struct bo *bo = bo_create(fd, DEFAULT_PIXEL_FORMAT, width, height, handles, pitches, offsets);
    if (!bo) {
        LOG_ERROR(MSGID_BUFFER_CREATION_FAILED, 0, "failed to create frame buffers  (%ux%u): (%d)", width, height,
                  strerror(errno));
        return -errno;
    }

    // TODO:: set fourcc DRM_FORMAT_XRGB8888 as a config param
    ret = drmModeAddFB2(fd, width, height, DRM_FORMAT_XRGB8888, handles, pitches, offsets, &fb_id, 0);
    if (ret) {
        LOG_ERROR(MSGID_FB_CREATION_FAILED, 0, "failed to add fb (%ux%u): %s\n", width, height, strerror(errno));
        bo_destroy(bo);
        return ret;
    }

    scanout_fbId = fb_id;
    boHandle     = bo;
    return 0;
}

DriDevice::~DriDevice()
{
    if (drmModuleFd) {
        // close(drmModuleFd);
    }
}

DRIElements::~DRIElements()
{
    g_source_remove(mTimeOutHandle);
    delete mUDev;
}

std::vector<uint32_t> DRIElements::getPlanes()
{
    DriDevice &driDevice = mDeviceList[mPrimaryDev];
    std::vector<uint32_t> planes;

    for (auto &crtc : driDevice.crtcList) {
        if (crtc.connectors.size()) {
            LOG_DEBUG("DRIElements - getPlanes - crtc id : %d, crtc_index : %d", crtc.mCrtc->crtc_id, crtc.crtc_index);
            for (auto &p : driDevice.planeList) {
                if (p.mDrmPlane->possible_crtcs & (1 << crtc.crtc_index)) {
                    LOG_DEBUG("mDrmPlane - plane id : %d, possible_crtcs : %d", p.mDrmPlane->plane_id,
                              p.mDrmPlane->possible_crtcs);
                    planes.push_back(p.mDrmPlane->plane_id);
                }
            }
        }
    }

    return planes;
}

PLANE_TYPES_T DRIElements::getPlaneType(int deviceFd, uint32_t planeId)
{
    PLANE_TYPES_T ret                   = NONE;
    std::vector<std::string> planeType  = {"Primary", "Overlay", "Cursor"};
    drmModeObjectProperties *planeProps = drmModeObjectGetProperties(deviceFd, planeId, DRM_MODE_OBJECT_PLANE);
    for (uint32_t i = 0; i < planeProps->count_props; ++i) {
        drmModePropertyRes *prop = drmModeGetProperty(deviceFd, planeProps->props[i]);
        for (int j = 0; j < prop->count_enums; ++j) {
            if (prop->enums[j].value == planeProps->prop_values[i]) {
                auto it = std::find(planeType.begin(), planeType.end(), prop->enums[j].name);
                ret     = static_cast<PLANE_TYPES_T>(std::distance(planeType.begin(), it));
                LOG_DEBUG("Type of planeID(%d) : %s, ret = %d", planeId, prop->enums[j].name, ret);
                drmModeFreeProperty(prop);
                drmModeFreeObjectProperties(planeProps);
                return ret;
            }
        }
        drmModeFreeProperty(prop);
    }
    drmModeFreeObjectProperties(planeProps);
    return ret;
}

bool DRIElements::setPlane(uint planeId, uint fbId, uint32_t crtc_x, uint32_t crtc_y, uint32_t crtc_w, uint32_t crtc_h,
                           uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h)
{
    LOG_DEBUG("Applying set plane to output {x:%u, y:%u, w:%u, h:%u} for source {x:%u, y:%u, w:%u, h:%u}, planeId %u",
              crtc_x, crtc_y, crtc_w, crtc_h, src_x, src_y, src_w, src_h, planeId);

    DriDevice &driDevice = mDeviceList[mPrimaryDev];
    for (auto &conn : driDevice.connectorList) {
        auto crtc = std::find_if(driDevice.crtcList.begin(), driDevice.crtcList.end(),
                                 [conn](DrmCrtc &c) { return c.mCrtc->crtc_id == conn.crtc_id; });

        if (crtc != driDevice.crtcList.end()) {
            if (drmModeSetPlane(driDevice.drmModuleFd, planeId, crtc->mCrtc->crtc_id, fbId, 0, crtc_x, crtc_y, crtc_w,
                                crtc_h, src_x, src_y, src_w << 16, src_h << 16)) {
                LOG_ERROR(MSGID_DRM_SET_PLANE_FAILED, 0, "%s", strerror(errno));
                return false;
            }
            break;
        }
    }
    return true;
}

uint32_t DRIElements::getSupportedNumConnector()
{
    uint32_t ret = 0;
    for (auto &c : mDeviceList[mPrimaryDev].connectorList) {
        if (c.isPlugged())
            ret++;
    }
    return ret;
}

std::vector<VAL_VIDEO_SIZE_T> DRIElements::getSupportedModes(uint8_t connIndex)
{
    DriDevice &driDevice = mDeviceList[mPrimaryDev];
    auto conn            = &driDevice.connectorList[connIndex];
    // Get unique wxh values.

    if (driDevice.connectorList.size()) {
        auto modes = conn->getSupportedModes();

        modes.erase(std::unique(modes.begin(), modes.end(),
                                [](const VAL_VIDEO_SIZE_T &lhs, const VAL_VIDEO_SIZE_T &rhs) {
                                    return (lhs.w == rhs.w && lhs.h == rhs.h);
                                }),
                    modes.end());

        return modes;
    }
    return std::vector<VAL_VIDEO_SIZE_T>();
}

bool DRIElements::setPlaneProperties(PLANE_PROPS_T propType, uint planeId, uint64_t value)
{

    DriDevice &driDevice = mDeviceList[mPrimaryDev];
    LOG_DEBUG("property type=%d, plane id = %d, value = %+" PRId64, propType, planeId, value);

    if (drmModeObjectSetProperty(driDevice.drmModuleFd, planeId, DRM_MODE_OBJECT_PLANE, propType, (uint64_t)value)) {
        LOG_ERROR(MSGID_DRM_SET_PROP_FAILED, 0, "%s", strerror(errno));
        return false;
    }
    return true;
}

uint32_t DRIElements::getPlaneBase() { return getPlanes()[0]; }

uint32_t DRIElements::getCrtcId(uint32_t planeId) { return mDeviceList[mPrimaryDev].findCrtc(planeId); }

uint32_t DRIElements::getConnId(uint32_t planeId) { return mDeviceList[mPrimaryDev].findConnector(planeId); }
