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
// Engine rendering and VGUI layout must continue to see the native
// dimensions, but only for a valid smaller fitted client.
inline Size NativeClient(Size physical, Size native)
{
    if (!Valid(native) || physical.Width<=0 || physical.Height<=0 ||
        physical.Width>native.Width || physical.Height>native.Height)
        return physical;
    return native;
}
struct Mode { int Width, Height, Bits, Refresh; };
static_assert(sizeof(Mode) == 16, "Source vmode_t layout");
inline bool Plausible(Mode m)
{
    return m.Width > 0 && m.Width <= 32768 && m.Height > 0 && m.Height <= 32768 &&
           (m.Bits == 16 || m.Bits == 24 || m.Bits == 32) && m.Refresh >= 0 && m.Refresh <= 1000;
}
inline Size Dimensions(Mode m) { return {m.Width,m.Height}; }
inline bool SameMode(Mode a, Mode b)
{
    return Dimensions(a)==Dimensions(b) && a.Bits==b.Bits && a.Refresh==b.Refresh;
}
// Only existing entries can be borrowed. Never infer capacity or touch
// the count, fallback slot zero, current mode or saved original mode.
// -2 = rejected; -1 = already present; otherwise an index within the list.
inline int BorrowIndex(const std::vector<Mode> &modes, Size requested, Size current, Size original)
{
    if (!Valid(requested) || modes.empty() || modes.size()>512 ||
        !std::all_of(modes.begin(),modes.end(),Plausible)) return -2;
    bool haveOriginal=false;
    for (const auto &m : modes)
    {
        if (Dimensions(m)==requested) return -1;
        if (Dimensions(m)==original) haveOriginal=true;
    }
    if (!haveOriginal) return -2;
    // Prefer the highest remaining index, typically the largest mode.
    for (int i=int(modes.size())-1;i>0;--i)
        if (Dimensions(modes[i])!=current && Dimensions(modes[i])!=original)
            return i;
    return -2;
}
inline bool Ready(Size requested, Size engine, Size back, Size capture)
{
    return Valid(requested) && requested == engine && requested == back && requested == capture;
}
}
