#include "shared/CinematicPath.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace cinematic;
namespace {
int checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
void near(double a, double b, double tolerance = 1e-7) {
    check(std::abs(a - b) <= tolerance, "Values differ");
}
void throws(const std::function<void()> &fn) {
    bool caught = false;
    try {
        fn();
    } catch (const std::exception &) {
        caught = true;
    }
    check(caught, "Expected rejection");
}
Point pt(double x, double y = 0, double z = 0) {
    Point p;
    p.pose.position = {x, y, z};
    return p;
}
Path line() {
    Path p;
    p.id = 1;
    p.curve = Curve::Linear;
    p.duration = 10;
    p.points = {pt(0), pt(50), pt(100)};
    p.rebuild();
    return p;
}
State state(Path p) {
    State s;
    s.paths.push_back(p);
    s.selected = 0;
    s.point = 0;
    Validate(s);
    return s;
}
void timing() {
    auto p = line();
    near(p.length(), 100);
    near(p.evaluate(5).position.x, 50);
    p.points[1].hold = 2;
    p.rebuild();
    auto ts = p.timeline();
    near(ts[1].arrival, 4);
    near(ts[1].departure, 6);
    near(ts[2].arrival, 10);
    near(p.evaluate(4).position.x, 50);
    near(p.evaluate(5).position.x, 50);
    near(p.evaluate(6).position.x, 50);
    near(p.evaluate(10).position.x, 100);
    p.points[0].seconds = 3;
    p.rebuild();
    near(p.pointTime(1), 3);
    near(p.pointTime(2), 10);
    p.points[1].seconds = 5;
    p.rebuild();
    near(p.pointTime(2), 10);
    p.duration = 11;
    throws([&] { p.rebuild(); });
    p.duration = 9;
    throws([&] { p.rebuild(); });
    p = line();
    p.points[1].stop = true;
    p.rebuild();
    double epsilon = 1e-4;
    check(std::abs(p.evaluate(5 - epsilon).position.x - 50) / epsilon < 0.01, "Stop incoming velocity");
    check(std::abs(p.evaluate(5 + epsilon).position.x - 50) / epsilon < 0.01, "Stop outgoing velocity");
    p = line();
    p.progress = {{0, 0}, {0.5, 1}, {1, 0}};
    p.rebuild();
    near(p.evaluate(5).position.x, 100);
    near(p.evaluate(10).position.x, 0);
    p.points[0].hold = 1;
    throws([&] { p.rebuild(); });
    p = line();
    p.ease = Ease::Custom;
    p.speed = {{0, 0}, {0.5, 0.25}, {1, 1}};
    p.rebuild();
    near(p.evaluate(2.5).position.x, 12.5);
    p.speed = {{0, 0}, {0.5, 0.9}, {0.7, 0.3}, {1, 1}};
    throws([&] { p.rebuild(); });
    p = line();
    p.points = {pt(0)};
    p.points[0].hold = 2;
    p.rebuild();
    near(p.evaluate(9).position.x, 0);
    p.rotations = {{0, Quat::Euler(0, 0, 0)}, {1, Quat::Euler(0, 90, 0)}};
    p.fovs = {{0, 90}, {1, 50}};
    p.rebuild();
    near(p.evaluate(5).rotation.euler().y, 45);
    near(p.evaluate(5).fov, 70);
    p = line();
    p.points = {pt(0), pt(0)};
    p.points[1].pose.rotation = Quat::Euler(0, 90, 0);
    p.rebuild();
    near(p.evaluate(5).rotation.euler().y, 45);

    // A zero-length segment within a moving path can still animate a turn.
    p = line();
    p.points = {pt(0), pt(0), pt(100)};
    p.points[0].seconds = 2;
    p.points[1].pose.rotation = Quat::Euler(0, 90, 0);
    p.rebuild();
    near(p.evaluate(1).position.x, 0);
    near(p.evaluate(1).rotation.euler().y, 45);
    p = line();
    p.progress = {{0, 0}, {0.5, 1}, {1, 0}};
    p.rebuild();
    near(p.pointTime(1), 2.5);
}
void geometry() {
    {
        auto p = line();
        p.points = {pt(0), pt(100), pt(100)};
        p.points.back().pose.rotation = Quat::Euler(0, 90, 0);
        p.rebuild();
        near(p.atDistance(1).rotation.euler().y, 90);
        near(p.evaluate(10).rotation.euler().y, 90);
    }

    auto p = line();
    p.curve = Curve::Smooth;
    p.points = {pt(0), pt(30, 50), pt(100, 10), pt(160)};
    p.rebuild();
    near((p.position(1) - p.points[1].pose.position).length(), 0);
    // Within a curved segment constant time increments should cover similar
    // distance: arc-length mapping, rather than raw spline parameterization.
    std::vector<double> d;
    for (int i = 0; i < 100; ++i)
        d.push_back((p.evaluate((i + 1) * 0.1).position - p.evaluate(i * 0.1).position).length());
    auto range = std::minmax_element(d.begin(), d.end());
    check(*range.second / *range.first < 1.06, "Constant speed on spline");
    p = line();
    p.curve = Curve::Bezier;
    p.points = {pt(0), pt(100)};
    p.points[0].outHandle = {0, 100, 0};
    p.points[1].inHandle = {0, 100, 0};
    p.rebuild();
    near(p.position(0.5).x, 50);
    near(p.position(0.5).y, 75);
    p = line();
    p.curve = Curve::Circle;
    p.circleRadius = 100;
    p.circleSweep = 180;
    p.circleCenter = {10, 20, 30};
    p.rebuild();
    near(p.length(), 100 * Pi);
    for (int i = 0; i < 20; ++i)
        near((p.evaluate(i * 0.5).position - p.circleCenter).length(), 100);
    p = line();
    p.anchor = {{500, 200, 10}, Quat::Euler(0, 90, 0), 2};
    p.rebuild();
    Pose world = p.evaluate(10);
    near(world.position.x, 500);
    near(world.position.y, 400);
    near(world.rotation.euler().y, 90);
    near(p.length(), 200);
    near(p.points.back().pose.position.x, 100);
    Pose pose{{20, 30, 40}, Quat::Euler(20, -40, 15), 80};
    auto round = p.toWorld(p.toLocal(pose));
    near((round.position - pose.position).length(), 0);
    near(std::abs(round.rotation.w * pose.rotation.w + round.rotation.x * pose.rotation.x +
                  round.rotation.y * pose.rotation.y + round.rotation.z * pose.rotation.z),
         1);
    p = line();
    p.constraints.grid = 8;
    p.constraints.locked[2] = true;
    p.constraints.plane.z = 13;
    p.capture({{11, 19, 27}, {}, 90});
    near(p.points.back().pose.position.x, 8);
    near(p.points.back().pose.position.y, 16);
    near(p.points.back().pose.position.z, 13);
    p = line();
    p.fixedAim = true;
    p.aimTarget = {0, 100, 0};
    p.rebuild();
    near(p.evaluate(0).rotation.euler().y, 90);
    auto a = Quat::Euler(0, 179, 0), b = Quat::Euler(0, -179, 0);
    near(std::abs(Quat::Slerp(a, b, 0.5).euler().y), 180);
    for (int pitch : {-90, -30, 0, 45, 90})
        for (int yaw : {-170, -30, 80})
            for (int roll : {-45, 0, 60}) {
                Quat q = Quat::Euler(pitch, yaw, roll);
                Vec e = q.euler();
                Quat r = Quat::Euler(e.x, e.y, e.z);
                near(std::abs(q.w * r.w + q.x * r.x + q.y * r.y + q.z * r.z), 1, 1e-6);
            }
}
void persistence() {
    // Check all geometry/easing combinations under direct, nonsequential seeks.
    for (Curve curve : {Curve::Linear, Curve::Smooth, Curve::Bezier, Curve::Circle})
        for (Ease ease : {Ease::Linear, Ease::In, Ease::Out, Ease::Smooth, Ease::Smoother, Ease::Custom}) {
            auto p = line();
            p.curve = curve;
            p.ease = ease;
            p.points[0].outHandle = {0, 20, 0};
            p.points[1].inHandle = {0, 20, 0};
            p.rebuild();
            for (double t : {0.0, 8.8, 2.2, 10.0, 1.0, 8.8, 0.0}) {
                Pose a = p.evaluate(t);
                p.evaluate(10 - t);
                Pose b = p.evaluate(t);
                near((a.position - b.position).length(), 0);
                near(a.fov, b.fov);
            }
        }

    auto p = line();
    p.name = "A & B <camera> \"test\"";
    p.anchor = {{1, 2, 3}, Quat::Euler(20, 50, 10), 2};
    p.points[1].hold = 2;
    p.points[1].stop = true;
    p.points[0].outHandle = {5, 6, 7};
    p.ease = Ease::Custom;
    p.constraints.grid = 16;
    p.constraints.locked[2] = true;
    p.constraints.plane.z = 8;
    p.rotations = {{0, {}}, {1, Quat::Euler(0, 90, 0)}};
    p.fovs = {{0, 80}, {1, 100}};
    p.scheduled = true;
    p.transition = 1;
    p.rebuild();
    State s = state(p);
    auto text = Serialize(s);
    auto loaded = Deserialize(text);
    check(loaded.paths[0].name == p.name, "Escaped name roundtrip");
    for (int i = 0; i <= 100; ++i) {
        auto a = p.evaluate(i * 0.1), b = loaded.paths[0].evaluate(i * 0.1);
        near((a.position - b.position).length(), 0);
        near(a.fov, b.fov);
        near(std::abs(a.rotation.w * b.rotation.w + a.rotation.x * b.rotation.x +
                      a.rotation.y * b.rotation.y + a.rotation.z * b.rotation.z),
             1);
    }
    Editor e;
    e.commit(s);
    e.markSaved();
    check(!e.dirty(), "Saved clean");
    e.state.point = 1;
    check(!e.dirty(), "Selection is not a data edit");
    auto invalid = e.state;
    invalid.paths[0].duration = -1;
    auto unchanged = e.xml();
    throws([&] { e.commit(invalid); });
    check(e.xml() == unchanged, "Invalid edit is atomic");
    e.duplicate();
    check(e.dirty(), "Copy dirty");
    check(!e.active().scheduled, "Copy must not compete with original schedule");
    e.undo();
    check(!e.dirty(), "Undo saved state");
    e.redo();
    check(e.state.paths.size() == 2, "Redo copy");
    auto before = e.xml();
    throws([&] { e.loadXml("<cinematic version=\"1\"><path duration=\"nan\"/></cinematic>", true); });
    check(e.xml() == before, "Invalid load is atomic");
    e.loadXml(text, false);
    check(e.state.paths.size() == 3, "Append");
    check(e.state.paths[2].id != 1, "Append remaps IDs");
    e.undo();
    check(e.state.paths.size() == 2, "Undo append");
    for (const std::string bad : {"<cinematic version=\"99\"/>", "<!DOCTYPE x><cinematic version=\"1\"/>",
                                  "<cinematic version=\"1\"><path id=\"1\" name=\"&bad;\"/></cinematic>",
                                  "<cinematic version=\"1\" version=\"1\"/>",
                                  "<cinematic version=\"1\"></wrong>", "<cinematic version=\"1\"/>garbage",
                                  "<cinematic version=\"1\"><path id=\"1\" duration=\"-1\"/></cinematic>"})
        throws([&] { Deserialize(bad); });
    Editor classic;
    classic.loadXml(
        "<campath offset=\"2\"><points><p t=\"10\" x=\"0\" y=\"0\" z=\"0\" rx=\"0\" ry=\"0\" rz=\"0\"/><p "
        "t=\"14\" x=\"40\" y=\"0\" z=\"0\" rx=\"0\" ry=\"0\" rz=\"90\"/></points></campath>",
        true);
    near(classic.active().start, 12);
    near(classic.active().duration, 4);
    near(classic.active().evaluate(2).position.x, 20);
    near(classic.active().evaluate(2).rotation.euler().y, 45);
    Editor exported;
    exported.loadXml(classic.legacyXml(), true);
    near(exported.active().evaluate(2).position.x, 20);
    near(exported.active().start, 12);
    s.paths[0].duration = std::numeric_limits<double>::infinity();
    throws([&] { Validate(s); });
}
void schedule() {
    auto a = line();
    a.scheduled = true;
    a.start = 10;
    a.holdEnd = false;
    Editor e;
    e.commit(state(a));
    Pose p;
    check(!e.evaluate(std::numeric_limits<double>::infinity(), p), "Reject non-finite schedule time");
    check(!e.evaluate(9, p), "Release before start");
    check(e.evaluate(15, p), "Scheduled active");
    near(p.position.x, 50);
    check(!e.evaluate(21, p), "Release after end");
    auto n = e.state;
    auto b = a;
    b.id = 2;
    b.start = 18;
    b.anchor.origin = {1000, 0, 0};
    n.paths.push_back(b);
    e.commit(n);
    check(e.overlaps().size() == 1, "Overlap detection");
    check(e.evaluate(19, p), "New path wins");
    near(p.position.x, 1010);
    auto exact = p;
    check(e.evaluate(12, p), "Backward seek");
    near(p.position.x, 20);
    e.evaluate(19, p);
    near(p.position.x, exact.position.x);
    n = e.state;
    n.paths[1].transition = 2;
    e.commit(n);
    e.evaluate(18, p);
    near(p.position.x, 80);
    e.evaluate(19, p);
    near(p.position.x, 550);
    e.evaluate(20, p);
    near(p.position.x, 1020);
    n = e.state;
    n.paths[1].holdEnd = true;
    e.commit(n);
    e.evaluate(1000, p);
    near(p.position.x, 1100);
    n = e.state;
    n.paths[1].start = 10;
    n.paths[1].transition = 0;
    e.commit(n);
    e.evaluate(11, p);
    near(p.position.x, 1010);
}
} // namespace
int main() {
    try {
        timing();
        geometry();
        persistence();
        schedule();
        std::cout << checks << " checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << "After " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
