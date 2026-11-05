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
struct InputPolicy
{
	MirvInput * Input;
	bool AllowUnused = false;
};
bool allowUnused(unsigned int key, void * context)
{
	auto & policy = *static_cast<InputPolicy *>(context);
	return allowUi(key, nullptr) || (policy.AllowUnused && key != VK_INSERT && !policy.Input->IsCameraKey(key));
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
	input.Supply_KeyEvent(MirvInput::KS_UP, 'K', 1);
	InputPolicy policy{&input};
	input.SetKeyboardCapture(true, allowUnused, &policy);
	inputCheck(input.IsCameraKey('W') && !input.IsCameraKey('K'), "Camera ownership uses the native key dispatcher");
	inputCheck(input.GetCamDForward() == 0, "Querying key ownership cannot move the camera");
	input.Supply_KeyEvent(MirvInput::KS_DOWN, 'Q', 1);
	packet = keyboard('Q');
	raw(input, packet);
	policy.AllowUnused = true;
	inputCheck(input.Supply_KeyEvent(MirvInput::KS_DOWN, 'Q', (1LL << 30) | 1),
			   "A captured normal press stays owned when passthrough changes");
	packet = keyboard('Q');
	raw(input, packet);
	inputCheck(packet.data.keyboard.VKey == 0, "A captured raw press stays owned when passthrough changes");
	inputCheck(input.Supply_KeyEvent(MirvInput::KS_UP, 'Q', 1), "Captured normal release stays owned");
	packet = keyboard('Q', true);
	raw(input, packet);
	inputCheck(packet.data.keyboard.VKey == 0, "Captured raw release stays owned");
	inputCheck(!input.Supply_KeyEvent(MirvInput::KS_DOWN, 'K', 1), "Unused key reaches Source in permissive mode");
	inputCheck(!input.Supply_KeyEvent(MirvInput::KS_UP, 'K', 1), "Unused release reaches Source");
	packet = keyboard('K');
	raw(input, packet);
	inputCheck(packet.data.keyboard.VKey == 'K', "Unused raw key reaches Source");
	inputCheck(input.Supply_KeyEvent(MirvInput::KS_DOWN, VK_INSERT, 1), "A preview action remains captured");
	inputCheck(input.Supply_KeyEvent(MirvInput::KS_UP, VK_INSERT, 1), "A preview action release remains captured");
	inputCheck(input.Supply_KeyEvent(MirvInput::KS_DOWN, 'W', 1) && input.GetCamDForward() > 0,
			   "Native camera movement remains captured and usable");
	inputCheck(input.Supply_KeyEvent(MirvInput::KS_UP, 'W', 1) && input.GetCamDForward() == 0,
			   "Native camera release clears movement");
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
#include "shared/CamPath.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
namespace
{
int checks = 0;
void check(bool ok, const char * message)
{
	++checks;
	if (!ok)
		throw std::runtime_error(message);
}
void near(double a, double b, double tolerance = 1e-6)
{
	check(std::abs(a - b) <= tolerance, "Values differ");
}
void populate(CamPath & path, bool linear = true)
{
	if (linear)
	{
		path.PositionInterpMethod_set(CamPath::DI_LINEAR);
		path.RotationInterpMethod_set(CamPath::QI_SLINEAR);
		path.FovInterpMethod_set(CamPath::DI_LINEAR);
	}
	path.Add(10, CamPathValue(0, 0, 0, 0, 0, 0, 90));
	path.Add(20, CamPathValue(100, 0, 0, 0, 90, 0, 60));
}
void captureAndTiming()
{
	CamPath path;
	populate(path);
	check(path.CanEval(), "Native linear interpolation is available");
	path.Add(15, CamPathValue(30, 0, 0, 0, 45, 0, 80));
	check(path.GetSize() == 3, "Capturing between demo times inserts a native keyframe");
	auto it = path.GetBegin();
	near(it.GetTime(), 10);
	++it;
	near(it.GetTime(), 15);
	near(it.GetValue().X, 30);
	++it;
	near(it.GetTime(), 20);
	path.Add(15, CamPathValue(40, 0, 0, 0, 45, 0, 80));
	check(path.GetSize() == 3, "Same-time native capture replaces a point");
	near(path.Eval(15).X, 40);
	path.Enabled_set(true);
	path.SetOffset(3);
	path.SetHold(true);
	check(path.Enabled_get() && path.GetHold(), "Native enable and hold retained");
	near(path.GetOffset(), 3);
	path.CurveEditing_set(true);
	path.SetHandle(10, true, Vector3(0, 100, 0));
	path.SetHandle(15, false, Vector3(0, 100, 0));
	path.SetStart(25);
	near(path.GetLowerBound(), 25);
	near(path.GetUpperBound(), 35);
	Vector3 handle;
	check(path.GetHandle(25, true, handle), "Handles follow native retiming");
	near(handle.Y, 100);
	path.SetDuration(20);
	near(path.GetUpperBound(), 45);
	near(path.Eval(30).Y, 75);
	path.SelectNone();
	path.SelectAdd(std::size_t(1), std::size_t(1));
	path.SetStart(40);
	it = path.GetBegin();
	++it;
	near(it.GetTime(), 40);
	near(it.GetValue().InHandle.Y, 100);
	path.Remove(40);
	check(path.GetSize() == 2, "Native point removal removes its handles too");
}
void curves()
{
	CamPath classic, curved;
	populate(classic);
	populate(curved);
	curved.CurveEditing_set(true);
	for (int i = 0; i <= 40; ++i)
	{
		auto a = classic.Eval(10 + i * 0.25), b = curved.Eval(10 + i * 0.25);
		near(a.X, b.X);
		near(a.Y, b.Y);
		near(a.Fov, b.Fov);
	}
	check(!classic.SetHandle(10, true, Vector3(0, 100, 0)), "Classic system does not accept curve edits");
	check(curved.SetHandle(10, true, Vector3(0, 100, 0)), "Outgoing handle set");
	check(curved.SetHandle(20, false, Vector3(0, 100, 0)), "Incoming handle set");
	near(curved.Eval(15).Y, 75);
	near(curved.Eval(15).X, 50);
	near(curved.Eval(15).Fov, classic.Eval(15).Fov);
	auto a = classic.Eval(15).R, b = curved.Eval(15).R;
	near(std::abs(DotProduct(a, b)), 1);
	near(curved.GetLowerBound(), 10);
	near(curved.GetUpperBound(), 20);
	near(curved.Eval(10).Y, 0);
	near(curved.Eval(20).Y, 0);
	curved.CurveEditing_set(false);
	near(curved.Eval(15).Y, 0);
	curved.CurveEditing_set(true);
	near(curved.Eval(15).Y, 75);
	curved.Bezier_set(false);
	near(curved.Eval(15).Y, 0);
	curved.Bezier_set(true);
	check(!curved.SetHandle(9, true, Vector3(1, 0, 0)), "Missing key rejected");
	check(!curved.SetHandle(10, true, Vector3(std::numeric_limits<double>::infinity(), 0, 0)),
		  "Nonfinite handle rejected");
	check(!curved.SetHandle(10, true, Vector3(1e10, 0, 0)), "Oversized handle rejected");
	near(curved.Eval(15).Y, 75);
	curved.ResetHandles(10);
	curved.ResetHandles(20);
	near(curved.Eval(15).Y, 0);
	Vector3 offset;
	curved.GetHandle(10, true, offset);
	near(offset.X, 100.0 / 3, 1e-5);
	near(offset.Y, 0);
	// Untouched segments use the exact original cubic interpolator.
	CamPath native, cubic;
	for (int i = 0; i < 4; ++i)
	{
		CamPathValue value(i * 20, (i % 2) * 30, 0, 0, i * 15, 0, 70 + i);
		native.Add(10 + i * 5, value);
		cubic.Add(10 + i * 5, value);
	}
	cubic.CurveEditing_set(true);
	cubic.Bezier_set(true);
	check(native.CanEval() && cubic.CanEval(), "Original cubic interpolation requirements retained");
	for (int i = 0; i <= 60; ++i)
	{
		auto n = native.Eval(10 + i * 0.25), c = cubic.Eval(10 + i * 0.25);
		near(n.X, c.X);
		near(n.Y, c.Y);
		near(n.Fov, c.Fov);
	}
	cubic.SetHandle(10, true, Vector3(0, 100, 0));
	for (int i = 0; i <= 20; ++i)
		near(native.Eval(20 + i * 0.25).Y, cubic.Eval(20 + i * 0.25).Y);
}
void placement()
{
	CamPath path;
	populate(path);
	path.CurveEditing_set(true);
	path.SetHandle(10, true, Vector3(0, 100, 0));
	path.SetHandle(20, false, Vector3(0, 100, 0));
	path.SetPosition(150, 100, 0);
	near(path.Eval(15).X, 150);
	near(path.Eval(15).Y, 175);
	path.Rotate(0, 90, 0);
	near(path.Eval(15).X, 75);
	near(path.Eval(15).Y, 100);
	Vector3 offset;
	path.GetHandle(10, true, offset);
	near(offset.X, -100);
	near(offset.Y, 0);
	path.AnchorTransform(150, 100, 0, 0, 90, 0, 0, 0, 0, 0, 0, 0);
	near(path.Eval(15).X, 0);
	near(path.Eval(15).Y, 75);
	path.GetHandle(10, true, offset);
	near(offset.X, 0);
	near(offset.Y, 100);
}
void persistence()
{
	auto directory = std::filesystem::temp_directory_path() / "afx-campath-curve-tests";
	std::filesystem::create_directories(directory);
	auto file = directory / "curve.xml", old = directory / "native.xml", invalid = directory / "retired.xml";
	CamPath path;
	populate(path);
	path.CurveEditing_set(true);
	path.SetOffset(2);
	path.SetHold(true);
	path.SelectAll();
	path.SetHandle(10, true, Vector3(0, 100, 0));
	path.SetHandle(20, false, Vector3(0, 100, 0));
	check(path.Save(file.wstring().c_str()), "Extended native XML saves");
	CamPath loaded;
	loaded.CurveEditing_set(true);
	check(loaded.Load(file.wstring().c_str()), "Extended native XML loads");
	check(loaded.Bezier_get() && loaded.CurveEditing_get() && loaded.GetHold(),
		  "Curve mode, system gate and native hold retained");
	near(loaded.GetOffset(), 2);
	near(loaded.Eval(15).Y, 75);
	check(loaded.GetBegin().GetValue().Selected, "Native selection persisted");
	CamPath classic;
	check(classic.Load(file.wstring().c_str()), "Classic can read the optional extensions");
	near(classic.Eval(15).Y, 0);
	check(classic.Save(file.wstring().c_str()), "Classic save retains optional handle data");
	check(loaded.Load(file.wstring().c_str()), "Curve data survives classic round trip");
	near(loaded.Eval(15).Y, 75);
	CamPath original;
	populate(original);
	check(original.Save(old.wstring().c_str()), "Unextended classic file saves");
	check(loaded.Load(old.wstring().c_str()), "Unextended classic file loads in new system");
	check(!loaded.Bezier_get(), "Unextended file restores original interpolation");
	near(loaded.Eval(15).Y, 0);
	{
		std::ofstream out(invalid);
		out << "<cinematicCamera version=\"1\"/>";
	}
	check(!loaded.Load(invalid.wstring().c_str()), "Retired schema rejected clearly");
	check(loaded.GetSize() == 2, "Rejected schema leaves the existing path intact");
	{
		std::ofstream out(invalid);
		out << "<campath curve=\"bezier\"><points><p t=\"1\" outX=\"nan\" outY=\"0\" outZ=\"0\"/></points></campath>";
	}
	check(!loaded.Load(invalid.wstring().c_str()), "Invalid handle data rejected");
	check(loaded.GetSize() == 2 && !loaded.Bezier_get(), "Invalid handle load restores native path and curve mode");
	near(loaded.Eval(15).X, 50);
	{
		std::ofstream out(invalid);
		out << "<campath><points><p t=\"1\" inX=\"3\"/></points></campath>";
	}
	check(!loaded.Load(invalid.wstring().c_str()), "Incomplete handle vector rejected");
	check(loaded.GetSize() == 2, "Partial handle load preserves path");
	{std::ofstream out(invalid);out<<"<campath/>";}
	check(!loaded.Load(invalid.wstring().c_str()),"Missing keyframe container rejected");check(loaded.GetSize()==2,"Missing container preserves native keys");

	std::filesystem::remove(file);
	std::filesystem::remove(old);
	std::filesystem::remove(invalid);
	std::filesystem::remove(directory);
}

template <class F> void rejected(F operation, const char * message)
{
	bool failed = false;
	try
	{
		operation();
	}
	catch (const std::invalid_argument &)
	{
		failed = true;
	}
	check(failed, message);
}

void localInterpolation()
{
	CamPath path;
	populate(path);
	for (auto mode : {CamPath::DI_SMOOTH, CamPath::DI_MONOTONE, CamPath::DI_STEP})
	{
		path.PositionInterpMethod_set(mode);
		path.FovInterpMethod_set(mode);
		path.RotationInterpMethod_set(mode == CamPath::DI_STEP ? CamPath::QI_STEP : CamPath::QI_SMOOTH);
		check(path.CanEval(), "Local interpolation works with two keyframes");
		near(path.Eval(10).X, 0);
		near(path.Eval(20).X, 100);
		near(path.Eval(15).X, mode == CamPath::DI_STEP ? 0 : 50);
		near(path.Eval(15).Fov, mode == CamPath::DI_STEP ? 90 : 75);
		near(path.Eval(15).R.ToQREulerAngles().ToQEulerAngles().Yaw, mode == CamPath::DI_STEP ? 0 : 45);
	}
	path.Remove(20);
	check(path.CanEval(), "Stepped interpolation supports a single stationary keyframe");
	near(path.Eval(10).X, 0);

	CamPath monotone;
	monotone.PositionInterpMethod_set(CamPath::DI_MONOTONE);
	monotone.FovInterpMethod_set(CamPath::DI_MONOTONE);
	monotone.RotationInterpMethod_set(CamPath::QI_SLINEAR);
	const double times[] = {0, 0.1, 4, 5, 20};
	const double values[] = {0, 10, 10, 9, 100};
	for (int i = 0; i < 5; ++i)
		monotone.Add(times[i], CamPathValue(values[i], 0, 0, 0, 0, 0, 60 + values[i]));
	for (int segment = 0; segment < 4; ++segment)
		for (int i = 0; i <= 100; ++i)
		{
			double t = times[segment] + (times[segment + 1] - times[segment]) * i / 100.0;
			auto value = monotone.Eval(t);
			double lower = (std::min)(values[segment], values[segment + 1]);
			double upper = (std::max)(values[segment], values[segment + 1]);
			check(value.X >= lower - 1e-6 && value.X <= upper + 1e-6, "Monotone positions cannot overshoot");
			check(value.Fov >= 60 + lower - 1e-6 && value.Fov <= 60 + upper + 1e-6, "Monotone FOV cannot overshoot");
		}
	CamPath restored;
	check(restored.LoadXml(monotone.ToXml()), "New interpolation settings survive native XML");
	check(restored.PositionInterpMethod_get() == CamPath::DI_MONOTONE, "Position mode restored");
	near(restored.Eval(4.5).X, monotone.Eval(4.5).X);
}

void keyframeHolds()
{
	CamPath path;
	populate(path);
	check(path.SetKeyHold(10, 14), "Full-pose hold accepted");
	for (double t : {10.0, 12.0, 14.0})
	{
		auto value = path.Eval(t);
		near(value.X, 0);
		near(value.Fov, 90);
		near(std::abs(DotProduct(value.R, path.GetBegin().GetValue().R)), 1);
	}
	near(path.Eval(17).X, 50);
	near(path.Eval(17).Fov, 75);
	near(path.Eval(17).R.ToQREulerAngles().ToQEulerAngles().Yaw, 45);
	near(path.Eval(20).X, 100);
	auto before = path.CaptureState();
	check(!path.SetKeyHold(10, 20), "Release at the next arrival leaves no travel time and is rejected");
	check(!path.SetKeyHold(10, 21), "Overlapping release rejected");
	check(!path.SetKeyHold(10, 9), "Release before arrival rejected");
	check(!path.SetKeyHold(10, std::numeric_limits<double>::infinity()), "Nonfinite release rejected");
	check(!path.SetKeyHold(11, 12), "Missing held key rejected");
	rejected([&] { path.Add(12, CamPathValue()); }, "New arrival inside an existing hold rejected");
	check(path.CaptureState() == before, "Rejected hold/insertion is atomic");
	path.SelectNone();
	path.SelectAdd(std::size_t(0), std::size_t(0));
	auto selected = path.CaptureState();
	rejected([&] { path.SetStart(18); }, "Retiming cannot move a hold into the next arrival");
	rejected([&] { path.SetDuration(20); }, "Scaling one hold cannot overlap an unselected arrival");
	check(path.CaptureState() == selected, "Rejected timing edits preserve the complete path");
	path.SelectNone();

	path.SetStart(30);
	near(path.GetBegin().GetValue().HoldDuration, 4);
	near(path.Eval(34).X, 0);
	path.SetDuration(20);
	near(path.GetBegin().GetValue().HoldDuration, 8);
	near(path.GetUpperBound(), 50);
	near(path.Eval(44).X, 50);
	check(path.SetKeyHold(50, 55), "Final keyframe can extend the path with a hold");
	near(path.GetUpperBound(), 55);
	near(path.GetDuration(), 25);
	near(path.Eval(55).X, 100);
	near(path.Eval(55).Fov, 60);
	path.SelectAll();
	path.SetDuration(50);
	near(path.GetUpperBound(), 80);
	near(path.GetBegin().GetValue().HoldDuration, 16);

	CamPath single;
	single.Add(3, CamPathValue(10, 20, 30, 15, 25, 35, 80));
	check(single.SetKeyHold(3, 8) && single.CanEval(), "A single held keyframe is a stationary shot");
	near(single.Eval(8).X, 10);
	single.SetDuration(10);
	near(single.GetUpperBound(), 13);
	check(single.SetKeyHold(3, 3), "A release equal to arrival removes the hold");
	check(!single.CanEval(), "Native cubic requirements return when the single hold is cleared");

	CamPath curve;
	populate(curve);
	curve.CurveEditing_set(true);
	curve.SetHandle(10, true, Vector3(0, 100, 0));
	curve.SetHandle(20, false, Vector3(0, 100, 0));
	curve.SetKeyHold(10, 14);
	near(curve.Eval(13).Y, 0);
	check(curve.Eval(17).Y > 0 && curve.Eval(17).Y < 75, "A curved hold accelerates smoothly after release");
	CamPath loaded;
	loaded.CurveEditing_set(true);
	check(loaded.LoadXml(curve.ToXml()), "Curves and holds share native XML");
	near(loaded.Eval(13).Y, 0);
	near(loaded.Eval(17).Y, curve.Eval(17).Y);
	auto valid = loaded.CaptureState();
	check(!loaded.LoadXml("<campath><points><p t=\"10\" holdDuration=\"10\"/><p t=\"20\"/></points></campath>"),
		  "Invalid XML overlap rejected");
	check(loaded.CaptureState() == valid, "Invalid XML hold preserves all state");
	check(!loaded.LoadXml("<campath><points><p t=\"10\" holdDuration=\"nan\"/></points></campath>"),
		  "Nonfinite XML hold rejected");
}

void collectionPlayback()
{
	CamPathCollection paths;
	populate(paths.Active());
	check(paths.Active().CurveEditing_get(), "Curve editing belongs to the ordinary path");
	CamPathValue value;
	check(!paths.Eval(15, value), "Master enable initially off");
	paths.Enabled_set(true);
	check(paths.Eval(15, value), "Ordinary path plays when enabled");
	near(value.X, 50);
	paths.New("Close shot");
	check(paths.Active().PositionInterpMethod_get() == CamPath::DI_SMOOTH,
		  "Fresh paths use two-key smooth interpolation");
	paths.Active().Add(12, CamPathValue(1000, 0, 0, 0, 0, 0, 90));
	paths.Active().Add(18, CamPathValue(1600, 0, 0, 0, 0, 0, 90));
	paths.Use(0);
	check(paths.Eval(13, value), "Latest starting overlapping path plays even if another is selected");
	near(value.X, 1100);
	check(paths.Eval(19, value), "Earlier path resumes when a shorter overlapping path ends");
	near(value.X, 90);
	paths.At(1).Enabled_set(false);
	check(paths.Eval(13, value), "Muted path does not participate in playback");
	near(value.X, 30);
	paths.At(1).Enabled_set(true);
	paths.At(1).SetOffset(10);
	check(!paths.Eval(21, value), "Time gaps do not override the demo camera");
	check(paths.Eval(23, value), "Path offsets shift automatic playback");
	near(value.X, 1100);
	paths.At(1).SetKeyHold(18, 20);
	check(paths.Eval(30, value), "Final keyframe release extends automatic playback");
	near(value.X, 1600);
	check(!paths.Eval(30.1, value), "Final hold releases after its endpoint");
	paths.Active().SetHold(true);
	check(paths.Eval(0, value), "Classic path-level hold preserves before-start pose");
	near(value.X, 0);
	check(paths.Eval(50, value), "Classic path-level hold preserves after-end pose");
	near(value.X, 100);
	paths.Enabled_set(false);
	check(!paths.Eval(15, value), "Master switch disables all paths");

	CamPathCollection tie;
	populate(tie.Active());
	tie.New("Later in list");
	tie.Active().Add(10, CamPathValue(999, 0, 0, 0, 0, 0, 90));
	tie.Active().Add(20, CamPathValue(999, 0, 0, 0, 0, 0, 90));
	tie.Enabled_set(true);
	check(tie.Eval(15, value), "Equal starting times have a deterministic winner");
	near(value.X, 999);
}

void historyAndCollections()
{
	CamPathCollection paths;
	auto empty = paths.Capture();
	populate(paths.Active());
	paths.Record(empty);
	check(paths.Undo(), "Undo capture to an empty path");
	check(paths.Active().GetSize() == 0, "Undo restores native keys");
	check(paths.Redo(), "Redo capture");
	check(paths.Active().GetSize() == 2, "Redo restores native keys");
	auto before = paths.Capture();
	paths.Active().SetKeyHold(10, 13);
	paths.Active().SetHandle(10, true, Vector3(0, 100, 0));
	paths.Rename("Wide & <shot> \"A\"");
	paths.Record(before);
	auto edited = paths.Capture();
	check(paths.Undo() && paths.Capture() == before, "Undo restores holds, handles, settings and names together");
	check(paths.Redo() && paths.Capture() == edited, "Redo restores complete edited state");
	before = paths.Capture();
	paths.New("Second");
	paths.Record(before);
	check(paths.Undo() && paths.Capture() == before, "Undo removes created path and restores selection");
	check(paths.Redo() && paths.Size() == 2 && paths.ActiveIndex() == 1, "Redo restores created path");
	before = paths.Capture();
	rejected([&] { paths.Rename(paths.At(0).GetName()); }, "Duplicate names rejected");
	rejected([&] { paths.New("Second"); }, "Duplicate path creation rejected");
	check(paths.Capture() == before, "Rejected collection edits are atomic");
	paths.Remove(0);
	paths.Record(before);
	check(paths.Undo() && paths.Capture() == before, "Undo path deletion");
	paths.Enabled_set(true);
	before = paths.Capture();
	paths.Rename("A different second shot");
	paths.Record(before);
	check(!paths.Redo(), "New edit invalidates redo history");
	check(paths.Undo() && paths.Enabled_get(), "Undo never toggles the master playback switch");

	auto directory = std::filesystem::temp_directory_path() / "afx-campath-collection-tests";
	std::filesystem::create_directories(directory);
	auto group = directory / "all.xml", single = directory / "one.xml";
	paths.At(0).Enabled_set(false);
	check(paths.SaveAll(group.wstring().c_str()), "Collection saves through native serializer");
	CamPathCollection loaded;
	check(loaded.Load(group.wstring().c_str()), "Collection XML loads");
	check(loaded.Capture() == paths.Capture(),
		  "Collection XML preserves names, selection, holds, curves and muted paths");
	check(!loaded.Enabled_get(), "Loading does not start playback");
	before = paths.Capture();
	paths.New("Unsaved third path");
	auto preLoad = paths.Capture();
	check(paths.Load(group.wstring().c_str()), "Loading a collection replaces the collection");
	paths.Record(preLoad);
	check(paths.Undo() && paths.Capture() == preLoad, "Undo restores a replaced collection");
	check(paths.Redo() && paths.Capture() == before, "Redo reapplies a collection load");
	loaded.Use(0);
	check(loaded.Active().Save(single.wstring().c_str()), "One selected path saves as native campath XML");
	CamPathCollection one;
	check(one.Load(single.wstring().c_str()) && one.Size() == 1, "Single native XML auto-detected");
	near(one.Active().GetBegin().GetValue().HoldDuration, 3);
	check(one.Active().GetName() == paths.At(0).GetName(), "Escaped names survive both native and collection XML");
	auto original = loaded.Capture();
	for (const auto & xml :
		 {"<campaths version=\"99\"/>",
		  "<campaths version=\"1\" active=\"5\"><campath name=\"One\"><points/></campath></campaths>",
		  "<campaths version=\"1\"><campath name=\"One\"><points/></campath><campath "
		  "name=\"One\"><points/></campath></campaths>",
		  "<campaths version=\"1\"><campath name=\"One\" enabled=\"2\"><points/></campath></campaths>"})
	{
		{
			std::ofstream output(group);
			output << xml;
		}
		check(!loaded.Load(group.wstring().c_str()), "Malformed collection rejected");
		check(loaded.Capture() == original, "Malformed collection leaves all existing paths intact");
	}
	std::filesystem::remove_all(directory);

	CamPathCollection bounded;
	for (int i = 0; i < 70; ++i)
	{
		auto state = bounded.Capture();
		bounded.Rename("Shot " + std::to_string(i));
		bounded.Record(state);
	}
	int undoCount = 0;
	while (bounded.Undo())
		++undoCount;
	check(undoCount == 64, "History is bounded to 64 edits");
}

void easingAndSmoothHolds()
{
	for (int function = CamPathEase::Inherit; function < CamPathEase::Count; ++function)
		for (auto direction : {CamPathEase::In, CamPathEase::Out, CamPathEase::InOut})
		{
			CamPathEase ease;
			ease.Type = CamPathEase::Function(function);
			ease.Mode = direction;
			check(ease.Valid(), "All default easing presets are valid");
			near(ease.Eval(0), 0);
			near(ease.Eval(1), 1);
			check(std::abs(ease.Eval(1e-8)) < 1e-3 && std::abs(ease.Eval(1 - 1e-8) - 1) < 1e-3,
				  "Preset endpoints are continuous");
			for (int i = 0; i <= 200; ++i)
				check(std::isfinite(ease.Eval(i / 200.0)), "Preset samples remain finite");
		}
	CamPath path;
	populate(path);
	CamPathEase ease;
	ease.Type = CamPathEase::Quad;
	ease.Mode = CamPathEase::In;
	path.SetEase(ease);
	near(path.Eval(15).X, 25);
	near(path.Eval(15).Fov, 82.5);
	near(path.Eval(15).R.ToQREulerAngles().ToQEulerAngles().Yaw, 22.5);
	path.Add(30, CamPathValue(200, 0, 0, 0, 180, 0, 30));
	ease.Type = CamPathEase::Linear;
	check(path.SetKeyEase(20, ease), "An outgoing segment can override path easing");
	near(path.Eval(25).X, 150);
	near(path.Eval(15).X, 25);
	ease.Type = CamPathEase::Inherit;
	path.SetKeyEase(20, ease);
	near(path.Eval(25).X, 125);
	check(!path.SetKeyEase(30, ease), "Final key has no outgoing segment to edit");
	ease.Type = CamPathEase::Back;
	ease.Mode = CamPathEase::Out;
	path.SetKeyEase(10, ease);
	check(path.Eval(17).X > 100, "Back easing overshoots the selected segment endpoint");
	check(path.Eval(17).X < 120, "Overshoot stays with the current segment, not a different global interval");
	ease.Type = CamPathEase::Bezier;
	ease.X1 = 1.0 / 3;
	ease.Y1 = 0;
	ease.X2 = 2.0 / 3;
	ease.Y2 = 1;
	path.SetEase(ease);
	near(path.Eval(12.5).X, 15.625, 1e-5);
	ease.Y1 = -1;
	ease.Y2 = 2;
	path.SetKeyEase(10, ease);
	check(path.Eval(11).X < 0, "Custom Bezier handles support anticipatory movement");
	auto previous = path.CaptureState();
	ease.X1 = 0.9;
	ease.X2 = 0.1;
	check(!path.SetKeyEase(10, ease), "Crossed time handles are rejected");
	rejected([&] { path.SetEase(ease); }, "Invalid global easing is rejected");
	check(path.CaptureState() == previous, "Invalid graph edits leave the path intact");

	CamPath held;
	populate(held);
	held.PositionInterpMethod_set(CamPath::DI_SMOOTH);
	held.Add(30, CamPathValue(200, 0, 0, 0, 180, 0, 30));
	held.SetKeyHold(20, 24);
	double epsilon = 1e-3;
	check((held.Eval(20).X - held.Eval(20 - epsilon).X) / epsilon < 1e-3,
		  "Smooth arrival decelerates to zero velocity");
	check((held.Eval(24 + epsilon).X - held.Eval(24).X) / epsilon < 1e-3, "Smooth departure starts at zero velocity");
	near(held.Eval(23).X, 100);
	near(held.Eval(23).Fov, 60);
	near(held.Eval(23).R.ToQREulerAngles().ToQEulerAngles().Yaw, 90);
	near(held.Eval(30).X, 200);
	ease = CamPathEase();
	ease.Type = CamPathEase::Bezier;
	ease.Y1 = 1;
	ease.Y2 = 0;
	held.SetEase(ease);
	check((held.Eval(20).X - held.Eval(20 - epsilon).X) / epsilon < 0.1, "Bezier timing flattens held arrival tangent");
	check((held.Eval(24 + epsilon).X - held.Eval(24).X) / epsilon < 0.1,
		  "Bezier timing flattens held departure tangent");
	CamPath restored;
	restored.CurveEditing_set(held.CurveEditing_get());
	check(restored.LoadXml(held.ToXml()), "Global easing and holds save through native XML");
	check(restored.CaptureState() == held.CaptureState(), "Easing XML preserves parameters and metadata exactly");
	CamPath perSegment;
	perSegment.CurveEditing_set(path.CurveEditing_get());
	check(perSegment.LoadXml(path.ToXml()), "Segment easing loads through native XML");
	check(perSegment.CaptureState() == path.CaptureState(), "Per-segment easing round-trips");
	auto valid = perSegment.CaptureState();
	check(!perSegment.LoadXml("<campath ease=\"bezier\" easeX1=\"nan\"><points/></campath>"),
		  "Nonfinite graph controls in XML rejected");
	check(perSegment.CaptureState() == valid, "Invalid graph load is atomic");
	check(!perSegment.LoadXml("<campath><points><p t=\"1\" ease=\"unknown\"/></points></campath>"),
		  "Unknown segment easing rejected");
	CamPathCollection history;
	populate(history.Active());
	auto before = history.Capture();
	ease = CamPathEase();
	ease.Type = CamPathEase::Bounce;
	history.Active().SetEase(ease);
	history.Record(before);
	check(history.Undo() && history.Capture() == before, "Easing edits participate in existing history");
	check(history.Redo() && history.Active().GetEase() == ease, "Redo restores easing settings");
	history.New("Second easing path");
	populate(history.Active());
	ease.Type = CamPathEase::Elastic;
	check(history.Active().SetKeyEase(10, ease), "Second path stores its own segment easing");
	auto collectionFile = std::filesystem::temp_directory_path() / "afx-easing-collection.xml";
	check(history.SaveAll(collectionFile.wstring().c_str()), "Easing collection saves");
	CamPathCollection restoredCollection;
	check(restoredCollection.Load(collectionFile.wstring().c_str()), "Easing collection loads");
	check(restoredCollection.Capture() == history.Capture(),
		  "Collection preserves path defaults and segment overrides");
	std::filesystem::remove(collectionFile);
}
} // namespace
int main()
{
	try
	{
		captureAndTiming();
		curves();
		placement();
		persistence();
		localInterpolation();
		keyframeHolds();
		collectionPlayback();
		historyAndCollections();
		easingAndSmoothHolds();
		std::cout << checks << " campath checks passed\n";
	}
	catch (const std::exception & e)
	{
		std::cerr << "After " << checks << " checks: " << e.what() << '\n';
		return 1;
	}
}
#endif
