#if defined(AFX_CAMPATH_INPUT_TESTS)
#include "shared/MirvInput.h"
#include <iostream>
#include <stdexcept>
namespace
{
int inputChecks = 0;
void inputCheck(bool value, const char * message)
{
	++inputChecks;
	if (!value)
		throw std::runtime_error(message);
}
class InputDependencies : public IMirvInputDependencies
{
public:
	bool Suspended = false;
	bool GetSuspendMirvInput() override
	{
		return Suspended;
	}
	void GetLastCameraData(double & x, double & y, double & z, double & rx, double & ry, double & rz,
						   double & fov) override
	{
		x = y = z = rx = ry = rz = 0;
		fov = 90;
	}
	void GetGameCameraData(double & x, double & y, double & z, double & rx, double & ry, double & rz,
						   double & fov) override
	{
		GetLastCameraData(x, y, z, rx, ry, rz, fov);
	}
	double GetInverseScaledFov(double fov) override
	{
		return fov;
	}
};
bool allowUi(unsigned int key, void *)
{
	return key == VK_ESCAPE || key == VK_OEM_3 || key == VK_F2;
}
RAWINPUT keyboard(USHORT key, bool release = false)
{
	RAWINPUT packet = {};
	packet.header.dwType = RIM_TYPEKEYBOARD;
	packet.header.dwSize = sizeof(packet);
	packet.data.keyboard.VKey = key;
	packet.data.keyboard.MakeCode = 1;
	packet.data.keyboard.Message = release ? WM_KEYUP : WM_KEYDOWN;
	packet.data.keyboard.Flags = release ? RI_KEY_BREAK : 0;
	return packet;
}
void raw(MirvInput & input, RAWINPUT & packet)
{
	UINT bytes = sizeof(packet);
	input.Supply_RawInputData(bytes, (HRAWINPUT)1, RID_INPUT, &packet, &bytes, sizeof(RAWINPUTHEADER));
}
void inputTests()
{
	InputDependencies dependencies;
	MirvInput input(&dependencies);
	input.MenuAwareEscape_set(true);
	input.SetCameraControlMode(true);
	inputCheck(!input.Supply_KeyEvent(MirvInput::KS_DOWN, 'K', 1), "Ordinary freecam preserves unassigned keys");
	auto packet = keyboard('K');
	raw(input, packet);
	inputCheck(packet.data.keyboard.VKey == 'K', "Ordinary raw keyboard behavior unchanged");
	packet = keyboard('K', true);
	raw(input, packet);
	auto preEditor = keyboard('P');
	raw(input, preEditor);
	input.SetKeyboardCapture(true, allowUi);
	inputCheck(input.Supply_KeyEvent(MirvInput::KS_DOWN, 'K', 1), "Editor owns an unassigned key");
	inputCheck(input.Supply_KeyEvent(MirvInput::KS_UP, 'K', 1), "Editor owns its matching release");
	inputCheck(input.Supply_CharEvent(1, 1), "Editor owns control-character input");
	inputCheck(input.Supply_KeyEvent(MirvInput::KS_DOWN, 'W', 1), "Freecam still receives W");
	inputCheck(input.GetCamDForward() > 0, "W produces camera movement");
	double speed = input.GetCamDForward();
	inputCheck(input.Supply_CharEvent('+', 1), "Editor retains character-based speed controls");
	inputCheck(input.GetCamDForward() > speed, "Plus increases free-camera speed while captured");
	inputCheck(input.Supply_KeyEvent(MirvInput::KS_UP, 'W', 1), "W release is consumed");
	inputCheck(input.GetCamDForward() == 0, "W release clears movement");
	inputCheck(!input.Supply_KeyEvent(MirvInput::KS_DOWN, VK_OEM_3, 1), "Console key reaches Source");
	inputCheck(!input.Supply_KeyEvent(MirvInput::KS_UP, VK_OEM_3, 1), "Console release reaches Source");
	inputCheck(!input.Supply_KeyEvent(MirvInput::KS_DOWN, VK_F2, 1), "Configured UI key reaches Source");
	packet = keyboard('K');
	raw(input, packet);
	inputCheck(packet.data.keyboard.VKey == 0, "Raw gameplay bind blocked");
	packet = keyboard('K', true);
	raw(input, packet);
	inputCheck(packet.data.keyboard.VKey == 0, "Raw release blocked");
	packet = keyboard(VK_F2);
	raw(input, packet);
	inputCheck(packet.data.keyboard.VKey == VK_F2, "Raw UI key allowed");
	RAWINPUT packets[2] = {keyboard('J'), keyboard('L')};
	UINT bytes = sizeof(packets);
	input.Supply_RawInputBuffer(2, packets, &bytes, sizeof(RAWINPUTHEADER));
	inputCheck(packets[0].data.keyboard.VKey == 0 && packets[1].data.keyboard.VKey == 0,
			   "Buffered raw keyboard is captured");
	input.Supply_KeyEvent(MirvInput::KS_DOWN, 'B', 1);
	packet = keyboard('B');
	raw(input, packet);
	dependencies.Suspended = true;
	inputCheck(!input.Supply_KeyEvent(MirvInput::KS_DOWN, 'B', 1), "Menu can take ownership of an editor key");
	packet = keyboard('B');
	raw(input, packet);
	inputCheck(packet.data.keyboard.VKey == 'B', "Menu can take ownership of a raw editor key");
	inputCheck(!input.Supply_KeyEvent(MirvInput::KS_DOWN, 'K', 1), "Visible menu owns normal input");
	inputCheck(!input.Supply_CharEvent(1, 1), "Visible menu owns characters");
	packet = keyboard('K');
	raw(input, packet);
	inputCheck(packet.data.keyboard.VKey == 'K', "Visible menu owns raw input");
	dependencies.Suspended = false;
	inputCheck(!input.Supply_KeyEvent(MirvInput::KS_UP, 'B', 1), "Menu-owned release passes after UI closes");
	packet = keyboard('B', true);
	raw(input, packet);
	inputCheck(packet.data.keyboard.VKey == 'B', "Menu-owned raw release passes after UI closes");
	inputCheck(!input.Supply_KeyEvent(MirvInput::KS_UP, 'K', 1), "Release of menu-owned press is forwarded");
	input.Supply_KeyEvent(MirvInput::KS_DOWN, 'W', 1);
	input.Supply_KeyEvent(MirvInput::KS_UP, 'W', 1);
	inputCheck(input.GetCamDForward() == 0, "Camera movement releases after closing a menu");
	packet = keyboard('M', true);
	raw(input, packet);
	inputCheck(packet.data.keyboard.VKey == 'M', "Release of pre-editor raw press is forwarded");
	packet = keyboard('P');
	raw(input, packet);
	inputCheck(packet.data.keyboard.VKey == 0, "Pre-editor raw repeat is blocked");
	packet = keyboard('P', true);
	raw(input, packet);
	inputCheck(packet.data.keyboard.VKey == 'P', "Pre-editor repeat keeps its Source-owned release");
	inputCheck(input.Supply_KeyEvent(MirvInput::KS_DOWN, 'Q', (1LL << 30) | 1), "Pre-editor normal repeat is blocked");
	inputCheck(!input.Supply_KeyEvent(MirvInput::KS_UP, 'Q', 1), "Pre-editor normal release remains Source-owned");
	inputCheck(input.Supply_KeyEvent(MirvInput::KS_DOWN, 'W', (1LL << 30) | 1),
			   "Pre-editor movement repeat is captured");
	inputCheck(input.GetCamDForward() == 0, "Pre-editor repeat cannot start camera movement");
	inputCheck(!input.Supply_KeyEvent(MirvInput::KS_UP, 'W', 1), "Pre-editor movement release reaches Source");
	inputCheck(input.Supply_KeyEvent(MirvInput::KS_DOWN, 'N', 1), "Capture press before editor exits");
	packet = keyboard('N');
	raw(input, packet);
	input.SetKeyboardCapture(false);
	input.SetCameraControlMode(false);
	inputCheck(input.Supply_KeyEvent(MirvInput::KS_UP, 'N', 1), "Consume editor-owned release after exit");
	packet = keyboard('N', true);
	raw(input, packet);
	inputCheck(packet.data.keyboard.VKey == 0, "Consume raw release after exit");
	inputCheck(!input.Supply_KeyEvent(MirvInput::KS_DOWN, 'K', 1), "Keyboard returns to Source after exit");
	input.SetKeyboardCapture(true, allowUi);
	input.SetCameraControlMode(true);
	input.Supply_Focus(false);
	inputCheck(!input.Supply_KeyEvent(MirvInput::KS_DOWN, 'K', 1), "Focus loss yields keyboard");
	packet = keyboard('K');
	raw(input, packet);
	inputCheck(packet.data.keyboard.VKey == 'K', "Focus loss yields raw input");
	input.Supply_Focus(true);
	inputCheck(input.Supply_KeyEvent(MirvInput::KS_DOWN, 'K', 1), "Capture resumes on focus");
}
} // namespace
int main()
{
	try
	{
		inputTests();
		std::cout << inputChecks << " input checks passed\n";
	}
	catch (const std::exception & error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
#else
#include "shared/CinematicPath.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace cinematic;
namespace
{
int checks = 0;
void check(bool ok, const char * message)
{
	++checks;
	if (!ok)
		throw std::runtime_error(message);
}
void near(double a, double b, double tolerance = 1e-7)
{
	check(std::abs(a - b) <= tolerance, "Values differ");
}
void throws(const std::function<void()> & fn)
{
	bool caught = false;
	try
	{
		fn();
	}
	catch (const std::exception &)
	{
		caught = true;
	}
	check(caught, "Expected rejection");
}
Point pt(double x, double y = 0, double z = 0)
{
	Point p;
	p.pose.position = {x, y, z};
	return p;
}
Path line()
{
	Path p;
	p.id = 1;
	p.curve = Curve::Linear;
	p.duration = 10;
	p.points = {pt(0), pt(50), pt(100)};
	p.rebuild();
	return p;
}
State state(Path p)
{
	State s;
	s.paths.push_back(p);
	s.selected = 0;
	s.point = 0;
	Validate(s);
	return s;
}
void timeKeys()
{
	auto p = line();
	auto geometry = p.points;
	p.setTimeKey(10, 0);
	p.setTimeKey(20, 1);
	near(p.start, 10);
	near(p.duration, 10);
	check(p.scheduled && p.easeProgress, "Time-key scheduling enabled");
	p.setTimeKey(15, 0.2);
	near(p.evaluate(5).position.X, 20);
	near(p.evaluate(2.5).position.X, 10);
	near(p.timeKeys()[1].time, 15);
	check(p.points.size() == geometry.size(), "Timing capture does not add spatial points");
	p.setTimeKey(5, 0.5);
	near(p.start, 5);
	near(p.duration, 15);
	near(p.evaluate(0).position.X, 50);
	p.eraseTimeKey(0);
	near(p.start, 10);
	near(p.duration, 10);
	near(p.timeKeys()[1].time, 15);
	p.setTimeKey(12, 0.2);
	near(p.evaluate(3).position.X, 20);
	near(p.evaluate(4).position.X, 20);
	p.ease = Ease::In;
	near(p.evaluate(7.5).position.X, 40);
	near(p.pointTime(2), 10);
	p = line();
	p.setTimeKey(10, 0);
	p.setTimeKey(20, 1);
	p.ease = Ease::In;
	near(p.pointTime(1), std::sqrt(0.5) * 10);
	near(p.distanceAtTime(5), 0.25);
	for (int i = 0; i <= 20; ++i)
		near(p.distanceAtTime(i * 0.5) * 100, p.evaluate(i * 0.5).position.X);
	State state;
	state.paths = {p};
	state.paths[0].id = 1;
	state.selected = 0;
	auto xml = Serialize(state);
	auto loaded = Deserialize(xml);
	check(loaded.paths[0].easeProgress, "Time-key easing survives XML");
	near(loaded.paths[0].evaluate(5).position.X, 25);
	near(loaded.paths[0].timeKeys()[1].time, 20);
	std::string before = Serialize(state);
	throws([&] { state.paths[0].setTimeKey(90000, 0.5); });
	check(Serialize(state) == before, "Invalid capture preserves prior data");
	throws([&] { p.setTimeKey(-1, 0); });
	throws([&] { p.setTimeKey(11, 2); });
	throws([&] { p.eraseTimeKey(2); });
	p.setTimeKey(15, 0.3);
	p.setTimeKey(15, 0.4);
	check(p.progress.size() == 3, "Same-time capture replaces instead of duplicating");
	near(p.evaluate(5).position.X, 40);
	p.eraseTimeKey(1);
	p.eraseTimeKey(1);
	near(p.evaluate(4).position.X, 0);
	p.eraseTimeKey(0);
	check(p.progress.empty() && !p.scheduled && !p.easeProgress, "Deleting all timing keys unschedules movement");
	Path empty;
	throws([&] { empty.setTimeKey(10, 0); });
	p = line();
	p.progress = {{0, 0}, {1, 1}};
	p.ease = Ease::In;
	p.rebuild();
	near(p.evaluate(5).position.X, 50);
	check(!p.easeProgress, "Old motion tracks retain linear interpolation");
	p = line();
	p.points[1].hold = 1;
	p.rebuild();
	p.setTimeKey(10, 0);
	p.setTimeKey(20, 1);
	near(p.points[1].hold, 0);
	near(p.evaluate(5).position.X, 50);
	p = line();
	p.setTimeKey(10, 0);
	p.setTimeKey(12, 0.2);
	p.setTimeKey(20, 1);
	p.transition = 5;
	p.replaceTimeKey(2, 22, 1);
	near(p.duration, 12);
	near(p.transition, 5);
	auto unchanged = p.timeKeys();
	throws([&] { p.replaceTimeKey(2, 12, 0.8); });
	near(p.timeKeys().back().time, unchanged.back().time);
	throws([&] { p.replaceTimeKey(9, 22, 1); });
	throws([&] { p.replaceTimeKey(2, 22, -1); });
	p = line();
	p.ease = Ease::Smooth;
	p.points[1].hold = 2;
	p.rebuild();
	for (double f : {0.1, 0.3, 0.5, 0.9})
		near(p.distanceAtTime(p.timeAtDistance(f)), f);
	p = line();
	p.setTimeKey(10, 0.2);
	p.setTimeKey(20, 0.8);
	p.ease = Ease::Smooth;
	for (double f : {0.2, 0.3, 0.5, 0.8})
		near(p.distanceAtTime(p.timeAtDistance(f)), f);
	throws([&] { p.timeAtDistance(1); });
	throws([&] { p.timeAtDistance(-1); });
	p = line();
	p.setTimeKey(10, 0);
	p.setTimeKey(20, 1);

	Editor editor;
	editor.create();
	auto next = editor.state;
	next.paths[0] = p;
	next.paths[0].id = 1;
	editor.commit(next);
	next = editor.state;
	next.paths[0].setTimeKey(15, 0.2);
	editor.commit(next);
	check(editor.undo(), "Time-key capture is undoable");
	near(editor.active().evaluate(5).position.X, 50);
	check(editor.redo(), "Time-key capture can redo");
	near(editor.active().evaluate(5).position.X, 20);
}
void timing()
{
	auto p = line();
	near(p.Length(), 100);
	near(p.evaluate(5).position.X, 50);
	p.points[1].hold = 2;
	p.rebuild();
	auto ts = p.timeline();
	near(ts[1].arrival, 4);
	near(ts[1].departure, 6);
	near(ts[2].arrival, 10);
	near(p.evaluate(4).position.X, 50);
	near(p.evaluate(5).position.X, 50);
	near(p.evaluate(6).position.X, 50);
	near(p.evaluate(10).position.X, 100);
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
	check(std::abs(p.evaluate(5 - epsilon).position.X - 50) / epsilon < 0.01, "Stop incoming velocity");
	check(std::abs(p.evaluate(5 + epsilon).position.X - 50) / epsilon < 0.01, "Stop outgoing velocity");
	p = line();
	p.progress = {{0, 0}, {0.5, 1}, {1, 0}};
	p.rebuild();
	near(p.evaluate(5).position.X, 100);
	near(p.evaluate(10).position.X, 0);
	p.points[0].hold = 1;
	throws([&] { p.rebuild(); });
	p = line();
	p.ease = Ease::Custom;
	p.speed = {{0, 0}, {0.5, 0.25}, {1, 1}};
	p.rebuild();
	near(p.evaluate(2.5).position.X, 12.5);
	p.speed = {{0, 0}, {0.5, 0.9}, {0.7, 0.3}, {1, 1}};
	throws([&] { p.rebuild(); });
	p = line();
	p.points = {pt(0)};
	p.points[0].hold = 2;
	p.rebuild();
	near(p.evaluate(9).position.X, 0);
	p.rotations = {{0, Euler(0, 0, 0)}, {1, Euler(0, 90, 0)}};
	p.fovs = {{0, 90}, {1, 50}};
	p.rebuild();
	near(Angles(p.evaluate(5).rotation).Y, 45);
	near(p.evaluate(5).fov, 70);
	p = line();
	p.points = {pt(0), pt(0)};
	p.points[1].pose.rotation = Euler(0, 90, 0);
	p.rebuild();
	near(Angles(p.evaluate(5).rotation).Y, 45);

	// A zero-length segment within a moving path can still animate a turn.
	p = line();
	p.points = {pt(0), pt(0), pt(100)};
	p.points[0].seconds = 2;
	p.points[1].pose.rotation = Euler(0, 90, 0);
	p.rebuild();
	near(p.evaluate(1).position.X, 0);
	near(Angles(p.evaluate(1).rotation).Y, 45);
	p = line();
	p.progress = {{0, 0}, {0.5, 1}, {1, 0}};
	p.rebuild();
	near(p.pointTime(1), 2.5);
}
void geometry()
{
	{
		auto p = line();
		p.points = {pt(0), pt(100), pt(100)};
		p.points.back().pose.rotation = Euler(0, 90, 0);
		p.rebuild();
		near(Angles(p.atDistance(1).rotation).Y, 90);
		near(Angles(p.evaluate(10).rotation).Y, 90);
	}

	auto p = line();
	p.curve = Curve::Smooth;
	p.points = {pt(0), pt(30, 50), pt(100, 10), pt(160)};
	p.rebuild();
	near((p.position(1) - p.points[1].pose.position).Length(), 0);
	// Within a curved segment constant time increments should cover similar
	// distance: arc-length mapping, rather than raw spline parameterization.
	std::vector<double> d;
	for (int i = 0; i < 100; ++i)
		d.push_back((p.evaluate((i + 1) * 0.1).position - p.evaluate(i * 0.1).position).Length());
	auto range = std::minmax_element(d.begin(), d.end());
	check(*range.second / *range.first < 1.06, "Constant speed on spline");
	p = line();
	p.curve = Curve::Bezier;
	p.points = {pt(0), pt(100)};
	p.points[0].outHandle = {0, 100, 0};
	p.points[1].inHandle = {0, 100, 0};
	p.rebuild();
	near(p.position(0.5).X, 50);
	near(p.position(0.5).Y, 75);
	p = line();
	p.curve = Curve::Circle;
	p.circleRadius = 100;
	p.circleSweep = 180;
	p.circleCenter = {10, 20, 30};
	p.rebuild();
	near(p.Length(), 100 * Pi);
	for (int i = 0; i < 20; ++i)
		near((p.evaluate(i * 0.5).position - p.circleCenter).Length(), 100);
	p = line();
	p.anchor = {{500, 200, 10}, Euler(0, 90, 0), 2};
	p.rebuild();
	Pose world = p.evaluate(10);
	near(world.position.X, 500);
	near(world.position.Y, 400);
	near(Angles(world.rotation).Y, 90);
	near(p.Length(), 200);
	near(p.points.back().pose.position.X, 100);
	Pose pose{{20, 30, 40}, Euler(20, -40, 15), 80};
	auto round = p.toWorld(p.toLocal(pose));
	near((round.position - pose.position).Length(), 0);
	near(std::abs(round.rotation.W * pose.rotation.W + round.rotation.X * pose.rotation.X +
				  round.rotation.Y * pose.rotation.Y + round.rotation.Z * pose.rotation.Z),
		 1);
	p = line();
	p.constraints.grid = 8;
	p.constraints.locked[2] = true;
	p.constraints.plane.Z = 13;
	p.capture({{11, 19, 27}, {}, 90});
	near(p.points.back().pose.position.X, 8);
	near(p.points.back().pose.position.Y, 16);
	near(p.points.back().pose.position.Z, 13);
	p = line();
	p.fixedAim = true;
	p.aimTarget = {0, 100, 0};
	p.rebuild();
	near(Angles(p.evaluate(0).rotation).Y, 90);
	auto a = Euler(0, 179, 0), b = Euler(0, -179, 0);
	near(std::abs(Angles(Slerp(a, b, 0.5)).Y), 180);
	for (int pitch : {-90, -30, 0, 45, 90})
		for (int yaw : {-170, -30, 80})
			for (int roll : {-45, 0, 60})
			{
				Quat q = Euler(pitch, yaw, roll);
				Vec e = Angles(q);
				Quat r = Euler(e.X, e.Y, e.Z);
				near(std::abs(q.W * r.W + q.X * r.X + q.Y * r.Y + q.Z * r.Z), 1, 1e-6);
			}
}
void persistence()
{
	// Check all geometry/easing combinations under direct, nonsequential seeks.
	for (Curve curve : {Curve::Linear, Curve::Smooth, Curve::Bezier, Curve::Circle})
		for (Ease ease : {Ease::Linear, Ease::In, Ease::Out, Ease::Smooth, Ease::Smoother, Ease::Custom})
		{
			auto p = line();
			p.curve = curve;
			p.ease = ease;
			p.points[0].outHandle = {0, 20, 0};
			p.points[1].inHandle = {0, 20, 0};
			p.rebuild();
			for (double t : {0.0, 8.8, 2.2, 10.0, 1.0, 8.8, 0.0})
			{
				Pose a = p.evaluate(t);
				p.evaluate(10 - t);
				Pose b = p.evaluate(t);
				near((a.position - b.position).Length(), 0);
				near(a.fov, b.fov);
			}
		}

	auto p = line();
	p.name = "A & B <camera> \"test\"";
	p.anchor = {{1, 2, 3}, Euler(20, 50, 10), 2};
	p.points[1].hold = 2;
	p.points[1].stop = true;
	p.points[0].outHandle = {5, 6, 7};
	p.ease = Ease::Custom;
	p.constraints.grid = 16;
	p.constraints.locked[2] = true;
	p.constraints.plane.Z = 8;
	p.rotations = {{0, {}}, {1, Euler(0, 90, 0)}};
	p.fovs = {{0, 80}, {1, 100}};
	p.scheduled = true;
	p.transition = 1;
	p.rebuild();
	State s = state(p);
	auto text = Serialize(s);
	auto loaded = Deserialize(text);
	check(loaded.paths[0].name == p.name, "Escaped name roundtrip");
	for (int i = 0; i <= 100; ++i)
	{
		auto a = p.evaluate(i * 0.1), b = loaded.paths[0].evaluate(i * 0.1);
		near((a.position - b.position).Length(), 0);
		near(a.fov, b.fov);
		near(std::abs(a.rotation.W * b.rotation.W + a.rotation.X * b.rotation.X + a.rotation.Y * b.rotation.Y +
					  a.rotation.Z * b.rotation.Z),
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
	for (const std::string bad :
		 {"<cinematic version=\"99\"/>", "<!DOCTYPE x><cinematic version=\"1\"/>",
		  "<cinematic version=\"1\"><path id=\"1\" name=\"&bad;\"/></cinematic>",
		  "<cinematic version=\"1\" version=\"1\"/>", "<cinematic version=\"1\"></wrong>",
		  "<cinematic version=\"1\"/>garbage", "<cinematic version=\"1\"><path id=\"1\" duration=\"-1\"/></cinematic>"})
		throws([&] { Deserialize(bad); });
	Editor classic;
	classic.loadXml("<campath offset=\"2\"><points><p t=\"10\" x=\"0\" y=\"0\" z=\"0\" rx=\"0\" ry=\"0\" rz=\"0\"/><p "
					"t=\"14\" x=\"40\" y=\"0\" z=\"0\" rx=\"0\" ry=\"0\" rz=\"90\"/></points></campath>",
					true);
	near(classic.active().start, 12);
	near(classic.active().duration, 4);
	near(classic.active().evaluate(2).position.X, 20);
	near(Angles(classic.active().evaluate(2).rotation).Y, 45);
	Editor exported;
	exported.loadXml(classic.legacyXml(), true);
	near(exported.active().evaluate(2).position.X, 20);
	near(exported.active().start, 12);
	s.paths[0].duration = std::numeric_limits<double>::infinity();
	throws([&] { Validate(s); });
}
void compatibility()
{
	// Files written by the first implementation remain readable after moving
	// persistence onto HLAE's existing RapidXML dependency.
	const std::string original =
		"<cinematic version=\"1\" selected=\"0\" point=\"1\">"
		"<path id=\"42\" name=\"Saved &amp; reused\" duration=\"10\" curve=\"0\" start=\"20\" scheduled=\"1\">"
		"<anchor x=\"100\" y=\"0\" z=\"0\" qw=\"1\" scale=\"2\"/>"
		"<point x=\"0\" y=\"0\" z=\"0\" qw=\"1\" fov=\"90\"/>"
		"<point x=\"50\" y=\"0\" z=\"0\" qw=\"1\" fov=\"60\"/>"
		"</path></cinematic>";
	auto loaded = Deserialize(original);
	check(loaded.paths[0].id == 42, "Stable saved ID");
	check(loaded.paths[0].name == "Saved & reused", "Escaped name compatibility");
	near(loaded.paths[0].evaluate(5).position.X, 150);
	near(loaded.paths[0].evaluate(5).fov, 75);
	auto again = Deserialize(Serialize(loaded));
	near(again.paths[0].evaluate(10).position.X, 200);
	auto numeric = Deserialize("<cinematic version=\"1\"><path id=\"1\" name=\"A &#38; B\"/></cinematic>");
	check(numeric.paths[0].name == "A & B", "Standard XML character reference");
	for (const char * value : {"&unknown;", "&#0;", "&#xD800;", "&#1114112;", "&#;", "&"})
	{
		std::string bad = "<cinematic version=\"1\"><path id=\"1\" name=\"" + std::string(value) + "\"/></cinematic>";
		throws([&] { Deserialize(bad); });
	}
	throws([&] { Deserialize("<cinematic version=\"1\">unexpected text</cinematic>"); });
	throws([&] { Deserialize("<cinematic version=\"1\"><path id=\"1\"/><!DOCTYPE bad></cinematic>"); });
	std::string deep;
	for (int i = 0; i < 10000; ++i)
		deep += "<x>";
	for (int i = 0; i < 10000; ++i)
		deep += "</x>";
	throws([&] { Deserialize(deep); });
	Path defaults;
	defaults.id = 1;
	defaults.points.push_back(Point{});
	defaults.rebuild();
	near(defaults.evaluate(0).position.Length(), 0);
	near(defaults.evaluate(2).fov, 90);
}
void schedule()
{
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
	near(p.position.X, 50);
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
	near(p.position.X, 1010);
	auto exact = p;
	check(e.evaluate(12, p), "Backward seek");
	near(p.position.X, 20);
	e.evaluate(19, p);
	near(p.position.X, exact.position.X);
	n = e.state;
	n.paths[1].transition = 2;
	e.commit(n);
	e.evaluate(18, p);
	near(p.position.X, 80);
	e.evaluate(19, p);
	near(p.position.X, 550);
	e.evaluate(20, p);
	near(p.position.X, 1020);
	n = e.state;
	n.paths[1].holdEnd = true;
	e.commit(n);
	e.evaluate(1000, p);
	near(p.position.X, 1100);
	n = e.state;
	n.paths[1].start = 10;
	n.paths[1].transition = 0;
	e.commit(n);
	e.evaluate(11, p);
	near(p.position.X, 1010);
}
} // namespace
int main()
{
	try
	{
		timeKeys();
		timing();
		geometry();
		persistence();
		schedule();
		compatibility();
		std::cout << checks << " checks passed\n";
	}
	catch (const std::exception & e)
	{
		std::cerr << "After " << checks << " checks: " << e.what() << '\n';
		return 1;
	}
}

#endif
