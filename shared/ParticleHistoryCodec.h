#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace advancedfx { namespace NativeParticles { namespace HistoryCodec {
using Bytes=std::vector<std::uint8_t>;
// Private in-memory format: literal 1..128 bytes, or match 4..131 bytes
// with little-endian 16-bit backwards distance. Independent frames only.
inline Bytes Encode(const Bytes &in) {
    Bytes out;
    out.reserve(in.size());
    std::array<std::uint32_t,16384> table{};
    auto hash=[&](std::size_t i) {
        std::uint32_t w;
        std::memcpy(&w,in.data()+i,4);
        return (w*2654435761u)>>18;
    };
    auto literals=[&](std::size_t first,std::size_t last) {
        while(first<last) {
            const auto count=(std::min)(std::size_t(128),last-first);
            out.push_back(static_cast<std::uint8_t>(count-1));
            out.insert(out.end(),in.begin()+first,in.begin()+first+count);
            first+=count;
        }
    };
    std::size_t i=0,start=0;
    while(i+4<=in.size()) {
        auto &slot=table[hash(i)];
        const auto previous=slot;
        slot=static_cast<std::uint32_t>(i+1);
        if(previous) {
            const auto pos=std::size_t(previous-1), distance=i-pos;
            if(distance&&distance<=65535&&std::memcmp(in.data()+i,in.data()+pos,4)==0) {
                const auto limit=(std::min)(std::size_t(131),in.size()-i);
                std::size_t count=4;
                while(count<limit&&in[i+count]==in[pos+count]) ++count;
                literals(start,i);
                out.push_back(static_cast<std::uint8_t>(128+count-4));
                out.push_back(static_cast<std::uint8_t>(distance));
                out.push_back(static_cast<std::uint8_t>(distance>>8));
                // Seed near the end of a match without hashing every byte.
                i+=count;start=i;
                if(i>=2&&i+2<=in.size()) table[hash(i-2)]=static_cast<std::uint32_t>(i-1);
                continue;
            }
        }
        ++i;
    }
    literals(start,in.size());
    return out;
}
inline bool Decode(const Bytes &in,std::size_t expected,Bytes &out) {
    out.clear();
    out.reserve(expected);
    std::size_t i=0;
    while(i<in.size()) {
        const auto token=in[i++];
        if(token<128) {
            const auto count=std::size_t(token)+1;
            if(count>in.size()-i||count>expected-out.size()) return false;
            out.insert(out.end(),in.begin()+i,in.begin()+i+count);i+=count;
        } else {
            const auto count=std::size_t(token&127)+4;
            if(in.size()-i<2||count>expected-out.size()) return false;
            const auto distance=std::size_t(in[i])+(std::size_t(in[i+1])<<8);i+=2;
            if(!distance||distance>out.size()) return false;
            for(std::size_t j=0;j<count;++j) out.push_back(out[out.size()-distance]);
        }
    }
    return out.size()==expected;
}
}}}
