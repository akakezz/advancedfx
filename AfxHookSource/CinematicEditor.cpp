#include "stdafx.h"

#include "CinematicEditor.h"
#undef min
#undef max
#include "MirvTime.h"
#include "RenderView.h"
#include "WrpVEngineClient.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

extern WrpVEngineClient *g_VEngineClient;
extern void MirvCampathPreview_Stop();
namespace {
using namespace cinematic;
Editor editor;
bool editing = false, preview = false, playing = false;
bool layers[6] = {true, true, true,
                  true, true, true}; // paths, cameras, labels, handles, constraints, direction
bool held[256] = {};
bool swallowed[256] = {};
int scrub = 0;
double previewTime = 0;
ULONGLONG lastMs = 0;
int lastFrame = -1;
struct CaptureMark {
    bool set = false, wall = false;
    double value = 0;
    std::uint64_t path = 0;
    int point = -1;
} mark, holdMark;

void need(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
double demoTime() {
    auto *engine = g_VEngineClient;
    auto *globals = g_Hook_VClient_RenderView.GetGlobals();
    auto *demo = engine ? engine->GetDemoInfoEx() : nullptr;
    need(engine && engine->IsPlayingDemo() && demo && globals, "A running demo is required for demo timing");
    double tick = globals->interval_per_tick_get();
    need(tick > 0 && std::isfinite(tick), "Demo tick interval unavailable");
    return (double(demo->GetDemoPlaybackTick()) + globals->interpolation_amount_get()) * tick;
}
double wallTime() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
Pose camera() {
    auto &r = g_Hook_VClient_RenderView;
    need(r.IsInstalled(), "Camera hook unavailable");
    return {{r.LastCameraOrigin[0], r.LastCameraOrigin[1], r.LastCameraOrigin[2]},
            Quat::Euler(r.LastCameraAngles[0], r.LastCameraAngles[1], r.LastCameraAngles[2]),
            r.LastCameraFov};
}
void retainCamera() {
    auto *input = g_Hook_VClient_RenderView.m_MirvInput;
    if (!input)
        return;
    Pose p = preview && editor.state.selected >= 0 && !editor.active().points.empty()
                 ? editor.active().evaluate(previewTime)
                 : camera();
    Vec a = p.rotation.euler();
    input->SetCameraControlMode(true);
    input->SetTx(float(p.position.x));
    input->SetTy(float(p.position.y));
    input->SetTz(float(p.position.z));
    input->SetRx(float(a.x));
    input->SetRy(float(a.y));
    input->SetRz(float(a.z));
    input->SetFov(float(p.fov));
}
void stopPreview() {
    if (preview)
        retainCamera();
    preview = false;
    scrub = 0;
}
void enterPreview() {
    need(!editor.active().points.empty(), "Capture at least one point first");
    MirvCampathPreview_Stop();
    playing = false;
    preview = true;
    if (auto *input = g_Hook_VClient_RenderView.m_MirvInput) {
        input->SetCameraControlMode(false);
        input->SetCameraControlMode(true);
    }
    previewTime = std::clamp(previewTime, 0.0, editor.active().duration);
    lastMs = GetTickCount64();
    MirvCinematic_ReleaseKeys();
}
void editOn() {
    MirvCampathPreview_Stop();
    editing = true;
    playing = false;
    auto *input = g_Hook_VClient_RenderView.m_MirvInput;
    if (input)
        input->SetCameraControlMode(true);
}
Path &path(State &s, bool create = false) {
    if (s.selected < 0 && create) {
        Path p;
        p.id = s.nextId++;
        p.name = "Path " + std::to_string(p.id);
        s.paths.push_back(p);
        s.selected = int(s.paths.size() - 1);
        s.point = -1;
    }
    need(s.selected >= 0 && s.selected < int(s.paths.size()), "No path selected");
    return s.paths[s.selected];
}
Point &point(State &s) {
    auto &p = path(s);
    need(s.point >= 0 && s.point < int(p.points.size()), "Select a point first");
    return p.points[s.point];
}
void capture(bool replace = false, int at = -1) {
    State n = editor.state;
    auto &p = path(n, true);
    Pose current = camera();
    if (p.points.empty())
        p.anchor.origin = current.position;
    p.capture(current, replace ? n.point : at, replace);
    n.point = replace ? n.point : at < 0 ? int(p.points.size() - 1) : at;
    editor.commit(std::move(n));
    if (preview)
        previewTime = editor.active().pointTime(editor.state.point);
}
void selectPoint(int i) {
    need(i >= 0 && i < int(editor.active().points.size()), "Invalid point index");
    editor.state.point = i;
    if (preview)
        previewTime = editor.active().pointTime(i);
}
std::filesystem::path filename(const char *utf8) {
    std::filesystem::path p = std::filesystem::u8path(utf8);
    need(!p.empty(), "Filename required");
    if (!p.has_extension())
        p += ".xml";
    return p;
}
void saveFile(const std::filesystem::path &file, const std::string &text, bool overwrite) {
    // Never truncate an existing file. Write a unique sibling, then atomically
    // rename it; without overwrite, MoveFileEx fails if a destination exists.
    auto tmp = file;
    tmp += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
    HANDLE handle =
        CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    need(handle != INVALID_HANDLE_VALUE, "Cannot create temporary save file");
    DWORD wrote = 0;
    bool ok = WriteFile(handle, text.data(), DWORD(text.size()), &wrote, nullptr) && wrote == text.size();
    if (ok)
        ok = FlushFileBuffers(handle) != FALSE;
    CloseHandle(handle);
    if (ok)
        ok = MoveFileExW(tmp.c_str(), file.c_str(),
                         MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0)) != FALSE;
    if (!ok) {
        DeleteFileW(tmp.c_str());
        throw std::runtime_error("Save failed: file exists or is not writable (use overwrite explicitly)");
    }
}
std::string readFile(const std::filesystem::path &file) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    need(bool(in), "Cannot open XML file");
    auto size = in.tellg();
    need(size >= 0 && size <= 16 * 1024 * 1024, "XML file exceeds 16 MiB");
    std::string s(std::size_t(size), '\0');
    in.seekg(0);
    if (size > 0)
        in.read(&s[0], size);
    need(bool(in), "Cannot read complete XML file");
    return s;
}
struct Args {
    IWrpCommandArgs *a;
    int offset;
    int count() const { return a->ArgC() - offset; }
    std::string at(int i) const {
        need(i >= 0 && i < count(), "Missing argument; see mirv_campath cinematic help");
        return a->ArgV(offset + i);
    }
    bool is(int i, const char *s) const { return i < count() && 0 == _stricmp(a->ArgV(offset + i), s); }
    double num(int i) const {
        auto s = at(i);
        std::istringstream in(s);
        in.imbue(std::locale::classic());
        double v;
        in >> std::noskipws >> v;
        need(bool(in) && in.peek() == std::char_traits<char>::eof() && std::isfinite(v),
             "Expected a finite number");
        return v;
    }
    int index(int i) const {
        double v = num(i);
        need(v >= 0 && v <= 100000 && v == std::floor(v), "Expected a nonnegative integer");
        return int(v);
    }
    bool flag(int i) const {
        double v = num(i);
        need(v == 0 || v == 1, "Expected 0 or 1");
        return v != 0;
    }
    Vec vec(int i) const { return {num(i), num(i + 1), num(i + 2)}; }
    void size(int n) const { need(count() == n, "Wrong argument count; see mirv_campath cinematic help"); }
};
void help() {
    Tier0_Msg(
        "Cinematic campath (Source 1). Enable: mirv_campath edit on; disable: mirv_campath edit off.\n"
        "While editing, use mirv_campath <command>; outside, use mirv_campath cinematic <command>.\n"
        "All indices are 0-based; times are seconds; track time/progress is 0..1; angles are pitch yaw "
        "roll.\n"
        "add | insert <index> | replace | delete | select <point> | point <x y z pitch yaw roll fov>\n"
        "new [name] | paths | use <index|name> | rename <name> | duplicate | remove confirm\n"
        "undo | redo | preview [enter|exit|toggle|seek|seconds] | play | stop\n"
        "curve linear|smooth|bezier | tension <0..1> | handle in|out <x y z|capture>\n"
        "orbit <centerX centerY centerZ radius xy|xz|yz sweepDegrees [startDegrees [points]]>\n"
        "anchor <x y z pitch yaw roll> | anchor capture | translate <x y z> | rotate <pitch yaw roll> | "
        "scale <factor>\n"
        "grid <units> | lock x|y|z <localValue|off> | constrain | corner stop|round\n"
        "duration <seconds> | start <demoSeconds|capture> | end capture | schedule <0|1> | finish "
        "release|hold\n"
        "segment <pointIndex> <seconds|auto> | hold <seconds> | stopPoint <0|1> | ease "
        "linear|in|out|smooth|smoother|custom\n"
        "mark begin|end [demo|wall] | holdMark begin|end [demo|wall]\n"
        "speed set <normalizedTime> <progress> | speed delete <time> | speed clear (custom segment easing)\n"
        "motion set <normalizedTime> <pathProgress> | motion clear (whole path; reversals allowed; replaces "
        "holds/segments)\n"
        "rotation set <normalizedTime> [pitch yaw roll] | rotation clear | rotation interp "
        "linear|smooth|step\n"
        "fov set <normalizedTime> <degrees> | fov clear | fov interp linear|smooth|step\n"
        "aim <worldX worldY worldZ|off> | transition <seconds> | overlaps\n"
        "save <file> [overwrite] | saveAll <file> [overwrite] | load <file> [append|replace [discard]]\n"
        "exportLegacy <file> [overwrite] (bakes to 60 Hz; original files can be imported with load)\n"
        "draw all|paths|cameras|labels|handles|constraints|direction <0|1>\n"
        "Keys: Insert capture, Shift+Insert replace, Delete remove point, Tab preview/freecam, Ctrl+Z/Y "
        "undo/redo.\n"
        "Preview only: Up/Down previous/next point, Home/End first/last, Left/Right scrub.\n"
        "Classic commands remain available after edit off. Full guide: doc/CINEMATIC_CAMERA.md\n");
}
Interp interpolation(const Args &a, int i) {
    if (a.is(i, "linear"))
        return Interp::Linear;
    if (a.is(i, "smooth"))
        return Interp::Smooth;
    if (a.is(i, "step"))
        return Interp::Step;
    throw std::runtime_error("Interpolation must be linear, smooth or step");
}
template <class Key> void put(std::vector<Key> &keys, Key k) {
    auto it =
        std::lower_bound(keys.begin(), keys.end(), k.time, [](const Key &v, double t) { return v.time < t; });
    if (it != keys.end() && it->time == k.time)
        *it = k;
    else
        keys.insert(it, k);
}
void run(const Args &a) {
    if (!a.count() || a.is(0, "help")) {
        help();
        return;
    }
    if (a.is(0, "paths")) {
        a.size(1);
        for (std::size_t i = 0; i < editor.state.paths.size(); ++i) {
            auto &p = editor.state.paths[i];
            Tier0_Msg("%s%u: %s - %u points, %.3fs, length %.2f, start %.3f, scheduled %i\n",
                      int(i) == editor.state.selected ? "* " : "  ", unsigned(i), p.name.c_str(),
                      unsigned(p.points.size()), p.duration, p.length(), p.start, p.scheduled);
        }
        Tier0_Msg("Unsaved changes: %s\n", editor.dirty() ? "yes" : "no");
        return;
    }
    if (a.is(0, "new")) {
        need(a.count() <= 2, "new [name]");
        stopPreview();
        editor.create(a.count() == 2 ? a.at(1) : "");
        return;
    }
    if (a.is(0, "use")) {
        a.size(2);
        stopPreview();
        std::string which = a.at(1);
        int chosen = -1;
        for (std::size_t i = 0; i < editor.state.paths.size(); ++i)
            if (editor.state.paths[i].name == which) {
                need(chosen < 0, "Ambiguous name; use a path index");
                chosen = int(i);
            }
        if (chosen < 0) {
            chosen = a.index(1);
            need(chosen < int(editor.state.paths.size()), "Invalid path index");
        }
        editor.state.selected = chosen;
        editor.state.point = -1;
        previewTime = 0;
        return;
    }
    if (a.is(0, "add")) {
        a.size(1);
        capture();
        return;
    }
    if (a.is(0, "insert")) {
        a.size(2);
        capture(false, a.index(1));
        return;
    }
    if (a.is(0, "replace")) {
        a.size(1);
        capture(true);
        return;
    }
    if (a.is(0, "select")) {
        a.size(2);
        selectPoint(a.index(1));
        return;
    }
    if (a.is(0, "duplicate")) {
        a.size(1);
        stopPreview();
        editor.duplicate();
        return;
    }
    if (a.is(0, "remove")) {
        a.size(2);
        need(a.is(1, "confirm"), "Use remove confirm; undo can restore it");
        stopPreview();
        editor.remove();
        return;
    }
    if (a.is(0, "undo") || a.is(0, "redo")) {
        a.size(1);
        bool ok = a.is(0, "undo") ? editor.undo() : editor.redo();
        if (!ok)
            Tier0_Msg("Nothing to %s.\n", a.at(0).c_str());
        MirvCinematic_ReleaseKeys();
        return;
    }
    if (a.is(0, "preview")) {
        need(a.count() <= 2, "preview [enter|exit|toggle|seek|seconds]");
        if (a.is(1, "exit")) {
            stopPreview();
            return;
        }
        if (a.is(1, "toggle") && preview) {
            stopPreview();
            return;
        }
        if (a.is(1, "seek")) {
            auto &p = editor.active();
            double t = p.start + previewTime;
            auto *globals = g_Hook_VClient_RenderView.GetGlobals();
            demoTime();
            double tick = t / globals->interval_per_tick_get();
            need(tick >= 0 && tick < (std::numeric_limits<int>::max)(), "Seek outside demo tick range");
            std::string cmd = "demo_gototick " + std::to_string(int(std::round(tick)));
            g_VEngineClient->ExecuteClientCmd(cmd.c_str());
            return;
        }
        double t = previewTime;
        if (a.count() == 2 && !a.is(1, "enter") && !a.is(1, "toggle")) {
            t = a.num(1);
            need(t >= 0 && t <= editor.active().duration, "Preview time outside movement");
        }
        enterPreview();
        previewTime = t;
        return;
    }
    if (a.is(0, "play")) {
        a.size(1);
        need(!editor.state.paths.empty(), "No paths to play");
        demoTime();
        stopPreview();
        MirvCampathPreview_Stop();
        playing = true;
        if (auto *i = g_Hook_VClient_RenderView.m_MirvInput)
            i->SetCameraControlMode(false);
        return;
    }
    if (a.is(0, "stop")) {
        a.size(1);
        stopPreview();
        playing = false;
        return;
    }
    if (a.is(0, "overlaps")) {
        a.size(1);
        auto pairs = editor.overlaps();
        for (auto &s : pairs)
            Tier0_Msg("Overlap: %s\n", s.c_str());
        Tier0_Msg("%u overlap(s); latest start wins, larger stable ID breaks ties.\n",
                  unsigned(pairs.size()));
        return;
    }
    if (a.is(0, "draw")) {
        a.size(3);
        bool value = a.flag(2);
        const char *names[] = {"paths", "cameras", "labels", "handles", "constraints", "direction"};
        if (a.is(1, "all")) {
            for (bool &v : layers)
                v = value;
            return;
        }
        for (int i = 0; i < 6; ++i)
            if (a.is(1, names[i])) {
                layers[i] = value;
                return;
            }
        throw std::runtime_error("Unknown draw layer");
    }
    if (a.is(0, "save") || a.is(0, "saveAll") || a.is(0, "exportLegacy")) {
        need(a.count() == 2 || (a.count() == 3 && a.is(2, "overwrite")),
             "save/saveAll/exportLegacy <file> [overwrite]");
        bool all = a.is(0, "saveAll");
        std::string text = a.is(0, "exportLegacy") ? editor.legacyXml() : editor.xml(all);
        saveFile(filename(a.at(1).c_str()), text, a.count() == 3);
        if (all)
            editor.markSaved();
        Tier0_Msg("Saved XML.\n");
        return;
    }
    if (a.is(0, "load")) {
        need(a.count() >= 2 && a.count() <= 4, "load <file> [append|replace [discard]]");
        bool replace = a.is(2, "replace");
        need(a.count() < 3 || replace || a.is(2, "append"), "Choose append or replace");
        need(a.count() < 4 || (replace && a.is(3, "discard")), "Only replace accepts discard");
        need(!replace || !editor.dirty() || a.is(3, "discard"),
             "Unsaved changes: saveAll first, append, or use load <file> replace discard");
        std::string xml = readFile(filename(a.at(1).c_str()));
        State verified = Deserialize(xml);
        (void)verified;
        stopPreview();
        editor.loadXml(xml, replace);
        if (replace)
            editor.markSaved();
        Tier0_Msg("Loaded XML (%s); undo is available.\n", replace ? "replace" : "append");
        return;
    }
    State n = editor.state;
    auto &p = path(n);
    if (a.is(0, "rename")) {
        a.size(2);
        p.name = a.at(1);
    } else if (a.is(0, "delete")) {
        a.size(1);
        point(n);
        p.points.erase(p.points.begin() + n.point);
        n.point = p.points.empty() ? -1 : (std::min)(n.point, int(p.points.size() - 1));
    } else if (a.is(0, "point")) {
        a.size(8);
        Pose v{a.vec(1), Quat::Euler(a.num(4), a.num(5), a.num(6)), a.num(7)};
        p.capture(v, n.point, true);
    } else if (a.is(0, "curve")) {
        a.size(2);
        Curve c;
        if (a.is(1, "linear"))
            c = Curve::Linear;
        else if (a.is(1, "smooth"))
            c = Curve::Smooth;
        else if (a.is(1, "bezier"))
            c = Curve::Bezier;
        else
            throw std::runtime_error("Curve must be linear, smooth or bezier");
        if (p.curve == Curve::Circle)
            for (std::size_t i = 0; i < p.points.size(); ++i)
                p.points[i].pose.position = p.position(double(i));
        p.curve = c;
    } else if (a.is(0, "tension")) {
        a.size(2);
        p.tension = a.num(1);
    } else if (a.is(0, "handle")) {
        need(a.count() == 5 || (a.count() == 3 && a.is(2, "capture")),
             "handle in|out <localX localY localZ|capture>");
        need(a.is(1, "in") || a.is(1, "out"), "Choose in or out handle");
        auto &v = point(n);
        Vec h = a.count() == 3 ? p.toLocal(camera()).position - v.pose.position : a.vec(2);
        h = p.constraints.apply(v.pose.position + h) - v.pose.position;
        if (a.is(1, "in"))
            v.inHandle = h;
        else
            v.outHandle = h;
        p.curve = Curve::Bezier;
    } else if (a.is(0, "orbit")) {
        need(a.count() >= 7 && a.count() <= 9,
             "orbit <worldCenterX Y Z radius xy|xz|yz sweep [start [points]]>");
        p.circleCenter = p.toLocal({a.vec(1), {}, 90}).position;
        p.circleRadius = a.num(4) / p.anchor.scale;
        p.circlePlane = a.is(5, "xy") ? 0 : a.is(5, "xz") ? 1 : a.is(5, "yz") ? 2 : -1;
        p.circleSweep = a.num(6);
        p.circleStart = a.count() > 7 ? a.num(7) : 0;
        int count = a.count() > 8 ? a.index(8) : 9;
        need(count >= 2 && count <= 4096, "Orbit needs 2..4096 points");
        p.points.assign(std::size_t(count), Point{});
        p.curve = Curve::Circle;
        for (int i = 0; i < count; ++i) {
            p.points[i].pose = p.toLocal(camera());
            p.points[i].pose.position = p.position(i);
        }
        n.point = 0;
    } else if (a.is(0, "anchor")) {
        if (a.count() == 2 && a.is(1, "capture")) {
            Pose c = camera();
            p.anchor.origin = c.position;
            p.anchor.rotation = c.rotation;
        } else {
            a.size(7);
            p.anchor.origin = a.vec(1);
            p.anchor.rotation = Quat::Euler(a.num(4), a.num(5), a.num(6));
        }
    } else if (a.is(0, "translate")) {
        a.size(4);
        p.anchor.origin = p.anchor.origin + a.vec(1);
    } else if (a.is(0, "rotate")) {
        a.size(4);
        p.anchor.rotation = Quat::Euler(a.num(1), a.num(2), a.num(3)) * p.anchor.rotation;
    } else if (a.is(0, "scale")) {
        a.size(2);
        p.anchor.scale = a.num(1);
    } else if (a.is(0, "grid")) {
        a.size(2);
        p.constraints.grid = a.num(1);
    } else if (a.is(0, "lock")) {
        a.size(3);
        int axis = a.is(1, "x") ? 0 : a.is(1, "y") ? 1 : a.is(1, "z") ? 2 : -1;
        need(axis >= 0, "Axis must be x, y or z");
        p.constraints.locked[axis] = !a.is(2, "off");
        if (!a.is(2, "off")) {
            double value = a.num(2);
            if (axis == 0)
                p.constraints.plane.x = value;
            else if (axis == 1)
                p.constraints.plane.y = value;
            else
                p.constraints.plane.z = value;
        }
    } else if (a.is(0, "constrain")) {
        a.size(1);
        need(p.curve != Curve::Circle, "Convert circle to another curve before constraining");
        for (auto &pt : p.points) {
            pt.pose.position = p.constraints.apply(pt.pose.position);
            pt.inHandle = p.constraints.apply(pt.pose.position + pt.inHandle) - pt.pose.position;
            pt.outHandle = p.constraints.apply(pt.pose.position + pt.outHandle) - pt.pose.position;
        }
    } else if (a.is(0, "corner")) {
        a.size(2);
        need(a.is(1, "stop") || a.is(1, "round"), "corner stop|round");
        point(n).stop = a.is(1, "stop");
        p.curve = a.is(1, "round") ? Curve::Smooth : Curve::Linear;
    } else if (a.is(0, "duration")) {
        a.size(2);
        p.duration = a.num(1);
    } else if (a.is(0, "start")) {
        a.size(2);
        p.start = a.is(1, "capture") ? demoTime() : a.num(1);
        p.scheduled = true;
    } else if (a.is(0, "end")) {
        a.size(2);
        need(a.is(1, "capture"), "end capture");
        p.duration = demoTime() - p.start;
    } else if (a.is(0, "schedule")) {
        a.size(2);
        p.scheduled = a.flag(1);
    } else if (a.is(0, "finish")) {
        a.size(2);
        need(a.is(1, "release") || a.is(1, "hold"), "finish release|hold");
        p.holdEnd = a.is(1, "hold");
    } else if (a.is(0, "transition")) {
        a.size(2);
        p.transition = a.num(1);
    } else if (a.is(0, "segment")) {
        a.size(3);
        int i = a.index(1);
        need(i + 1 < int(p.points.size()), "Select an outgoing segment index");
        p.points[i].seconds = a.is(2, "auto") ? 0 : a.num(2);
        need(a.is(2, "auto") || p.points[i].seconds > 0, "Segment duration must be positive or auto");
    } else if (a.is(0, "hold")) {
        a.size(2);
        point(n).hold = a.num(1);
    } else if (a.is(0, "stopPoint")) {
        a.size(2);
        point(n).stop = a.flag(1);
    } else if (a.is(0, "ease")) {
        a.size(2);
        const char *names[] = {"linear", "in", "out", "smooth", "smoother", "custom"};
        int chosen = -1;
        for (int i = 0; i < 6; ++i)
            if (a.is(1, names[i]))
                chosen = i;
        need(chosen >= 0, "Unknown easing");
        p.ease = Ease(chosen);
    } else if (a.is(0, "mark") || a.is(0, "holdMark")) {
        need(a.count() == 2 || a.count() == 3, "mark/holdMark begin|end [demo|wall]");
        bool isHold = a.is(0, "holdMark");
        auto &m = isHold ? holdMark : mark;
        need(a.is(1, "begin") || a.is(1, "end"), "Choose begin or end");
        bool wall = a.count() == 3 ? a.is(2, "wall") : a.is(1, "end") && m.set ? m.wall : false;
        need(a.count() < 3 || wall || a.is(2, "demo"), "Clock must be demo or wall");
        double now = wall ? wallTime() : demoTime();
        if (a.is(1, "begin")) {
            if (isHold)
                point(n);
            m = {true, wall, now, p.id, n.point};
            Tier0_Msg("Timing mark captured (%s).\n", wall ? "wall" : "demo");
            return;
        }
        need(m.set && m.wall == wall && m.path == p.id, "Capture a matching begin mark on this path first");
        double dt = now - m.value;
        need(dt >= 0, "End mark precedes begin mark");
        if (isHold) {
            need(n.point == m.point, "Select the point where hold capture began");
            point(n).hold = dt;
        } else {
            need(dt > 0, "Duration must be positive");
            p.duration = dt;
            if (!wall) {
                p.start = m.value;
                p.scheduled = true;
            }
        }
        editor.commit(std::move(n));
        m.set = false;
        return;
    } else if (a.is(0, "speed") || a.is(0, "motion") || a.is(0, "rotation") || a.is(0, "fov")) {
        if (a.is(1, "interp")) {
            a.size(3);
            need(a.is(0, "rotation") || a.is(0, "fov"), "Only rotation/fov accept interp");
            if (a.is(0, "rotation"))
                p.rotationInterp = interpolation(a, 2);
            else
                p.fovInterp = interpolation(a, 2);
        } else if (a.is(1, "clear")) {
            a.size(2);
            if (a.is(0, "speed"))
                p.speed = {{0, 0}, {1, 1}};
            else if (a.is(0, "motion"))
                p.progress.clear();
            else if (a.is(0, "rotation"))
                p.rotations.clear();
            else
                p.fovs.clear();
        } else if (a.is(1, "delete")) {
            a.size(3);
            double t = a.num(2);
            auto erase = [&](auto &keys) {
                auto it = std::find_if(keys.begin(), keys.end(), [&](const auto &k) { return k.time == t; });
                need(it != keys.end(), "No key at this normalized time");
                keys.erase(it);
            };
            if (a.is(0, "rotation"))
                erase(p.rotations);
            else if (a.is(0, "fov"))
                erase(p.fovs);
            else if (a.is(0, "speed"))
                erase(p.speed);
            else
                erase(p.progress);
        } else {
            need(a.is(1, "set"), "Use set, delete, clear or interp");
            double t = a.num(2);
            if (a.is(0, "rotation")) {
                need(a.count() == 3 || a.count() == 6, "rotation set <time> [pitch yaw roll]");
                Quat q = a.count() == 3
                             ? p.toLocal(camera()).rotation
                             : p.anchor.rotation.inverse() * Quat::Euler(a.num(3), a.num(4), a.num(5));
                put(p.rotations, RotationKey{t, q});
            } else {
                a.size(4);
                double value = a.num(3);
                auto &track = a.is(0, "speed") ? p.speed : a.is(0, "motion") ? p.progress : p.fovs;
                if (a.is(0, "motion") && track.empty())
                    track = {{0, 0}, {1, 1}};
                put(track, ScalarKey{t, value});
            }
        }
    } else if (a.is(0, "aim")) {
        if (a.count() == 2 && a.is(1, "off"))
            p.fixedAim = false;
        else {
            a.size(4);
            p.fixedAim = true;
            p.aimTarget = p.toLocal({a.vec(1), {}, 90}).position;
        }
    } else
        throw std::runtime_error("Unknown cinematic command; see mirv_campath cinematic help");
    editor.commit(std::move(n));
    if (preview && editor.state.selected >= 0)
        previewTime = std::clamp(previewTime, 0.0, editor.active().duration);
}
} // namespace
cinematic::Editor &MirvCinematic_Editor() { return editor; }
bool MirvCinematic_DrawEnabled(int layer) {
    return layer >= 0 && layer < 6 && layers[layer] && (editing || preview);
}
bool MirvCinematic_Command(IWrpCommandArgs *args) {
    bool explicitCommand = args->ArgC() > 1 && 0 == _stricmp(args->ArgV(1), "cinematic");
    // Only the two new mode toggles belong to this editor. In particular,
    // legacy `mirv_campath edit start/position/angles/...` must keep reaching
    // the classic command handler when cinematic editing is off.
    bool editCommand = args->ArgC() == 3 && 0 == _stricmp(args->ArgV(1), "edit") &&
                       (0 == _stricmp(args->ArgV(2), "on") || 0 == _stricmp(args->ArgV(2), "off"));
    if (!explicitCommand && !editCommand && !editing)
        return false;
    try {
        if (editCommand) {
            need(args->ArgC() == 3, "mirv_campath edit on|off");
            if (0 == _stricmp(args->ArgV(2), "on"))
                editOn();
            else {
                need(0 == _stricmp(args->ArgV(2), "off"), "mirv_campath edit on|off");
                stopPreview();
                editing = false;
                playing = false;
                MirvCinematic_ReleaseKeys();
            }
            Tier0_Msg("Cinematic editor %s.\n", editing ? "on" : "off");
        } else
            run({args, explicitCommand ? 2 : 1});
    } catch (const std::exception &e) {
        Tier0_Warning("Cinematic camera: %s\n", e.what());
    }
    return true;
}
bool MirvCinematic_OwnsCamera() { return preview || playing; }
bool MirvCinematic_IsEditing() { return editing; }
void MirvCinematic_Update() {
    if (preview && (editor.state.selected < 0 || editor.active().points.empty() ||
                    !g_Hook_VClient_RenderView.m_MirvInput ||
                    !g_Hook_VClient_RenderView.m_MirvInput->GetCameraControlMode())) {
        preview = false;
        scrub = 0;
    }
    if (preview) {
        int frame = g_MirvTime.GetFrameCount();
        ULONGLONG now = GetTickCount64();
        if (g_Hook_VClient_RenderView.IsMenuActive()) {
            MirvCinematic_ReleaseKeys();
            lastMs = now;
            return;
        }
        if (frame != lastFrame) {
            lastFrame = frame;
            double dt = lastMs ? double((std::min)(now - lastMs, ULONGLONG(100))) / 1000 : 0;
            lastMs = now;
            previewTime = std::clamp(previewTime + scrub * dt, 0.0, editor.active().duration);
        }
    }
}
bool MirvCinematic_Override(float &x, float &y, float &z, float &pitch, float &yaw, float &roll, float &fov) {
    try {
        Pose p;
        if (preview)
            p = editor.active().evaluate(previewTime);
        else if (!playing || !editor.evaluate(demoTime(), p))
            return false;
        Vec a = p.rotation.euler();
        x = float(p.position.x);
        y = float(p.position.y);
        z = float(p.position.z);
        pitch = float(a.x);
        yaw = float(a.y);
        roll = float(a.z);
        fov = float(p.fov);
        return true;
    } catch (const std::exception &) {
        return false;
    }
}
void MirvCinematic_ReleaseKeys() {
    for (bool &v : held)
        v = false;
    scrub = 0;
    lastMs = GetTickCount64();
}
bool MirvCinematic_KeyEvent(UINT message, WPARAM key, LPARAM flags) {
    if (message == WM_KEYUP || message == WM_SYSKEYUP) {
        if (key < 256 && swallowed[key]) {
            swallowed[key] = false;
            held[key] = false;
            scrub = (held[VK_RIGHT] ? 1 : 0) - (held[VK_LEFT] ? 1 : 0);
            return true;
        }
    }
    if (!editing)
        return false;
    if (message == WM_CHAR || message == WM_SYSCHAR) {
        return !g_Hook_VClient_RenderView.IsMenuActive() && (key == 9 || key == 26 || key == 25);
    }
    bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN,
         up = message == WM_KEYUP || message == WM_SYSKEYUP;
    if (!down && !up)
        return false;
    if (key >= 256)
        return false;
    if (g_Hook_VClient_RenderView.IsMenuActive())
        return false;
    bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool reserved = key == VK_INSERT || key == VK_DELETE || key == VK_TAB ||
                    (control && (key == 'Z' || key == 'Y')) ||
                    (preview && (key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN ||
                                 key == VK_HOME || key == VK_END));
    if (!reserved)
        return false;
    bool repeat = held[key] || (flags & (1LL << 30));
    held[key] = down;
    swallowed[key] = down;
    if (up || repeat)
        return true;
    try {
        if (key == VK_INSERT)
            capture((GetKeyState(VK_SHIFT) & 0x8000) != 0);
        else if (key == VK_DELETE) {
            State n = editor.state;
            auto &p = path(n);
            point(n);
            p.points.erase(p.points.begin() + n.point);
            n.point = p.points.empty() ? -1 : (std::min)(n.point, int(p.points.size() - 1));
            editor.commit(std::move(n));
        } else if (key == VK_TAB) {
            if (preview)
                stopPreview();
            else
                enterPreview();
        } else if (control && key == 'Z')
            editor.undo();
        else if (control && key == 'Y')
            editor.redo();
        else if (key == VK_LEFT || key == VK_RIGHT)
            scrub = (held[VK_RIGHT] ? 1 : 0) - (held[VK_LEFT] ? 1 : 0);
        else {
            int i = editor.state.point;
            if (key == VK_HOME)
                i = 0;
            else if (key == VK_END)
                i = int(editor.active().points.size() - 1);
            else
                i += key == VK_UP ? -1 : 1;
            selectPoint((std::max)(0, (std::min)(i, int(editor.active().points.size() - 1))));
        }
    } catch (const std::exception &e) {
        Tier0_Warning("Cinematic camera: %s\n", e.what());
    }
    return true;
}
