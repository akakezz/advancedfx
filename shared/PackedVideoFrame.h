#pragma once
#include <cstddef>
#include <cstring>
#include <vector>
#include <cstdint>
#include "ImageFormat.h"

namespace advancedfx {
// Rawvideo consumes tightly packed pixels, not D3D's optional row padding.
// Borrow already-packed frames; reuse staging storage only for padded input.
inline bool PackedVideoFrame(const CImageFormat &format, const unsigned char *&data,
                             std::vector<unsigned char> &scratch, size_t &bytes) {
    bytes=0;
    if (!data || format.Width<=0 || format.Height<=0) return false;
    const size_t pixel=format.GetPixelStride();
    const size_t width=static_cast<size_t>(format.Width), height=static_cast<size_t>(format.Height);
    if (!pixel || width>SIZE_MAX/pixel) return false;
    const size_t row=width*pixel;
    if (format.Pitch<row || height>SIZE_MAX/format.Pitch || format.Bytes!=height*format.Pitch ||
        height>SIZE_MAX/row || height*row>UINT32_MAX) return false;
    bytes=height*row;
    if (format.Pitch==row) return true;
    try { scratch.resize(bytes); } catch (...) { bytes=0; return false; }
    for (size_t y=0;y<height;++y) std::memcpy(scratch.data()+y*row,data+y*format.Pitch,row);
    data=scratch.data();
    return true;
}
}
