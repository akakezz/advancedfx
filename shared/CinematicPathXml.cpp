#include "CinematicPath.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>

namespace cinematic {
namespace {
struct Node {
    std::string name;
    std::map<std::string, std::string> attrs;
    std::vector<Node> children;
};
[[noreturn]] void fail(const char *why) { throw std::runtime_error(why); }
std::string escape(const std::string &s) {
    std::string r;
    for (unsigned char c : s)
        switch (c) {
        case '&':
            r += "&amp;";
            break;
        case '<':
            r += "&lt;";
            break;
        case '>':
            r += "&gt;";
            break;
        case '"':
            r += "&quot;";
            break;
        case '\'':
            r += "&apos;";
            break;
        default:
            if (c < 32 && c != 9 && c != 10 && c != 13)
                fail("Invalid XML character");
            r += char(c);
        }
    return r;
}
std::string unescape(const std::string &s) {
    std::string r;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            auto e = s.find(';', i);
            if (e == std::string::npos)
                fail("Invalid XML entity");
            auto v = s.substr(i, e - i + 1);
            if (v == "&amp;")
                r += '&';
            else if (v == "&lt;")
                r += '<';
            else if (v == "&gt;")
                r += '>';
            else if (v == "&quot;")
                r += '"';
            else if (v == "&apos;")
                r += '\'';
            else
                fail("Unsupported XML entity (DTD and external entities are not allowed)");
            i = e;
        } else {
            if (s[i] == '<' ||
                (static_cast<unsigned char>(s[i]) < 32 && s[i] != 9 && s[i] != 10 && s[i] != 13))
                fail("Invalid XML attribute");
            r += s[i];
        }
    }
    return r;
}
// Deliberately small, strict XML reader for this data format. No DTD, external
// entities, text payloads or processing instructions other than the declaration.
class Reader {
    const std::string &s;
    std::size_t p = 0, nodes = 0;
    bool has(const char *t) const { return s.compare(p, std::char_traits<char>::length(t), t) == 0; }
    void ws() {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n'))
            ++p;
    }
    void expect(char c) {
        if (p >= s.size() || s[p++] != c)
            fail("Malformed XML");
    }
    std::string name() {
        std::size_t b = p;
        while (p < s.size() && ((s[p] >= 'a' && s[p] <= 'z') || (s[p] >= 'A' && s[p] <= 'Z') ||
                                (s[p] >= '0' && s[p] <= '9') || s[p] == '_' || s[p] == '-'))
            ++p;
        if (b == p)
            fail("Expected XML name");
        return s.substr(b, p - b);
    }
    void comments() {
        ws();
        while (has("<!--")) {
            auto e = s.find("-->", p + 4);
            if (e == std::string::npos)
                fail("Unclosed XML comment");
            p = e + 3;
            ws();
        }
    }
    Node node(int depth) {
        if (depth > 8 || ++nodes > 300000)
            fail("XML structure exceeds limits");
        expect('<');
        Node n;
        n.name = name();
        while (true) {
            std::size_t before = p;
            ws();
            if (has("/>")) {
                p += 2;
                return n;
            }
            if (has(">")) {
                ++p;
                break;
            }
            if (before == p)
                fail("XML attributes require whitespace");
            auto k = name();
            ws();
            expect('=');
            ws();
            if (p >= s.size())
                fail("Missing XML value");
            char q = s[p++];
            if (q != '\'' && q != '"')
                fail("Unquoted XML attribute");
            auto e = s.find(q, p);
            if (e == std::string::npos)
                fail("Unclosed XML attribute");
            if (!n.attrs.emplace(k, unescape(s.substr(p, e - p))).second)
                fail("Duplicate XML attribute");
            p = e + 1;
        }
        while (true) {
            comments();
            if (has("</")) {
                p += 2;
                auto end = name();
                ws();
                expect('>');
                if (end != n.name)
                    fail("Mismatched XML element");
                return n;
            }
            n.children.push_back(node(depth + 1));
        }
    }

  public:
    explicit Reader(const std::string &v) : s(v) {}
    Node read() {
        if (s.size() > 16 * 1024 * 1024)
            fail("XML exceeds 16 MiB limit");
        if (s.compare(0, 3, "\xef\xbb\xbf") == 0)
            p = 3;
        ws();
        if (has("<?xml ")) {
            auto e = s.find("?>", p + 6);
            if (e == std::string::npos)
                fail("Unclosed XML declaration");
            p = e + 2;
        }
        comments();
        Node n = node(0);
        comments();
        if (p != s.size())
            fail("Unexpected data after XML document");
        return n;
    }
};
std::string attr(const Node &n, const char *k, std::string fallback = "") {
    auto a = n.attrs.find(k);
    return a == n.attrs.end() ? fallback : a->second;
}
double number(const Node &n, const char *k, double fallback = 0, bool required = false) {
    auto a = n.attrs.find(k);
    if (a == n.attrs.end()) {
        if (required)
            fail("Missing required numeric XML attribute");
        return fallback;
    }
    std::istringstream in(a->second);
    in.imbue(std::locale::classic());
    double v;
    in >> std::noskipws >> v;
    if (!in || in.peek() != std::char_traits<char>::eof() || !std::isfinite(v))
        fail("Invalid numeric XML attribute");
    return v;
}
int integer(const Node &n, const char *k, int fallback = 0) {
    double v = number(n, k, fallback);
    if (v != std::floor(v) || v < -1000000 || v > 1000000)
        fail("Invalid integer XML attribute");
    return int(v);
}
bool boolean(const Node &n, const char *k, bool fallback = false) {
    double v = number(n, k, fallback ? 1 : 0);
    if (v != 0 && v != 1)
        fail("Invalid boolean XML attribute");
    return v != 0;
}
Vec vec(const Node &n, const char *prefix) {
    std::string p = prefix;
    return {number(n, (p + "x").c_str()), number(n, (p + "y").c_str()), number(n, (p + "z").c_str())};
}
Quat quat(const Node &n) {
    if (n.attrs.count("qw"))
        return {number(n, "qw", 1), number(n, "qx"), number(n, "qy"), number(n, "qz")};
    return Quat::Euler(number(n, "ry"), number(n, "rz"), number(n, "rx"));
}
Pose pose(const Node &n) { return {vec(n, ""), quat(n), number(n, "fov", 90)}; }
struct Writer {
    std::ostringstream out;
    Writer() {
        out.imbue(std::locale::classic());
        out << std::setprecision(17);
    }
    void a(const char *k, double v) { out << ' ' << k << "=\"" << v << '"'; }
    void text(const char *k, const std::string &v) { out << ' ' << k << "=\"" << escape(v) << '"'; }
    void v(const char *prefix, Vec v) {
        std::string p = prefix;
        a((p + "x").c_str(), v.x);
        a((p + "y").c_str(), v.y);
        a((p + "z").c_str(), v.z);
    }
    void q(Quat q) {
        a("qw", q.w);
        a("qx", q.x);
        a("qy", q.y);
        a("qz", q.z);
    }
    void p(Pose p) {
        v("", p.position);
        q(p.rotation);
        a("fov", p.fov);
    }
};
void scalarTrack(Writer &w, const char *name, const std::vector<ScalarKey> &keys) {
    for (auto k : keys) {
        w.out << "    <" << name;
        w.a("t", k.time);
        w.a("value", k.value);
        w.out << "/>\n";
    }
}
} // namespace
std::string Serialize(const State &s) {
    Writer w;
    w.out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<cinematic version=\"1\"";
    w.a("selected", s.selected);
    w.a("point", s.point);
    w.out << ">\n";
    for (const auto &p : s.paths) {
        w.out << "  <path";
        w.text("id", std::to_string(p.id));
        w.text("name", p.name);
        w.a("curve", int(p.curve));
        w.a("tension", p.tension);
        w.a("duration", p.duration);
        w.a("start", p.start);
        w.a("scheduled", p.scheduled);
        w.a("holdEnd", p.holdEnd);
        w.a("transition", p.transition);
        w.a("ease", int(p.ease));
        w.a("rotationInterp", int(p.rotationInterp));
        w.a("fovInterp", int(p.fovInterp));
        w.a("fixedAim", p.fixedAim);
        w.v("aim", p.aimTarget);
        w.out << ">\n";
        w.out << "    <anchor";
        w.v("", p.anchor.origin);
        w.q(p.anchor.rotation);
        w.a("scale", p.anchor.scale);
        w.out << "/>\n";
        w.out << "    <constraints";
        w.a("grid", p.constraints.grid);
        w.a("lockX", p.constraints.locked[0]);
        w.a("lockY", p.constraints.locked[1]);
        w.a("lockZ", p.constraints.locked[2]);
        w.v("", p.constraints.plane);
        w.out << "/>\n";
        w.out << "    <circle";
        w.v("", p.circleCenter);
        w.a("radius", p.circleRadius);
        w.a("start", p.circleStart);
        w.a("sweep", p.circleSweep);
        w.a("plane", p.circlePlane);
        w.out << "/>\n";
        for (const auto &v : p.points) {
            w.out << "    <point";
            w.p(v.pose);
            w.v("in", v.inHandle);
            w.v("out", v.outHandle);
            w.a("hold", v.hold);
            w.a("seconds", v.seconds);
            w.a("stop", v.stop);
            w.out << "/>\n";
        }
        scalarTrack(w, "speed", p.speed);
        scalarTrack(w, "progress", p.progress);
        scalarTrack(w, "fov", p.fovs);
        for (const auto &v : p.rotations) {
            w.out << "    <rotation";
            w.a("t", v.time);
            w.q(v.value);
            w.out << "/>\n";
        }
        w.out << "  </path>\n";
    }
    w.out << "</cinematic>\n";
    std::string result = w.out.str();
    if (result.size() > 16 * 1024 * 1024)
        fail("Serialized collection exceeds 16 MiB; save individual paths");
    return result;
}
State Deserialize(const std::string &xml) {
    Node root = Reader(xml).read();
    State s;
    if (root.name == "campath") {
        Path p;
        p.id = 1;
        p.name = "Imported campath";
        p.curve = Curve::Linear;
        p.scheduled = true;
        p.holdEnd = boolean(root, "hold");
        std::vector<std::pair<double, Pose>> keys;
        for (auto &group : root.children) {
            if (group.name != "points")
                fail("Unexpected legacy campath element");
            for (auto &pt : group.children) {
                if (pt.name != "p")
                    fail("Unexpected legacy point element");
                keys.push_back({number(pt, "t", 0, true), pose(pt)});
            }
        }
        if (keys.empty())
            fail("Legacy campath has no points");
        std::sort(keys.begin(), keys.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        double first = keys.front().first, last = keys.back().first;
        p.start = first + number(root, "offset");
        p.duration = last > first ? last - first : 5;
        double traveled = 0;
        std::vector<double> distances{0};
        for (std::size_t i = 0; i < keys.size(); ++i) {
            Point pt;
            pt.pose = keys[i].second;
            p.points.push_back(pt);
            if (i) {
                if (keys[i].first <= keys[i - 1].first)
                    fail("Duplicate legacy key time");
                traveled += (keys[i].second.position - keys[i - 1].second.position).length();
                distances.push_back(traveled);
            }
        }
        if (keys.size() > 1)
            for (std::size_t i = 0; i < keys.size(); ++i) {
                double t = (keys[i].first - first) / p.duration;
                p.progress.push_back(
                    {t, traveled > 1e-9 ? distances[i] / traveled : double(i) / double(keys.size() - 1)});
                p.rotations.push_back({t, keys[i].second.rotation});
                p.fovs.push_back({t, keys[i].second.fov});
            }
        s.paths.push_back(std::move(p));
        s.selected = 0;
        Validate(s);
        return s;
    }
    if (root.name != "cinematic" || integer(root, "version", -1) != 1)
        fail("Unsupported cinematic XML version");
    s.selected = integer(root, "selected", -1);
    s.point = integer(root, "point", -1);
    for (auto &n : root.children) {
        if (n.name != "path")
            fail("Unexpected cinematic element");
        Path p;
        std::string id = attr(n, "id");
        try {
            std::size_t used = 0;
            p.id = std::stoull(id, &used);
            if (used != id.size() || id.empty() || id[0] == '-')
                fail("Invalid path ID");
        } catch (...) {
            fail("Invalid path ID");
        }
        p.name = attr(n, "name");
        p.curve = Curve(integer(n, "curve", 1));
        p.tension = number(n, "tension");
        p.duration = number(n, "duration", 5);
        p.start = number(n, "start");
        p.scheduled = boolean(n, "scheduled");
        p.holdEnd = boolean(n, "holdEnd");
        p.transition = number(n, "transition");
        p.ease = Ease(integer(n, "ease"));
        p.rotationInterp = Interp(integer(n, "rotationInterp"));
        p.fovInterp = Interp(integer(n, "fovInterp"));
        p.fixedAim = boolean(n, "fixedAim");
        p.aimTarget = vec(n, "aim");
        p.speed.clear();
        bool anchor = false, constraint = false, circle = false;
        for (auto &v : n.children) {
            if (!v.children.empty())
                fail("Unexpected nested path data");
            if (v.name == "anchor") {
                if (anchor)
                    fail("Duplicate anchor");
                anchor = true;
                p.anchor = {vec(v, ""), quat(v), number(v, "scale", 1)};
            } else if (v.name == "constraints") {
                if (constraint)
                    fail("Duplicate constraints");
                constraint = true;
                p.constraints.grid = number(v, "grid");
                p.constraints.locked = {{boolean(v, "lockX"), boolean(v, "lockY"), boolean(v, "lockZ")}};
                p.constraints.plane = vec(v, "");
            } else if (v.name == "circle") {
                if (circle)
                    fail("Duplicate circle");
                circle = true;
                p.circleCenter = vec(v, "");
                p.circleRadius = number(v, "radius", 128);
                p.circleStart = number(v, "start");
                p.circleSweep = number(v, "sweep", 360);
                p.circlePlane = integer(v, "plane");
            } else if (v.name == "point") {
                Point pt;
                pt.pose = pose(v);
                pt.inHandle = vec(v, "in");
                pt.outHandle = vec(v, "out");
                pt.hold = number(v, "hold");
                pt.seconds = number(v, "seconds");
                pt.stop = boolean(v, "stop");
                p.points.push_back(pt);
            } else if (v.name == "rotation")
                p.rotations.push_back({number(v, "t", 0, true), quat(v)});
            else if (v.name == "speed" || v.name == "progress" || v.name == "fov") {
                auto &keys = v.name == "speed" ? p.speed : v.name == "progress" ? p.progress : p.fovs;
                keys.push_back({number(v, "t", 0, true), number(v, "value", 0, true)});
            } else
                fail("Unknown path data element");
        }
        if (p.speed.empty())
            p.speed = {{0, 0}, {1, 1}};
        s.paths.push_back(std::move(p));
    }
    Validate(s);
    return s;
}
std::string Editor::legacyXml() const {
    const auto &p = active();
    if (p.points.empty())
        fail("Cannot export empty path");
    Writer w;
    w.out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<campath positionInterp=\"linear\" "
             "rotationInterp=\"slinear\" fovInterp=\"linear\"";
    w.a("hold", p.holdEnd);
    w.out << "><points>\n";
    // Explicit sampling bakes timing, placement and animation into legacy keys.
    int samples = (std::min)(20000, (std::max)(1, int(std::ceil(p.duration * 60))));
    for (int i = 0; i <= samples; ++i) {
        double t = p.duration * double(i) / samples;
        w.out << "  <p";
        w.a("t", p.start + t);
        w.p(p.evaluate(t));
        w.out << "/>\n";
    }
    w.out << "</points></campath>\n";
    return w.out.str();
}
} // namespace cinematic
