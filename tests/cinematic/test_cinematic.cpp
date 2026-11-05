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
} // namespace
int main()
{
	try
	{
		captureAndTiming();
		curves();
		placement();
		persistence();
		std::cout << checks << " campath checks passed\n";
	}
	catch (const std::exception & e)
	{
		std::cerr << "After " << checks << " checks: " << e.what() << '\n';
		return 1;
	}
}
#endif
