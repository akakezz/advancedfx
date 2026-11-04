#include "stdafx.h"

#include "CinematicEditor.h"
#undef min
#undef max
#include "MaterialSystemHooks.h"
#include "RenderView.h"
#include "WrpVEngineClient.h"
#include "d3d9Hooks.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

extern WrpVEngineClient *g_VEngineClient;
namespace {
using namespace cinematic;
IDirect3DDevice9 *device = nullptr;
IDirect3DTexture9 *font = nullptr;
struct Line {
    Vec a, b;
    DWORD color;
};
struct Label {
    Vec p;
    std::string text;
    DWORD color;
};
struct Snapshot {
    double matrix[4][4];
    std::vector<Line> lines;
    std::vector<Label> labels;
};
struct Vertex {
    float x, y, z, rhw;
    DWORD color;
    float u, v;
};
void line(Snapshot &s, Vec a, Vec b, DWORD color) {
    if (s.lines.size() < 100000)
        s.lines.push_back({a, b, color});
}
void frustum(Snapshot &s, Pose p, DWORD color) {
    double w = 16 * std::tan(p.fov * Pi / 360), h = w * 9 / 16;
    Vec c[4];
    for (int i = 0; i < 4; ++i)
        c[i] = p.position + p.rotation.rotate({16, (i == 0 || i == 3) ? w : -w, i < 2 ? h : -h});
    for (int i = 0; i < 4; ++i) {
        line(s, p.position, c[i], color);
        line(s, c[i], c[(i + 1) % 4], color);
    }
}
bool project(const Snapshot &s, Vec p, const D3DVIEWPORT9 &vp, float &x, float &y) {
    double w = s.matrix[3][0] * p.x + s.matrix[3][1] * p.y + s.matrix[3][2] * p.z + s.matrix[3][3];
    if (w < 0.01)
        return false;
    double a = (s.matrix[0][0] * p.x + s.matrix[0][1] * p.y + s.matrix[0][2] * p.z + s.matrix[0][3]) / w;
    double b = (s.matrix[1][0] * p.x + s.matrix[1][1] * p.y + s.matrix[1][2] * p.z + s.matrix[1][3]) / w;
    if (!std::isfinite(a) || !std::isfinite(b))
        return false;
    x = float(vp.X + (a + 1) * 0.5 * vp.Width);
    y = float(vp.Y + (1 - b) * 0.5 * vp.Height);
    return true;
}
void createFont() {
    if (font || !device)
        return;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc)
        return;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 128;
    info.bmiHeader.biHeight = -128;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HFONT face = CreateFontW(-14, 8, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, FIXED_PITCH, L"Consolas");
    if (!bitmap || !face) {
        if (bitmap)
            DeleteObject(bitmap);
        if (face)
            DeleteObject(face);
        DeleteDC(dc);
        return;
    }
    auto oldBitmap = SelectObject(dc, bitmap), oldFont = SelectObject(dc, face);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkColor(dc, RGB(0, 0, 0));
    PatBlt(dc, 0, 0, 128, 128, BLACKNESS);
    for (int c = 32; c < 127; ++c) {
        char text = char(c);
        TextOutA(dc, (c % 16) * 8, (c / 16) * 16, &text, 1);
    }
    GdiFlush();
    IDirect3DTexture9 *t = nullptr;
    if (SUCCEEDED(device->CreateTexture(128, 128, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &t,
                                        nullptr))) {
        D3DLOCKED_RECT lock;
        if (SUCCEEDED(t->LockRect(0, &lock, nullptr, D3DLOCK_DISCARD))) {
            for (int y = 0; y < 128; ++y) {
                auto *row =
                    reinterpret_cast<DWORD *>(static_cast<unsigned char *>(lock.pBits) + y * lock.Pitch);
                for (int x = 0; x < 128; ++x) {
                    DWORD v = static_cast<DWORD *>(pixels)[y * 128 + x] & 255;
                    row[x] = (v << 24) | 0xFFFFFF;
                }
            }
            t->UnlockRect(0);
            font = t;
        } else
            t->Release();
    }
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBitmap);
    DeleteObject(face);
    DeleteObject(bitmap);
    DeleteDC(dc);
}
void render(const Snapshot &s) {
    if (!device)
        return;
    D3DVIEWPORT9 vp;
    if (FAILED(device->GetViewport(&vp)) || !vp.Width || !vp.Height)
        return;
    createFont();
    AfxD3D9BeginCleanState();
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    device->SetTexture(0, nullptr);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    for (const auto &l : s.lines) {
        Vec a = l.a, b = l.b;
        auto clipW = [&](Vec p) {
            return s.matrix[3][0] * p.x + s.matrix[3][1] * p.y + s.matrix[3][2] * p.z + s.matrix[3][3];
        };
        double wa = clipW(a), wb = clipW(b);
        if (wa < 0.011 && wb < 0.011)
            continue;
        if (wa < 0.011)
            a = a + (b - a) * ((0.011 - wa) / (wb - wa));
        else if (wb < 0.011)
            b = b + (a - b) * ((0.011 - wb) / (wa - wb));
        float x0, y0, x1, y1;
        if (!project(s, a, vp, x0, y0) || !project(s, b, vp, x1, y1))
            continue;
        // Clip in screen space before sending vertices to D3D (near-plane
        // projections can otherwise create enormous rasterizer coordinates).
        double dx = x1 - x0, dy = y1 - y0, lo = 0, hi = 1;
        auto clip = [&](double p, double q) {
            if (p == 0)
                return q >= 0;
            double r = q / p;
            if (p < 0)
                lo = (std::max)(lo, r);
            else
                hi = (std::min)(hi, r);
            return lo <= hi;
        };
        if (!clip(-dx, x0 - vp.X) || !clip(dx, vp.X + vp.Width - x0) || !clip(-dy, y0 - vp.Y) ||
            !clip(dy, vp.Y + vp.Height - y0))
            continue;
        Vertex v[2] = {{float(x0 + lo * dx), float(y0 + lo * dy), 0, 1, l.color, 0, 0},
                       {float(x0 + hi * dx), float(y0 + hi * dy), 0, 1, l.color, 0, 0}};
        device->DrawPrimitiveUP(D3DPT_LINELIST, 1, v, sizeof(Vertex));
    }
    if (font) {
        device->SetTexture(0, font);
        device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        for (const auto &label : s.labels) {
            float x, y;
            if (!project(s, label.p, vp, x, y) || x < vp.X || x > vp.X + vp.Width || y < vp.Y ||
                y > vp.Y + vp.Height)
                continue;
            y -= 18;
            for (unsigned char c : label.text) {
                if (c < 32 || c >= 127)
                    c = '?';
                float u = float(c % 16) / 16, v = float(c / 16) / 8;
                Vertex quad[4] = {{x, y, 0, 1, label.color, u, v},
                                  {x + 8, y, 0, 1, label.color, u + 1.0f / 16, v},
                                  {x, y + 16, 0, 1, label.color, u, v + 1.0f / 8},
                                  {x + 8, y + 16, 0, 1, label.color, u + 1.0f / 16, v + 1.0f / 8}};
                device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));
                x += 8;
            }
        }
    }
    AfxD3D9EndCleanState();
}
class DrawTask : public CMaterialSystemFunctor {
    Snapshot snapshot;

  public:
    explicit DrawTask(Snapshot s) : snapshot(std::move(s)) {}
    void operator()() override { render(snapshot); }
};
} // namespace
void MirvCinematic_BeginDevice(IDirect3DDevice9 *d) {
    MirvCinematic_EndDevice();
    device = d;
}
void MirvCinematic_EndDevice() {
    if (font) {
        font->Release();
        font = nullptr;
    }
    device = nullptr;
}
void MirvCinematic_Draw() {
    if (!g_VEngineClient)
        return;
    bool any = false;
    for (int i = 0; i < 6; ++i)
        any |= MirvCinematic_DrawEnabled(i);
    if (!any)
        return;
    Snapshot s;
    auto &m = g_VEngineClient->WorldToScreenMatrix();
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            s.matrix[r][c] = m.m[r][c];
    // Reject skybox/reflection passes by checking that this matrix projects the
    // current camera's forward ray to the center of the view.
    auto &r = g_Hook_VClient_RenderView;
    Vec eye{r.LastCameraOrigin[0], r.LastCameraOrigin[1], r.LastCameraOrigin[2]};
    Vec ray =
        eye +
        Quat::Euler(r.LastCameraAngles[0], r.LastCameraAngles[1], r.LastCameraAngles[2]).rotate({100, 0, 0});
    D3DVIEWPORT9 unit = {0, 0, 100, 100, 0, 1};
    float sx, sy;
    if (!project(s, ray, unit, sx, sy) || std::abs(sx - 50) > 2 || std::abs(sy - 50) > 2)
        return;
    const auto &state = MirvCinematic_Editor().state;
    for (std::size_t j = 0; j < state.paths.size(); ++j) {
        const auto &p = state.paths[j];
        if (p.points.empty())
            continue;
        bool selected = int(j) == state.selected;
        DWORD color = selected ? 0xFF55CFFF : 0xFF888899;
        if (MirvCinematic_DrawEnabled(0) || MirvCinematic_DrawEnabled(5)) {
            int samples = (std::min)(2048, (std::max)(1, int(p.points.size() - 1) * 32));
            Vec prev = p.atDistance(0).position;
            for (int i = 1; i <= samples; ++i) {
                Pose pose = p.atDistance(double(i) / samples);
                Vec pos = pose.position;
                if (MirvCinematic_DrawEnabled(0))
                    line(s, prev, pos, color);
                if (MirvCinematic_DrawEnabled(5) && i % (std::max)(1, samples / 8) == 0) {
                    Vec d = pos - prev;
                    double len = d.length();
                    if (len > 1e-6) {
                        d = d * (1 / len);
                        Vec side{-d.y, d.x, 0};
                        line(s, pos, pos - d * 8 + side * 4, color);
                        line(s, pos, pos - d * 8 - side * 4, color);
                    }
                }
                prev = pos;
            }
        }
        if (MirvCinematic_DrawEnabled(2))
            s.labels.push_back(
                {p.pointWorld(0).position + Vec{0, 0, 16}, std::to_string(j) + ": " + p.name, color});
        for (std::size_t i = 0; i < p.points.size(); ++i) {
            Pose pose = p.pointWorld(i);
            DWORD c = selected && int(i) == state.point ? 0xFFFFFF00
                      : i == 0                          ? 0xFF55FF88
                      : i + 1 == p.points.size()        ? 0xFFFF7766
                                                        : color;
            if (MirvCinematic_DrawEnabled(1))
                frustum(s, pose, c);
            if (MirvCinematic_DrawEnabled(2))
                s.labels.push_back({pose.position, std::to_string(i), c});
            if (MirvCinematic_DrawEnabled(3) && p.curve == Curve::Bezier) {
                for (Vec h : {p.points[i].inHandle, p.points[i].outHandle}) {
                    Vec pos = pose.position + p.anchor.rotation.rotate(h * p.anchor.scale);
                    line(s, pose.position, pos, 0xFFFF88EE);
                    line(s, pos - Vec{2, 0, 0}, pos + Vec{2, 0, 0}, 0xFFFF88EE);
                    line(s, pos - Vec{0, 0, 2}, pos + Vec{0, 0, 2}, 0xFFFF88EE);
                }
            }
        }
        if (MirvCinematic_DrawEnabled(4))
            for (int axis = 0; axis < 3; ++axis)
                if (p.constraints.locked[axis]) {
                    Vec center = p.constraints.plane;
                    Vec a = axis == 0 ? Vec{0, 64, 0} : Vec{64, 0, 0},
                        b = axis == 2 ? Vec{0, 64, 0} : Vec{0, 0, 64};
                    Vec corners[] = {center - a - b, center + a - b, center + a + b, center - a + b};
                    for (int k = 0; k < 4; ++k)
                        line(s, p.toWorld({corners[k], {}, 90}).position,
                             p.toWorld({corners[(k + 1) % 4], {}, 90}).position, 0xFFAA77FF);
                }
    }
    MaterialSystem_ExecuteOnRenderThread(new DrawTask(std::move(s)));
}
