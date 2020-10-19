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

#include <iostream>
#include <string.h>
typedef unsigned int u32;
typedef unsigned char u8;
typedef unsigned short u16;

// This is a place holder for extracting information from Edid.
class Edid
{
private:
    char manuf_name[4];
    unsigned int model;
    unsigned int serial;
    unsigned int year;
    unsigned int week;
    unsigned int version[2];
    unsigned int nonconformant;
    unsigned int type;
    unsigned int bpp;
    unsigned int xres;
    unsigned int yres;
    unsigned int voltage;
    unsigned int sync;
    unsigned int xsize_cm;
    unsigned int ysize_cm;
    /* used to compute timing for graphics chips. */
    unsigned char phsync;
    unsigned char pvsync;
    unsigned int x_mm;
    unsigned int y_mm;
    unsigned int pixel_clock;
    unsigned int link_clock;
    unsigned int ha;
    unsigned int hbl;
    unsigned int hso;
    unsigned int hspw;
    unsigned int hborder;
    unsigned int va;
    unsigned int vbl;
    unsigned int vso;
    unsigned int vspw;
    unsigned int vborder;
    /* 3 variables needed for coreboot framebuffer.
     * In most cases, they are the same as the ha
     * and va variables, but not always, as in the
     * case of a 1366 wide display.
     */
    u32 x_resolution;
    u32 y_resolution;
    u32 bytes_per_line;
    /* it is unlikely we need these things. */
    /* if one of these is non-zero, use that one. */
    unsigned int aspect_landscape;
    unsigned int aspect_portrait;
    const char *range_class;
    const char *syncmethod;
    const char *stereo;
    //	void set_vbe_mode_info_valid(struct edid *edid, uintptr_t fb_addr);

    unsigned char *mBlob = nullptr;
    int mEdidLen         = 0;

public:
    Edid(unsigned char *edid, int size):xsize_cm(0),
    ysize_cm(0),
    phsync(0),
    pvsync(0),
    x_mm(0),
    y_mm(0),
    pixel_clock(0),
    link_clock(0),
    ha(0),
    hbl(0),
    hso(0),
    hspw(0),
    hborder(0),
    va(0),
    vbl(0),
    vso(0),
    vspw(0),
    vborder(0),
    x_resolution(0),
    y_resolution(0),
    bytes_per_line(0),
    aspect_landscape(0),
    aspect_portrait(0),
    range_class(nullptr),
    syncmethod(nullptr)
    {
        mBlob = new unsigned char[size]();
        memcpy(mBlob, edid, size);
        mEdidLen = size;
    }

    Edid():xsize_cm(0),
    ysize_cm(0),
    phsync(0),
    pvsync(0),
    x_mm(0),
    y_mm(0),
    pixel_clock(0),
    link_clock(0),
    ha(0),
    hbl(0),
    hso(0),
    hspw(0),
    hborder(0),
    va(0),
    vbl(0),
    vso(0),
    vspw(0),
    vborder(0),
    x_resolution(0),
    y_resolution(0),
    bytes_per_line(0),
    aspect_landscape(0),
    aspect_portrait(0),
    range_class(nullptr),
    syncmethod(nullptr) {}

    Edid(const Edid &other) { Edid(other.mBlob, other.mEdidLen); };

    Edid& operator=(const Edid& other)
    {
        // check for self-assignment
        if (&other == this)
            return *this;

        if (mBlob) {
            delete[] mBlob;
            mBlob = nullptr;
        }

        unsigned char *edid = other.mBlob;
        int size = other.mEdidLen;

        mBlob = new unsigned char[size]();
        memcpy(mBlob, edid, size);
        mEdidLen = size;

        return *this;
    }

    ~Edid()
    {
        if (mBlob) {
            delete[] mBlob;
            mBlob = nullptr;
        }
    }

    friend std::ostream &operator<<(std::ostream &os, const Edid &edid)
    {
        for (int j = 0; j < edid.mEdidLen; j++) {
            os << *(edid.mBlob + j);
            if (!((j + 1) % 8)) {
                os << std::endl;
            }
        }
        return os;
    }
};
