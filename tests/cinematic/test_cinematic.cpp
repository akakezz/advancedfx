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
	return key == VK_ESCAPE || key == VK_OEM_3 || key == VK_F2 || key == VK_SHIFT || key == VK_LSHIFT ||
		   key == VK_RSHIFT;
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
	for (USHORT key : {VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_F2})
	{
		inputCheck(!input.Supply_KeyEvent(MirvInput::KS_DOWN, key, 1), "Native demo UI receives Shift and F2 presses");
		packet = keyboard(key);
		raw(input, packet);
		inputCheck(packet.data.keyboard.VKey == key, "Native demo UI receives raw modifier/key presses");
		inputCheck(!input.Supply_KeyEvent(MirvInput::KS_UP, key, 1), "Native demo UI receives modifier/key releases");
		packet = keyboard(key, true);
		raw(input, packet);
		inputCheck(packet.data.keyboard.VKey == key, "Native demo UI receives raw modifier/key releases");
	}
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
	input.Supply_KeyEvent(MirvInput::KS_DOWN, 'W', 1);
	inputCheck(input.GetCamDForward() > 0, "Movement is held before the graph takes input");
	input.ClearMovementInput(true);
	inputCheck(input.GetCamDForward() == 0, "Graph handoff clears held movement immediately");
	inputCheck(input.GetCameraControlMode() && input.GetKeyboardCapture(),
			   "Graph handoff retains freecam and preview modes");
	dependencies.Suspended = true;
	POINT cursor = {200, 300};
	input.Supply_GetCursorPos(&cursor);
	inputCheck(cursor.x == 200 && cursor.y == 300, "GUI cursor position is not replaced by freecam while suspended");
	input.Supply_MouseFrameEnd();
	inputCheck(input.GetCameraControlMode() && input.GetKeyboardCapture(),
			   "Suspended freecam retains its modes across frames");
	dependencies.Suspended = false;
	input.ClearMovementInput(true);
	input.Supply_KeyEvent(MirvInput::KS_UP, 'W', 1);
	inputCheck(input.Supply_KeyEvent(MirvInput::KS_DOWN, 'W', 1) && input.GetCamDForward() > 0,
			   "Movement resumes after graph input is released");
	input.Supply_KeyEvent(MirvInput::KS_UP, 'W', 1);
	// Preview clears movement before every free-camera override during a scrub.
	input.Supply_KeyEvent(MirvInput::KS_DOWN, 'W', 1);
	RAWINPUT mouse = {};
	mouse.header.dwType = RIM_TYPEMOUSE;
	mouse.header.dwSize = sizeof(mouse);
	mouse.data.mouse.lLastX = 25;
	mouse.data.mouse.lLastY = -10;
	raw(input, mouse);
	inputCheck(input.GetCamDForward() != 0 && input.GetCamDYaw() != 0,
			   "Keyboard and mouse accumulate before scrub lock");
	input.SetTx(25);
	input.SetTy(40);
	input.SetTz(60);
	input.SetRx(10);
	input.SetRy(20);
	input.SetRz(30);
	input.SetFov(75);
	input.ClearMovementInput();
	float x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0, fov = 90;
	input.Override(0.25f, x, y, z, rx, ry, rz, fov);
	inputCheck(x == 25 && y == 40 && z == 60 && rx == 10 && ry == 20 && rz == 30 && fov == 75,
			   "Scrub camera pose survives free-camera evaluation with keyboard and mouse input");
	inputCheck(input.GetCamDForward() == 0 && input.GetCamDYaw() == 0 && input.GetCamDPitch() == 0,
			   "Scrub lock discards translation and rotation deltas");
	inputCheck(input.GetKeyboardCapture() && input.GetCameraControlMode(),
			   "Scrub lock retains input ownership and free-camera mode");
	input.Supply_KeyEvent(MirvInput::KS_UP, 'W', 1);
	policy.AllowUnused = false;
	input.Supply_KeyEvent(MirvInput::KS_DOWN, 'Q', 1);
	input.ClearMovementInput(true);
	// The GUI consumes this key's release while it owns input.
	policy.AllowUnused = true;
	inputCheck(!input.Supply_KeyEvent(MirvInput::KS_DOWN, 'Q', 1),
			   "GUI-owned releases cannot leave permissive preview keys captured");
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
	if (!(std::abs(a - b) <= tolerance))
		std::cerr << "Expected " << b << ", got " << a << " (tolerance " << tolerance << ")\n";
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
void alignedHandles()
{
	CamPathCollection paths;
	auto & path = paths.Active();
	populate(path);
	path.Add(30, CamPathValue(200, 100, 0, 0, 0, 0, 90));
	check(std::next(path.GetBegin().wrapped)->second.HandlesAligned, "New keys use aligned spatial handles");
	Vector3 in, out;
	path.GetHandle(20, false, in);
	double originalLength = in.Length();
	check(path.SetHandle(20, true, Vector3(0, 0, 12)), "Set an aligned outgoing handle");
	path.GetHandle(20, false, in);
	near(in.X, 0);
	near(in.Y, 0);
	near(in.Z, -originalLength);
	check(path.SetHandle(20, false, Vector3(3, 4, 0)), "Move the incoming aligned handle");
	path.GetHandle(20, true, out);
	near(out.X, -7.2);
	near(out.Y, -9.6);
	near(out.Z, 0);
	near(out.Length(), 12);
	path.GetHandle(20, false, in);
	near(in.Length(), 5);
	auto before = path.Eval(20 - 1e-5), at = path.Eval(20), after = path.Eval(20 + 1e-5);
	Vector3 arrival(at.X - before.X, at.Y - before.Y, at.Z - before.Z),
		departure(after.X - at.X, after.Y - at.Y, after.Z - at.Z);
	arrival = arrival.Normalize();
	departure = departure.Normalize();
	near(arrival.X * departure.X + arrival.Y * departure.Y + arrival.Z * departure.Z, 1, 1e-5);
	check(path.SetHandleMode(20, false), "Free handles remain available for corners");
	path.SetHandle(20, false, Vector3(0, 5, 0));
	path.SetHandle(20, true, Vector3(12, 0, 0));
	path.GetHandle(20, false, in);
	near(in.Y, 5);
	near(in.X, 0);
	auto corner = paths.Capture();
	check(path.SetHandleMode(20, true), "Existing corners can be aligned");
	path.GetHandle(20, false, in);
	path.GetHandle(20, true, out);
	near(in.X, -5);
	near(in.Y, 0);
	near(in.Length(), 5);
	near(out.Length(), 12);
	paths.Record(corner);
	check(paths.Undo() && paths.Capture() == corner, "Undo restores both handle directions and mode");
	check(paths.Redo() && std::next(paths.Active().GetBegin().wrapped)->second.HandlesAligned,
		  "Redo restores alignment");
	// Undo/redo can replace the active path object.
	auto & current = paths.Active();
	check(current.SetHandle(20, true, Vector3(0, 0, 0)), "A collapsed handle has no required direction");
	check(current.SetHandle(20, true, Vector3(1e-200, 0, 0)), "Tiny nonzero handle alignment stays finite");
	current.GetHandle(20, false, in);
	near(in.X, -5);
	near(in.Length(), 5);
	auto valid = current.CaptureState();
	check(!current.SetHandle(20, true, Vector3(std::numeric_limits<double>::quiet_NaN(), 0, 0)),
		  "Invalid aligned handle rejected");
	check(current.CaptureState() == valid, "Rejected alignment leaves both handles unchanged");
	check(!current.SetHandle(std::numeric_limits<double>::quiet_NaN(), true, Vector3(1, 0, 0)),
		  "Nonfinite handle key time rejected");
	check(!current.SetHandleMode(std::numeric_limits<double>::quiet_NaN(), false), "Nonfinite mode key time rejected");
	check(current.CaptureState() == valid, "Invalid key time cannot modify the first keyframe");
	current.SetHandleMode(20, false);
	current.SetHandle(20, false, Vector3(1e9, 1e9, 0));
	valid = current.CaptureState();
	check(!current.SetHandleMode(20, true), "Alignment which exceeds the handle bound is rejected");
	check(current.CaptureState() == valid, "Rejected mode change is atomic");
	current.SetHandle(20, false, Vector3(0, 5, 0));
	CamPath restored;
	restored.CurveEditing_set(true);
	restored.Enabled_set(current.Enabled_get());
	check(restored.LoadXml(current.ToXml()), "Handle modes load from native XML");
	check(restored.CaptureState() == current.CaptureState(), "Free and aligned modes round-trip with the curve");
	check(restored.SetHandleMode(20, true), "Loaded free handles can be aligned");
	CamPath aligned;
	aligned.CurveEditing_set(true);
	aligned.Enabled_set(restored.Enabled_get());
	check(aligned.LoadXml(restored.ToXml()) && aligned.CaptureState() == restored.CaptureState(),
		  "Aligned controls round-trip exactly");
	valid = aligned.CaptureState();
	check(!aligned.LoadXml("<campath><points><p t=\"1\" handleMode=\"unknown\"/></points></campath>"),
		  "Unknown handle mode rejected");
	check(aligned.CaptureState() == valid, "Invalid mode load preserves the current path");
	CamPath legacy;
	legacy.CurveEditing_set(true);
	check(legacy.LoadXml("<campath positionInterp=\"linear\" rotationInterp=\"sLinear\" fovInterp=\"linear\" "
						 "curve=\"bezier\"><points>"
						 "<p t=\"0\"/><p t=\"1\" inX=\"-10\" inY=\"0\" inZ=\"0\" outX=\"0\" outY=\"20\" outZ=\"0\"/>"
						 "<p t=\"2\"/></points></campath>"),
		  "Older authored handles load");
	check(!std::next(legacy.GetBegin().wrapped)->second.HandlesAligned, "Legacy corners retain free handles");
	legacy.SetHandle(1, true, Vector3(0, 30, 0));
	legacy.GetHandle(1, false, in);
	near(in.X, -10);
	near(in.Y, 0);
	check(legacy.GetBegin().GetValue().HandlesAligned, "Classic keys without authored handles use the aligned default");
}
void continuousTravel()
{
	CamPath native;
	populate(native);
	native.CurveEditing_set(true);
	native.Add(30, CamPathValue(200, 0, 0, 0, 160, 0, 45));
	native.SetHandle(10, true, Vector3(1, 0, 0));
	native.SetHandle(20, false, Vector3(-2, 0, 0));
	native.SetHandle(20, true, Vector3(35, 0, 0));
	native.SetHandle(30, false, Vector3(-1, 0, 0));
	CamPath flow;
	flow.RestoreState(native.CaptureState());
	auto authored = flow.CaptureState();
	flow.SpeedContinuous_set(true);
	for (int i = 0; i <= 200; ++i)
	{
		double t = 10 + i * 0.1;
		auto a = flow.Eval(t), b = native.Eval(t);
		near(a.X, (t - 10) * 10, 1e-5);
		near(a.Y, 0);
		near(a.Fov, b.Fov);
		near(std::abs(DotProduct(a.R, b.R)), 1);
	}
	double e = 1e-4;
	near((flow.Eval(20).X - flow.Eval(20 - e).X) / e, 10, 0.005);
	near((flow.Eval(20 + e).X - flow.Eval(20).X) / e, 10, 0.005);
	auto stored = flow.CaptureState();
	stored.ContinuousSpeed = false;
	check(stored == authored, "Continuous travel preserves keyframes, handles and arrival times");
	flow.SpeedContinuous_set(false);
	check(flow.CaptureState() == authored, "Native speed restores the original path state");
	for (int i = 0; i <= 200; ++i)
		near(flow.Eval(10 + i * 0.1).X, native.Eval(10 + i * 0.1).X);
	flow.SpeedContinuous_set(true);
	flow.SetPosition(200, 0, 0);
	near(flow.Eval(15).X, 150, 1e-5);
	flow.SetDuration(40);
	near(flow.Eval(20).X, 150, 1e-5);
	check(flow.GetSize() == 3, "Continuous speed uses ordinary full camera keyframes");
	CamPath curved;
	curved.RestoreState(native.CaptureState());
	curved.SetHandle(10, true, Vector3(10, 30, 5));
	curved.SetHandle(20, false, Vector3(-5, 0, 0));
	curved.SetHandle(20, true, Vector3(40, 0, 0));
	curved.SetHandle(30, false, Vector3(-10, -40, -5));
	CamPath curvedFlow;
	curvedFlow.RestoreState(curved.CaptureState());
	curvedFlow.SpeedContinuous_set(true);
	for (int i = 1; i < 100; ++i)
	{
		double t = 10 + i * 0.2;
		auto p = curvedFlow.Eval(t);
		double lo = t < 20 ? 10 : 20, hi = t < 20 ? 20 : 30;
		for (int n = 0; n < 45; ++n)
		{
			double m = (lo + hi) / 2;
			if (curved.Eval(m).X < p.X)
				lo = m;
			else
				hi = m;
		}
		auto q = curved.Eval((lo + hi) / 2);
		near(p.X, q.X, 1e-5);
		near(p.Y, q.Y, 1e-5);
		near(p.Z, q.Z, 1e-5);
	}
	auto a = curvedFlow.Eval(20 - e), b = curvedFlow.Eval(20), c = curvedFlow.Eval(20 + e);
	Vector3 va((b.X - a.X) / e, (b.Y - a.Y) / e, (b.Z - a.Z) / e),
		vb((c.X - b.X) / e, (c.Y - b.Y) / e, (c.Z - b.Z) / e);
	check(va.Length() > 1 && vb.Length() > 1, "Curved travel passes through ordinary keyframes without stopping");
	near((va - vb).Length(), 0, 0.02);
	curvedFlow.SetKeyHold(20, 23);
	near(curvedFlow.Eval(21).X, 100);
	near(curvedFlow.Eval(22).X, 100);
	near((curvedFlow.Eval(20).X - curvedFlow.Eval(20 - e).X) / e, 0, 0.01);
	near((curvedFlow.Eval(23 + e).X - curvedFlow.Eval(23).X) / e, 0, 0.01);
	for (auto type :
		 {CamPathEase::Sine, CamPathEase::Back, CamPathEase::Bounce, CamPathEase::Elastic, CamPathEase::Bezier})
	{
		CamPathEase ease;
		ease.Type = type;
		curved.SetEase(ease);
		curvedFlow.RestoreState(curved.CaptureState());
		curvedFlow.SpeedContinuous_set(true);
		curvedFlow.BlendStrength_set(0);
		for (int i = 0; i <= 100; ++i)
		{
			double t = 10 + i * 0.2;
			auto x = curved.Eval(t), y = curvedFlow.Eval(t);
			near(x.X, y.X);
			near(x.Y, y.Y);
			near(x.Fov, y.Fov);
			near(std::abs(DotProduct(x.R, y.R)), 1);
		}
	}
	CamPath stationary;
	stationary.PositionInterpMethod_set(CamPath::DI_LINEAR);
	stationary.RotationInterpMethod_set(CamPath::QI_SLINEAR);
	stationary.FovInterpMethod_set(CamPath::DI_LINEAR);
	stationary.Add(10, CamPathValue(5, 6, 7, 0, 0, 0, 90));
	stationary.Add(20, CamPathValue(5, 6, 7, 0, 0, 0, 90));
	stationary.SpeedContinuous_set(true);
	for (int i = 0; i <= 20; ++i)
	{
		auto p = stationary.Eval(10 + i * 0.5);
		near(p.X, 5);
		near(p.Y, 6);
		near(p.Z, 7);
	}
	stationary.PositionInterpMethod_set(CamPath::DI_STEP);
	near(stationary.Eval(15).X, 5);
	CamPath notReady;
	notReady.SpeedContinuous_set(true);
	notReady.Add(0, CamPathValue());
	notReady.Add(1, CamPathValue(1, 0, 0, 0, 0, 0, 90));
	near(notReady.GetSegmentProgress(0, 0.5), 0.5);
	CamPath tiny;
	tiny.PositionInterpMethod_set(CamPath::DI_LINEAR);
	tiny.RotationInterpMethod_set(CamPath::QI_SLINEAR);
	tiny.FovInterpMethod_set(CamPath::DI_LINEAR);
	tiny.Add(0, CamPathValue());
	tiny.Add(1e-320, CamPathValue(100, 0, 0, 0, 0, 0, 90));
	tiny.SpeedContinuous_set(true);
	check(std::isfinite(tiny.Eval(5e-321).X),
		  "Unrepresentable travel rates fall back without producing nonfinite camera positions");
	CamPath saved;
	saved.RestoreState(flow.CaptureState());
	CamPath loaded;
	loaded.CurveEditing_set(true);
	check(loaded.LoadXml(saved.ToXml()), "Continuous travel saves in native XML");
	check(loaded.CaptureState() == saved.CaptureState(), "Continuous speed and all pose metadata round-trip");
	auto valid = loaded.CaptureState();
	check(!loaded.LoadXml("<campath continuousSpeed=\"bad\"><points/></campath>"), "Invalid speed setting is rejected");
	check(loaded.CaptureState() == valid, "Rejected speed settings restore the previous path");
	check(loaded.LoadXml(native.ToXml()) && !loaded.SpeedContinuous_get(),
		  "Older native files retain native speed by default");
	CamPathCollection history;
	check(history.Active().SpeedContinuous_get(), "New paths enable continuous travel");
	populate(history.Active());
	auto before = history.Capture();
	history.Active().SpeedContinuous_set(false);
	history.Record(before);
	check(history.Undo() && history.Capture() == before, "Undo restores travel interpolation");
	check(history.Redo() && !history.Active().SpeedContinuous_get(), "Redo restores native speed");
}
void continuousRotation()
{
	CamPath path;
	path.PositionInterpMethod_set(CamPath::DI_SMOOTH);
	path.RotationInterpMethod_set(CamPath::QI_SMOOTH);
	path.FovInterpMethod_set(CamPath::DI_LINEAR);
	path.Add(0, CamPathValue(0, 0, 0, 0, 0, 0, 90));
	path.Add(2, CamPathValue(20, 0, 0, 0, 20, 0, 90));
	path.Add(7, CamPathValue(70, 0, 0, 0, 70, 0, 90));
	double e = 1e-4;
	auto a = path.Eval(2 - e), b = path.Eval(2), c = path.Eval(2 + e);
	near((b.X - a.X) / e, 10, 0.001);
	near((c.X - b.X) / e, 10, 0.001);
	double left = (b.R.ToQREulerAngles().ToQEulerAngles().Yaw - a.R.ToQREulerAngles().ToQEulerAngles().Yaw) / e;
	double right = (c.R.ToQREulerAngles().ToQEulerAngles().Yaw - b.R.ToQREulerAngles().ToQEulerAngles().Yaw) / e;
	near(left, 10, 0.01);
	near(right, 10, 0.01);
	CamPath turning;
	turning.PositionInterpMethod_set(CamPath::DI_SMOOTH);
	turning.FovInterpMethod_set(CamPath::DI_LINEAR);
	turning.RotationInterpMethod_set(CamPath::QI_SMOOTH);
	turning.Add(0, CamPathValue(0, 0, 0, 10, 170, 5, 90));
	turning.Add(2, CamPathValue(20, 0, 0, 25, -175, -10, 90));
	turning.Add(6, CamPathValue(60, 0, 0, -5, -140, 20, 90));
	auto qa = turning.Eval(2 - e).R, qb = turning.Eval(2).R, qc = turning.Eval(2 + e).R;
	if (DotProduct(qa, qb) < 0)
		qa = -1.0 * qa;
	if (DotProduct(qb, qc) < 0)
		qc = -1.0 * qc;
	Vector3 axisA, axisB;
	double speedA = qa.GetAng(qb, axisA) / e, speedB = qb.GetAng(qc, axisB) / e;
	near(speedA, speedB, 0.002);
	check(speedA > 0.01, "Smooth rotation carries angular velocity through wrapped nonuniform keys");
	near((axisA - axisB).Length(), 0, 0.002);
	near(qb.Norm(), 1);
	CamPath same;
	populate(same);
	same.RotationInterpMethod_set(CamPath::QI_SMOOTH);
	same.SetAngles(0, 0, 0);
	for (int i = 0; i <= 40; ++i)
		near(same.Eval(10 + i * 0.25).R.Norm(), 1);
}
void channelEasingAndBlend()
{
	CamPath path;
	populate(path);
	path.Add(30, CamPathValue(200, 0, 0, 0, 160, 0, 40));
	CamPathEase shared;
	shared.Type = CamPathEase::Sine;
	path.SetEase(shared);
	auto baseline = path.Eval(12.5);
	CamPathEase fov;
	fov.Type = CamPathEase::Quad;
	fov.Mode = CamPathEase::In;
	path.SetEase(fov, true, CamPathEase::Fov);
	auto changed = path.Eval(12.5);
	near(changed.X, baseline.X);
	near(std::abs(DotProduct(changed.R, baseline.R)), 1);
	near(changed.Fov, 90 + (60 - 90) * 0.0625);
	CamPathEase angle;
	angle.Type = CamPathEase::Bounce;
	angle.Mode = CamPathEase::Out;
	path.SetKeyEase(10, angle, CamPathEase::Angles);
	auto rotated = path.Eval(12.5);
	near(rotated.X, changed.X);
	near(rotated.Fov, changed.Fov);
	check(std::abs(DotProduct(rotated.R, changed.R)) < 0.999, "Angle easing changes independently of position and FOV");
	CamPathEase selected;
	selected.Type = CamPathEase::Cubic;
	selected.Mode = CamPathEase::In;
	path.SetKeyEase(10, selected);
	CamPathEase linear;
	linear.Type = CamPathEase::Linear;
	path.SetEase(linear, true, CamPathEase::Fov);
	check(path.GetKeyEase(10, false, CamPathEase::Fov).Type == CamPathEase::Linear,
		  "Whole-path FOV edit overrides shared segment easing only for FOV");
	check(path.GetKeyEase(10, false, CamPathEase::Position).Type == CamPathEase::Cubic,
		  "FOV changes preserve positional overrides");
	path.SetKeyEase(10, angle, CamPathEase::Angles);
	path.SetKeyEase(10, CamPathEase(), CamPathEase::Fov);
	check(path.GetKeyEase(10, false, CamPathEase::Fov).Type == CamPathEase::Linear,
		  "Per-channel inherit follows its path default");
	auto stored = path.CaptureState();
	CamPath loaded;
	check(loaded.LoadXml(path.ToXml()), "Independent channel easings save in native XML");
	check(loaded.CaptureState() == stored, "Optional channel overrides and explicit inherit round-trip");
	check(!loaded.LoadXml("<campath fovEase=\"unknown\"><points/></campath>"), "Invalid channel easing is rejected");
	check(loaded.CaptureState() == stored, "Failed channel load is atomic");
	check(
		!loaded.LoadXml("<campath><points><p t=\"1\" anglesEase=\"bezier\" anglesEaseX1=\"nan\"/></points></campath>"),
		"Nonfinite per-key channel controls are rejected");
	path.SetEase(CamPathEase());
	for (auto ch : {CamPathEase::Position, CamPathEase::Angles, CamPathEase::Fov})
		check(path.GetKeyEase(10, false, ch).Type == CamPathEase::Inherit,
			  "All-channel edits reset independent overrides together");
	CamPathCollection history;
	populate(history.Active());
	auto before = history.Capture();
	history.Active().SetKeyEase(10, angle, CamPathEase::Angles);
	history.Record(before);
	check(history.Undo() && history.Capture() == before, "Channel edits use shared undo");
	check(history.Redo() && history.Active().GetKeyEase(10, false, CamPathEase::Angles).Type == CamPathEase::Bounce,
		  "Channel redo restores the chosen channel only");
	for (auto type : {CamPathEase::Sine, CamPathEase::Quad, CamPathEase::Cubic, CamPathEase::Quart, CamPathEase::Quint,
					  CamPathEase::Expo, CamPathEase::Circ, CamPathEase::Back, CamPathEase::Bounce,
					  CamPathEase::Elastic, CamPathEase::Bezier})
	{
		CamPath raw;
		populate(raw);
		raw.Add(30, CamPathValue(200, 0, 0, 0, 160, 0, 40));
		CamPathEase ease;
		ease.Type = type;
		raw.SetEase(ease);
		CamPath blended;
		blended.RestoreState(raw.CaptureState());
		blended.SpeedContinuous_set(true);
		double e = 1e-4;
		auto at = blended.Eval(20), left = blended.Eval(20 - e), right = blended.Eval(20 + e);
		near((at.X - left.X) / e, 10, 0.03);
		near((right.X - at.X) / e, 10, 0.03);
		for (double t : {10.0, 20.0, 30.0})
			near(blended.Eval(t).X, raw.Eval(t).X);
		std::vector<double> original, full;
		for (int i = 0; i <= 100; ++i)
		{
			double t = 10 + i * 0.2;
			auto a = raw.Eval(t), b = blended.Eval(t);
			original.push_back(a.X);
			full.push_back(b.X);
			near(a.Fov, b.Fov);
			near(std::abs(DotProduct(a.R, b.R)), 1);
			check(std::isfinite(b.X), "Blended easing produces finite camera positions");
		}
		blended.BlendStrength_set(0);
		for (int i = 0; i <= 100; ++i)
			near(blended.Eval(10 + i * 0.2).X, original[i]);
		blended.BlendStrength_set(0.5);
		for (int i = 0; i <= 100; ++i)
			near(blended.Eval(10 + i * 0.2).X, (original[i] + full[i]) * 0.5, 1e-5);
		blended.BlendStrength_set(1);
		if (type <= CamPathEase::Circ)
		{
			double previous = -1e-8;
			for (int i = 0; i <= 500; ++i)
			{
				double x = blended.Eval(10 + i * 0.04).X;
				check(x >= previous - 1e-6, "Non-reversing presets stay monotone after endpoint blending");
				previous = x;
			}
		}
		blended.SetKeyHold(20, 23);
		for (double t : {20.0, 21.0, 22.9, 23.0})
		{
			auto p = blended.Eval(t);
			near(p.X, 100);
			near(p.Fov, 60);
			near(std::abs(DotProduct(p.R, raw.Eval(20).R)), 1);
		}
	}
	CamPath marked;
	populate(marked);
	marked.CurveEditing_set(true);
	marked.SetHandle(10, true, Vector3(1, 0, 0));
	marked.SetHandle(20, false, Vector3(-20, 0, 0));
	Vector3 middle;
	check(marked.GetSegmentMidpoint(10, middle), "Native geometry exposes a segment midpoint for labels");
	near(middle.X, 50, 1e-6);
	marked.SetEase(angle);
	marked.SetKeyHold(10, 12);
	marked.SpeedContinuous_set(true);
	marked.BlendStrength_set(0.37);
	Vector3 same;
	marked.GetSegmentMidpoint(10, same);
	near((middle - same).Length(), 0, 1e-6);
	CamPath stepped;
	populate(stepped);
	stepped.Add(30, CamPathValue(200, 0, 0, 0, 160, 0, 40));
	stepped.PositionInterpMethod_set(CamPath::DI_STEP);
	stepped.CurveEditing_set(true);
	stepped.SetHandle(10, true, Vector3(30, 0, 0));
	stepped.SpeedContinuous_set(true);
	for (double t : {20.1, 25.0, 29.9})
		near(stepped.Eval(t).X, 100);
	check(stepped.GetSegmentMidpoint(20, same), "Unedited stepped segment still exposes a label midpoint");
	near(same.X, 150);
	auto valid = marked.CaptureState();
	bool rejected = false;
	try
	{
		marked.BlendStrength_set(std::numeric_limits<double>::quiet_NaN());
	}
	catch (const std::invalid_argument &)
	{
		rejected = true;
	}
	check(rejected && marked.CaptureState() == valid, "Invalid blend strength does not mutate data");
	CamPath roundtrip;
	roundtrip.CurveEditing_set(true);
	check(roundtrip.LoadXml(marked.ToXml()) && roundtrip.CaptureState() == marked.CaptureState(),
		  "Blend strength, holds and handles round-trip together");
	check(!roundtrip.LoadXml("<campath blendStrength=\"1.1\"><points/></campath>"),
		  "Out-of-range serialized blend strength is rejected");
	auto collectionFile = std::filesystem::temp_directory_path() / "afx-channel-sequence.xml";
	history.Active().BlendStrength_set(0.42);
	history.Active().SetEase(fov, true, CamPathEase::Fov);
	history.New("Channels");
	populate(history.Active());
	history.Active().SetKeyEase(10, angle, CamPathEase::Angles);
	check(history.SaveAll(collectionFile.wstring().c_str()), "Independent channel sequence saves");
	CamPathCollection restored;
	check(restored.Load(collectionFile.wstring().c_str()) && restored.Capture() == history.Capture(),
		  "Collection preserves channel settings and blend strengths");
	std::filesystem::remove(collectionFile);
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
		alignedHandles();
		continuousTravel();
		continuousRotation();
		channelEasingAndBlend();
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
