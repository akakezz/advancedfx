#include "CinematicPath.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace cinematic {
namespace {
double clamp(double x) { return std::clamp(x, 0.0, 1.0); }
double smooth(double t) { return t * t * (3 - 2 * t); }
bool finite(double v) { return std::isfinite(v); }
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
double scalar(const std::vector<ScalarKey> &keys, double t, Interp mode = Interp::Linear) {
    if (t <= keys.front().time)
        return keys.front().value;
    auto b = std::upper_bound(keys.begin(), keys.end(), t,
                              [](double v, const ScalarKey &k) { return v < k.time; });
    if (b == keys.end())
        return keys.back().value;
    auto a = b - 1;
    double u = (t - a->time) / (b->time - a->time);
    if (mode == Interp::Step)
        u = 0;
    else if (mode == Interp::Smooth)
        u = smooth(u);
    return a->value + (b->value - a->value) * u;
}
void checkVec(Vec p) {
    require(finite(p.x) && finite(p.y) && finite(p.z) && std::abs(p.x) <= 1e9 && std::abs(p.y) <= 1e9 &&
                std::abs(p.z) <= 1e9,
            "Coordinates must be finite and within +/-1e9 units");
}
void checkQuat(Quat &q) {
    require(finite(q.w) && finite(q.x) && finite(q.y) && finite(q.z), "Rotation must be finite");
    double n = q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z;
    require(finite(n) && n > 1e-20, "Invalid quaternion");
    q = q.unit();
}
void checkPose(Pose &p) {
    checkVec(p.position);
    checkQuat(p.rotation);
    require(finite(p.fov) && p.fov > 1 && p.fov < 179, "FOV must be between 1 and 179 degrees");
}
template <class Key> void checkKeys(const std::vector<Key> &keys) {
    double last = -1;
    for (const auto &k : keys) {
        require(finite(k.time) && k.time >= 0 && k.time <= 1 && k.time > last,
                "Track times must be strictly increasing within 0..1");
        last = k.time;
    }
}
} // namespace
double Vec::length() const { return std::sqrt(x * x + y * y + z * z); }
Quat Quat::unit() const {
    double n = std::sqrt(w * w + x * x + y * y + z * z);
    return n > 1e-20 ? Quat{w / n, x / n, y / n, z / n} : Quat{};
}
Quat Quat::operator*(Quat b) const {
    return {w * b.w - x * b.x - y * b.y - z * b.z, w * b.x + x * b.w + y * b.z - z * b.y,
            w * b.y - x * b.z + y * b.w + z * b.x, w * b.z + x * b.y - y * b.x + z * b.w};
}
Vec Quat::rotate(Vec v) const {
    Quat r = (*this) * Quat{0, v.x, v.y, v.z} * inverse();
    return {r.x, r.y, r.z};
}
Quat Quat::Euler(double pitch, double yaw, double roll) {
    double p = pitch * Pi / 360, y = yaw * Pi / 360, r = roll * Pi / 360;
    return (Quat{std::cos(y), 0, 0, std::sin(y)} * Quat{std::cos(p), 0, std::sin(p), 0} *
            Quat{std::cos(r), std::sin(r), 0, 0})
        .unit();
}
Vec Quat::euler() const {
    Quat q = unit();
    double pitch = std::asin(std::clamp(2 * (q.w * q.y - q.z * q.x), -1.0, 1.0));
    double yaw, roll;
    if (std::abs(std::cos(pitch)) < 1e-7) {
        yaw = std::atan2(2 * (q.w * q.z - q.x * q.y), 1 - 2 * (q.x * q.x + q.z * q.z));
        roll = 0;
    } else {
        yaw = std::atan2(2 * (q.w * q.z + q.x * q.y), 1 - 2 * (q.y * q.y + q.z * q.z));
        roll = std::atan2(2 * (q.w * q.x + q.y * q.z), 1 - 2 * (q.x * q.x + q.y * q.y));
    }
    return {pitch * 180 / Pi, yaw * 180 / Pi, roll * 180 / Pi};
}
Quat Quat::Slerp(Quat a, Quat b, double t) {
    a = a.unit();
    b = b.unit();
    double dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    if (dot < 0) {
        b = {-b.w, -b.x, -b.y, -b.z};
        dot = -dot;
    }
    double x = 1 - t, y = t;
    if (dot < 0.9995) {
        double angle = std::acos(std::clamp(dot, -1.0, 1.0)), s = std::sin(angle);
        x = std::sin((1 - t) * angle) / s;
        y = std::sin(t * angle) / s;
    }
    return Quat{a.w * x + b.w * y, a.x * x + b.x * y, a.y * x + b.y * y, a.z * x + b.z * y}.unit();
}
Pose Blend(const Pose &a, const Pose &b, double t) {
    return {a.position * (1 - t) + b.position * t, Quat::Slerp(a.rotation, b.rotation, t),
            a.fov * (1 - t) + b.fov * t};
}
Vec Constraints::apply(Vec v) const {
    if (grid > 0) {
        v.x = std::round(v.x / grid) * grid;
        v.y = std::round(v.y / grid) * grid;
        v.z = std::round(v.z / grid) * grid;
    }
    if (locked[0])
        v.x = plane.x;
    if (locked[1])
        v.y = plane.y;
    if (locked[2])
        v.z = plane.z;
    return v;
}
Pose Path::toWorld(Pose p) const {
    p.position = anchor.origin + anchor.rotation.rotate(p.position * anchor.scale);
    p.rotation = anchor.rotation * p.rotation;
    return p;
}
Pose Path::toLocal(Pose p) const {
    p.position = anchor.rotation.inverse().rotate(p.position - anchor.origin) * (1 / anchor.scale);
    p.rotation = anchor.rotation.inverse() * p.rotation;
    return p;
}
Pose Path::pointWorld(std::size_t i) const {
    Pose p = points.at(i).pose;
    p.position = position(double(i));
    return toWorld(p);
}
Vec Path::position(double parameter) const {
    if (points.empty())
        return {};
    if (points.size() == 1)
        return points[0].pose.position;
    double t = std::clamp(parameter, 0.0, double(points.size() - 1));
    if (curve == Curve::Circle) {
        double a = (circleStart + circleSweep * t / double(points.size() - 1)) * Pi / 180;
        Vec p{circleRadius * std::cos(a), circleRadius * std::sin(a), 0};
        if (circlePlane == 1)
            p = {p.x, 0, p.y};
        else if (circlePlane == 2)
            p = {0, p.x, p.y};
        return circleCenter + p;
    }
    std::size_t i = (std::min)(std::size_t(t), points.size() - 2);
    double u = t - double(i);
    Vec a = points[i].pose.position, b = points[i + 1].pose.position;
    if (curve == Curve::Linear)
        return a * (1 - u) + b * u;
    if (curve == Curve::Bezier) {
        Vec c = a + points[i].outHandle, d = b + points[i + 1].inHandle;
        double v = 1 - u;
        return a * (v * v * v) + c * (3 * v * v * u) + d * (3 * v * u * u) + b * (u * u * u);
    }
    Vec prev = i ? points[i - 1].pose.position : a,
        next = i + 2 < points.size() ? points[i + 2].pose.position : b;
    Vec m0 = (b - prev) * ((1 - tension) * 0.5), m1 = (next - a) * ((1 - tension) * 0.5);
    return a * (2 * u * u * u - 3 * u * u + 1) + m0 * (u * u * u - 2 * u * u + u) +
           b * (-2 * u * u * u + 3 * u * u) + m1 * (u * u * u - u * u);
}
void Path::rebuild() {
    require(points.size() <= 4096, "Maximum 4096 points per path");
    checkVec(anchor.origin);
    checkQuat(anchor.rotation);
    require(finite(anchor.scale) && anchor.scale > 1e-6 && anchor.scale < 1e6,
            "Scale must be positive and within 0.000001..1000000");
    require(finite(duration) && duration > 0 && duration <= 86400,
            "Duration must be within 0..86400 seconds");
    require(finite(start) && finite(transition) && transition >= 0 && transition <= duration,
            "Invalid schedule or transition");
    require(finite(start + duration), "Schedule overflows");
    require(finite(tension) && tension >= 0 && tension <= 1, "Tension must be 0..1");
    require(finite(constraints.grid) && constraints.grid >= 0, "Grid must be nonnegative");
    checkVec(constraints.plane);
    require(int(curve) >= 0 && int(curve) <= 3 && int(ease) >= 0 && int(ease) <= 5 &&
                int(rotationInterp) >= 0 && int(rotationInterp) <= 2 && int(fovInterp) >= 0 &&
                int(fovInterp) <= 2,
            "Unknown interpolation mode");
    checkVec(circleCenter);
    checkVec(aimTarget);
    require(finite(circleRadius) && circleRadius > 0 && circleRadius <= 1e9 && finite(circleStart) &&
                finite(circleSweep) && std::abs(circleSweep) <= 36000 && circlePlane >= 0 && circlePlane <= 2,
            "Invalid circle");
    double holds = 0, fixed = 0;
    bool autoSegment = false;
    for (std::size_t i = 0; i < points.size(); ++i) {
        auto &p = points[i];
        checkPose(p.pose);
        checkVec(p.inHandle);
        checkVec(p.outHandle);
        require(finite(p.hold) && p.hold >= 0 && finite(p.seconds) && p.seconds >= 0,
                "Holds and segment durations must be nonnegative");
        holds += p.hold;
        if (i + 1 < points.size()) {
            fixed += p.seconds;
            autoSegment |= p.seconds == 0;
        }
    }
    require(holds + fixed <= duration + 1e-9, "Holds and explicit segments exceed total duration");
    if (points.size() > 1) {
        if (autoSegment)
            require(duration - holds - fixed > 1e-9, "No duration remains for automatic segments");
        else
            require(std::abs(duration - holds - fixed) < 1e-7,
                    "Explicit segment times plus holds must equal duration");
    }
    checkKeys(speed);
    require(speed.size() >= 2 && speed.front().time == 0 && speed.back().time == 1 &&
                speed.front().value == 0 && speed.back().value == 1,
            "Speed curve needs endpoints (0,0) and (1,1)");
    double last = -1;
    for (auto k : speed) {
        require(finite(k.value) && k.value >= last && k.value >= 0 && k.value <= 1,
                "Speed curve must be monotonic within 0..1");
        last = k.value;
    }
    checkKeys(progress);
    for (auto k : progress)
        require(finite(k.value) && k.value >= 0 && k.value <= 1, "Progress must be 0..1");
    checkKeys(fovs);
    for (auto k : fovs)
        require(finite(k.value) && k.value > 1 && k.value < 179, "FOV must be within 1..179");
    checkKeys(rotations);
    for (auto &k : rotations)
        checkQuat(k.value);
    require(progress.empty() || (holds == 0 && fixed == 0),
            "Custom progress replaces automatic timing: clear holds and segment times first");
    m_Cache = std::make_shared<Cache>();
    m_Cache->arc.clear();
    m_Cache->nodes.assign(points.size(), 0);
    m_Cache->length = 0;
    if (points.empty())
        return;
    m_Cache->arc.push_back({0, 0});
    Vec prev = position(0);
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        // Bounded arc-length table, independent of timing and world placement.
        int steps = curve == Curve::Linear ? 1 : 128;
        for (int j = 1; j <= steps; ++j) {
            double t = double(i) + double(j) / steps;
            Vec p = position(t);
            m_Cache->length += (p - prev).length();
            require(finite(m_Cache->length), "Path length overflows");
            m_Cache->arc.push_back({t, m_Cache->length});
            prev = p;
        }
        m_Cache->nodes[i + 1] = m_Cache->length;
    }
    if (curve == Curve::Circle && points.size() > 1) {
        // Circle distance is analytic; no chord-length approximation.
        m_Cache->length = circleRadius * std::abs(circleSweep) * Pi / 180;
        for (auto &s : m_Cache->arc)
            s.distance = m_Cache->length * s.parameter / double(points.size() - 1);
        for (std::size_t i = 0; i < points.size(); ++i)
            m_Cache->nodes[i] = m_Cache->length * double(i) / double(points.size() - 1);
    }
}
std::vector<TimingSpan> Path::timeline() const {
    std::vector<TimingSpan> spans(points.size());
    double remaining = duration, weights = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        remaining -= points[i].hold;
        if (i + 1 < points.size()) {
            remaining -= points[i].seconds;
            if (points[i].seconds == 0)
                weights += m_Cache->length > 1e-9 ? m_Cache->nodes[i + 1] - m_Cache->nodes[i] : 1;
        }
    }
    // Zero-distance segments still animate orientation/FOV: give them a small
    // weight, or equal weights for a wholly stationary path.
    if (weights <= 1e-12)
        weights = 0;
    double clock = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        spans[i].arrival = clock;
        clock += points[i].hold;
        spans[i].departure = clock;
        if (i + 1 < points.size()) {
            double weight = m_Cache->length > 1e-9 ? m_Cache->nodes[i + 1] - m_Cache->nodes[i] : 1;
            spans[i].travel =
                points[i].seconds > 0 ? points[i].seconds : (weights > 0 ? remaining * weight / weights : 0);
            clock += spans[i].travel;
        }
    }
    return spans;
}
double Path::distanceParameter(double fraction) const {
    if (points.size() < 2)
        return 0;
    if (fraction <= 0)
        return 0;
    if (fraction >= 1)
        return double(points.size() - 1);
    if (m_Cache->length <= 1e-9)
        return clamp(fraction) * double(points.size() - 1);
    double d = clamp(fraction) * m_Cache->length;
    auto b = std::lower_bound(m_Cache->arc.begin(), m_Cache->arc.end(), d,
                              [](const ArcSample &s, double v) { return s.distance < v; });
    if (b == m_Cache->arc.begin())
        return 0;
    if (b == m_Cache->arc.end())
        return double(points.size() - 1);
    auto a = b - 1;
    double delta = b->distance - a->distance;
    return a->parameter + (b->parameter - a->parameter) * (delta > 0 ? (d - a->distance) / delta : 0);
}
Pose Path::atDistance(double fraction) const {
    require(!points.empty(), "Path has no points");
    double t = distanceParameter(fraction);
    std::size_t i = points.size() > 1 ? (std::min)(std::size_t(t), points.size() - 2) : 0;
    double u = t - double(i);
    Pose p = points[i].pose;
    if (i + 1 < points.size()) {
        double r = rotationInterp == Interp::Step ? 0 : rotationInterp == Interp::Smooth ? smooth(u) : u;
        double f = fovInterp == Interp::Step ? 0 : fovInterp == Interp::Smooth ? smooth(u) : u;
        p.rotation = Quat::Slerp(points[i].pose.rotation, points[i + 1].pose.rotation, r);
        p.fov = points[i].pose.fov + (points[i + 1].pose.fov - points[i].pose.fov) * f;
        if (u >= 1)
            p = points[i + 1].pose;
    }
    p.position = position(t);
    return toWorld(p);
}
double Path::eased(double t, bool a, bool b) const {
    t = clamp(t);
    // End velocities of 0 (stop) or 1: monotonic cubic Hermite.
    if (a || b) {
        double m0 = a ? 0 : 1, m1 = b ? 0 : 1;
        t = (-2 * t * t * t + 3 * t * t) + (t * t * t - 2 * t * t + t) * m0 + (t * t * t - t * t) * m1;
    }
    switch (ease) {
    case Ease::In:
        return t * t;
    case Ease::Out:
        return 1 - (1 - t) * (1 - t);
    case Ease::Smooth:
        return smooth(t);
    case Ease::Smoother:
        return t * t * t * (t * (6 * t - 15) + 10);
    case Ease::Custom:
        return scalar(speed, t);
    default:
        return t;
    }
}
Pose Path::evaluate(double seconds) const {
    require(!points.empty() && finite(seconds), "Cannot evaluate empty path or non-finite time");
    double t = std::clamp(seconds, 0.0, duration), normalized = t / duration;
    Pose p;
    if (!progress.empty())
        p = atDistance(scalar(progress, normalized));
    else if (t >= duration)
        p = atDistance(1);
    else {
        auto spans = timeline();
        double fraction = 1;
        bool found = false;
        for (std::size_t i = 0; i < points.size(); ++i) {
            double node = m_Cache->length > 1e-9 ? m_Cache->nodes[i] / m_Cache->length
                          : points.size() > 1    ? double(i) / double(points.size() - 1)
                                                 : 0;
            if (t <= spans[i].departure) {
                p = pointWorld(i);
                found = true;
                break;
            }
            if (i + 1 < points.size() && t < spans[i].departure + spans[i].travel) {
                double u =
                    eased((t - spans[i].departure) / spans[i].travel, points[i].stop || points[i].hold > 0,
                          points[i + 1].stop || points[i + 1].hold > 0);
                double next = m_Cache->length > 1e-9 ? m_Cache->nodes[i + 1] / m_Cache->length
                                                     : double(i + 1) / double(points.size() - 1);
                fraction = node + (next - node) * u;
                if (std::abs(next - node) < 1e-12) {
                    double r = rotationInterp == Interp::Step     ? 0
                               : rotationInterp == Interp::Smooth ? smooth(u)
                                                                  : u;
                    double f = fovInterp == Interp::Step ? 0 : fovInterp == Interp::Smooth ? smooth(u) : u;
                    Pose local = points[i].pose;
                    local.rotation = Quat::Slerp(local.rotation, points[i + 1].pose.rotation, r);
                    local.fov += (points[i + 1].pose.fov - local.fov) * f;
                    p = toWorld(local);
                    found = true;
                }
                break;
            }
        }
        if (!found)
            p = atDistance(fraction);
    }
    if (!rotations.empty()) {
        Quat r = rotations.front().value;
        auto b = std::upper_bound(rotations.begin(), rotations.end(), normalized,
                                  [](double t, const RotationKey &k) { return t < k.time; });
        if (b == rotations.end())
            r = rotations.back().value;
        else if (b != rotations.begin()) {
            auto a = b - 1;
            double u = (normalized - a->time) / (b->time - a->time);
            if (rotationInterp == Interp::Step)
                u = 0;
            else if (rotationInterp == Interp::Smooth)
                u = smooth(u);
            r = Quat::Slerp(a->value, b->value, u);
        }
        p.rotation = anchor.rotation * r;
    }
    if (!fovs.empty())
        p.fov = scalar(fovs, normalized, fovInterp);
    if (fixedAim) {
        Vec d = toWorld({aimTarget, {}, 90}).position - p.position;
        if (d.length() > 1e-9)
            p.rotation = Quat::Euler(-std::atan2(d.z, std::hypot(d.x, d.y)) * 180 / Pi,
                                     std::atan2(d.y, d.x) * 180 / Pi, 0);
    }
    return p;
}
double Path::pointTime(std::size_t i) const {
    require(i < points.size(), "Invalid point index");
    if (progress.empty())
        return timeline().at(i).arrival;
    double node = m_Cache->length > 1e-9 ? m_Cache->nodes.at(i) / m_Cache->length
                  : points.size() > 1    ? double(i) / double(points.size() - 1)
                                         : 0;
    if (node == progress.front().value)
        return progress.front().time * duration;
    for (std::size_t j = 1; j < progress.size(); ++j) {
        auto a = progress[j - 1], b = progress[j];
        if (node >= (std::min)(a.value, b.value) && node <= (std::max)(a.value, b.value) &&
            a.value != b.value)
            return duration * (a.time + (b.time - a.time) * (node - a.value) / (b.value - a.value));
    }
    throw std::runtime_error("This point is not visited by the custom motion track");
}
void Path::capture(Pose world, int index, bool replace) {
    require(curve != Curve::Circle, "Switch curve to linear/smooth/bezier before editing circle points");
    Pose p = toLocal(world);
    p.position = constraints.apply(p.position);
    if (replace) {
        require(index >= 0 && std::size_t(index) < points.size(), "Select a point first");
        points[index].pose = p;
        points[index].inHandle = constraints.apply(p.position + points[index].inHandle) - p.position;
        points[index].outHandle = constraints.apply(p.position + points[index].outHandle) - p.position;
    } else {
        if (index < 0)
            index = int(points.size());
        require(std::size_t(index) <= points.size(), "Invalid insert index");
        Point v;
        v.pose = p;
        points.insert(points.begin() + index, v);
    }
    rebuild();
}
void Validate(State &s) {
    require(s.paths.size() <= 128, "Maximum 128 paths");
    std::vector<std::uint64_t> ids;
    std::size_t totalPoints = 0, totalKeys = 0;
    for (auto &p : s.paths) {
        totalPoints += p.points.size();
        totalKeys += p.progress.size() + p.rotations.size() + p.fovs.size() + p.speed.size();
        require(totalKeys <= 65536, "Maximum 65536 animation keys across all paths");
        require(totalPoints <= 16384, "Maximum 16384 points across all paths");
        require(p.progress.size() <= 16384 && p.rotations.size() <= 16384 && p.fovs.size() <= 16384 &&
                    p.speed.size() <= 16384,
                "Animation track exceeds 16384 keys");
        require(p.id > 0 && p.id < std::numeric_limits<std::uint64_t>::max(), "Invalid path ID");
        require(std::find(ids.begin(), ids.end(), p.id) == ids.end(), "Duplicate path ID");
        ids.push_back(p.id);
        require(p.name.size() <= 256, "Path name too long");
        p.rebuild();
        s.nextId = (std::max)(s.nextId, p.id + 1);
    }
    require(s.selected >= -1 && s.selected < int(s.paths.size()), "Invalid path selection");
    if (s.selected < 0)
        s.point = -1;
    else
        require(s.point >= -1 && s.point < int(s.paths[s.selected].points.size()), "Invalid point selection");
}
Path &Editor::active() {
    require(state.selected >= 0 && state.selected < int(state.paths.size()), "No path selected");
    return state.paths[state.selected];
}
const Path &Editor::active() const {
    require(state.selected >= 0 && state.selected < int(state.paths.size()), "No path selected");
    return state.paths[state.selected];
}
Path &Editor::ensurePath() {
    if (state.selected < 0)
        create();
    return active();
}
void Editor::commit(State next) {
    Validate(next);
    m_Undo.push_back(state);
    for (auto &p : m_Undo.back().paths)
        p.m_Cache = std::make_shared<Path::Cache>();
    if (m_Undo.size() > 64)
        m_Undo.erase(m_Undo.begin());
    state = std::move(next);
    m_Redo.clear();
}
void Editor::create(std::string name) {
    State n = state;
    Path p;
    p.id = n.nextId++;
    p.name = name.empty() ? "Path " + std::to_string(p.id) : name;
    n.paths.push_back(p);
    n.selected = int(n.paths.size() - 1);
    n.point = -1;
    commit(std::move(n));
}
void Editor::duplicate() {
    State n = state;
    Path p = active();
    p.id = n.nextId++;
    p.name += " copy";
    p.scheduled = false;
    n.paths.push_back(p);
    n.selected = int(n.paths.size() - 1);
    commit(std::move(n));
}
void Editor::remove() {
    active();
    State n = state;
    n.paths.erase(n.paths.begin() + n.selected);
    n.selected = n.paths.empty() ? -1 : (std::min)(n.selected, int(n.paths.size() - 1));
    n.point = -1;
    commit(std::move(n));
}
bool Editor::undo() {
    if (m_Undo.empty())
        return false;
    m_Redo.push_back(state);
    for (auto &p : m_Redo.back().paths)
        p.m_Cache = std::make_shared<Path::Cache>();
    state = std::move(m_Undo.back());
    Validate(state);
    m_Undo.pop_back();
    return true;
}
bool Editor::redo() {
    if (m_Redo.empty())
        return false;
    m_Undo.push_back(state);
    for (auto &p : m_Undo.back().paths)
        p.m_Cache = std::make_shared<Path::Cache>();
    state = std::move(m_Redo.back());
    Validate(state);
    m_Redo.pop_back();
    return true;
}
bool Editor::dirty() const {
    State canonical = state;
    canonical.selected = -1;
    canonical.point = -1;
    return m_Saved.empty() ? !state.paths.empty() : Serialize(canonical) != m_Saved;
}
void Editor::markSaved() {
    State canonical = state;
    canonical.selected = -1;
    canonical.point = -1;
    m_Saved = Serialize(canonical);
}
bool Editor::evaluate(double time, Pose &pose) const {
    if (!std::isfinite(time))
        return false;
    const Path *current = nullptr, *previous = nullptr;
    for (const auto &p : state.paths)
        if (p.scheduled && !p.points.empty() && p.start <= time) {
            if (!current || p.start > current->start || (p.start == current->start && p.id > current->id))
                current = &p;
        }
    if (!current || (time > current->start + current->duration && !current->holdEnd))
        return false;
    pose = current->evaluate(time - current->start);
    if (current->transition > 0 && time < current->start + current->transition) {
        for (const auto &p : state.paths)
            if (p.scheduled && !p.points.empty() && p.start < current->start &&
                (!previous || p.start > previous->start ||
                 (p.start == previous->start && p.id > previous->id)))
                previous = &p;
        if (previous)
            pose = Blend(previous->evaluate(time - previous->start), pose,
                         smooth(clamp((time - current->start) / current->transition)));
    }
    return true;
}
std::vector<std::string> Editor::overlaps() const {
    std::vector<std::string> result;
    for (std::size_t i = 0; i < state.paths.size(); ++i)
        for (std::size_t j = i + 1; j < state.paths.size(); ++j) {
            auto &a = state.paths[i];
            auto &b = state.paths[j];
            if (a.scheduled && b.scheduled && a.start <= b.start + b.duration &&
                b.start <= a.start + a.duration)
                result.push_back(a.name + " / " + b.name);
        }
    return result;
}
std::string Editor::xml(bool all) const {
    if (all)
        return Serialize(state);
    State s;
    s.paths.push_back(active());
    s.selected = 0;
    return Serialize(s);
}
void Editor::loadXml(const std::string &xml, bool replace) {
    State loaded = Deserialize(xml), n = state;
    require(!loaded.paths.empty(), "XML collection contains no paths");
    if (replace)
        n = std::move(loaded);
    else {
        for (auto p : loaded.paths) {
            p.id = n.nextId++;
            n.paths.push_back(std::move(p));
        }
        n.selected = int(n.paths.size() - loaded.paths.size());
        n.point = -1;
    }
    commit(std::move(n));
}
} // namespace cinematic
