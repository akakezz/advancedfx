#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
namespace advancedfx::RenderResolution
{
struct Size { int Width = 0, Height = 0; };
inline bool operator==(Size a, Size b) { return a.Width == b.Width && a.Height == b.Height; }
inline bool operator!=(Size a, Size b) { return !(a == b); }
inline bool Valid(Size s) { return s.Width >= 640 && s.Height >= 480 && s.Width <= 16384 && s.Height <= 16384; }
inline Size Fit(Size source, Size available)
{
    if (source.Width <= 0 || source.Height <= 0 || available.Width <= 0 || available.Height <= 0) return {};
    if (source.Width <= available.Width && source.Height <= available.Height) return source;
    if (std::int64_t(available.Width)*source.Height <= std::int64_t(available.Height)*source.Width)
        return {available.Width, (std::max)(1, int(std::int64_t(source.Height)*available.Width/source.Width))};
    return {(std::max)(1, int(std::int64_t(source.Width)*available.Height/source.Height)), available.Height};
}
struct Mode { int Width, Height, Bits, Refresh; };
static_assert(sizeof(Mode) == 16, "Source vmode_t layout");
inline bool Plausible(Mode m)
{
    return m.Width > 0 && m.Width <= 32768 && m.Height > 0 && m.Height <= 32768 &&
           (m.Bits == 16 || m.Bits == 24 || m.Bits == 32) && m.Refresh >= 0 && m.Refresh <= 1000;
}
inline bool Before(Mode a, Mode b)
{
    return a.Width < b.Width || (a.Width == b.Width && a.Height < b.Height);
}
// Pure planner: preserve all existing entries; reject an unexpected list layout.
inline bool Plan(std::vector<Mode> &modes, Size requested, bool &inserted)
{
    inserted = false;
    if (!Valid(requested) || modes.empty() || modes.size() > 512 ||
        !std::all_of(modes.begin(), modes.end(), Plausible)) return false;
    for (const auto &m : modes)
        if (m.Width == requested.Width && m.Height == requested.Height) return true;
    if (modes.size() >= 512) return false;
    Mode mode{requested.Width, requested.Height, modes.front().Bits, modes.front().Refresh};
    if (std::is_sorted(modes.begin(), modes.end(), Before))
        modes.insert(std::upper_bound(modes.begin(), modes.end(), mode, Before), mode);
    else
        modes.push_back(mode); // retain native ordering for unsorted mode lists
    inserted = true;
    return true;
}
inline bool Ready(Size requested, Size engine, Size back, Size capture)
{
    return Valid(requested) && requested == engine && requested == back && requested == capture;
}
}
