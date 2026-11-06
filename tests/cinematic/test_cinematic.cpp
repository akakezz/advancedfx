#include "shared/CamPath.h"
#include "shared/MirvCampath.h"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
// These tests exercise interpolation commands. File conversion and prefix parsing
// are platform services; XML round trips are tested directly through CamPath.
bool UTF8StringToWideString(const char *, std::wstring &) { return false; }
bool StringBeginsWith(const char *text, const char *prefix)
{
	return std::strncmp(text, prefix, std::strlen(prefix)) == 0;
}
namespace
{
int checks = 0;
void check(bool ok, const char *message)
{
	++checks;
	if (!ok)
		throw std::runtime_error(message);
}
void near(double a, double b, double tolerance = 1e-6)
{
	check(std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= tolerance,
		  "Numeric camera result differs");
}
void custom(CamPath &path)
{
	path.PositionInterpMethod_set(CamPath::DI_CUSTOM);
	path.RotationInterpMethod_set(CamPath::QI_CUSTOM);
	path.FovInterpMethod_set(CamPath::DI_CUSTOM);
}
void populate(CamPath &path)
{
	custom(path);
	for (int i = 0; i < 5; ++i)
	{
		double t = i == 0 ? 0 : i == 1 ? 1 : i == 2 ? 3 : i == 3 ? 4 : 8;
		path.Add(t, CamPathValue(30 * t, 10 * t, -5 * t, 0, 15 * t, 0, 90 - 3 * t));
	}
}
void continuity()
{
	CamPath path;
	populate(path);
	double eps = 1e-5;
	for (double t : {1., 3., 4.})
	{
		auto a = path.Eval(t - eps), b = path.Eval(t), c = path.Eval(t + eps);
		near((b.X - a.X) / eps, (c.X - b.X) / eps, 0.02);
		near((b.Y - a.Y) / eps, (c.Y - b.Y) / eps, 0.02);
		near((b.Fov - a.Fov) / eps, (c.Fov - b.Fov) / eps, 0.02);
		check((c.X - a.X) / (2 * eps) > 10, "Ordinary key does not restart movement at zero velocity");
		auto ea = a.R.ToQREulerAngles().ToQEulerAngles(), eb = b.R.ToQREulerAngles().ToQEulerAngles(),
			 ec = c.R.ToQREulerAngles().ToQEulerAngles();
		near((eb.Yaw - ea.Yaw) / eps, (ec.Yaw - eb.Yaw) / eps, 0.02);
	}
	for (int i = 0; i <= 2000; ++i)
	{
		auto v = path.Eval(i * 8.0 / 2000);
		check(std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Fov) && std::isfinite(v.R.W),
			  "Whole trajectory remains finite");
	}
	CamPath two;
	custom(two);
	two.Add(0, CamPathValue(0, 0, 0, 0, 0, 0, 90));
	two.Add(2, CamPathValue(10, 0, 0, 0, 90, 0, 60));
	check(two.CanEval(), "Custom interpolation draws with two keys");
	near(two.Eval(1).X, 5);
	near(two.Eval(1).Fov, 75);
	CamPath classic;
	classic.Add(0, CamPathValue());
	classic.Add(1, CamPathValue());
	check(!classic.CanEval(), "Classic four-key interpolation keeps original requirement");
}
void tangents()
{
	CamPath p;
	populate(p);
	p.SelectNone();
	p.SelectAdd(size_t(1), size_t(1));
	p.SetTangentMode(CamPath::CH_X, true, true, CamPath::TM_FREE);
	p.SetTangent(CamPath::CH_X, true, true, 12, 12);
	double eps = 1e-5;
	near((p.Eval(1).X - p.Eval(1 - eps).X) / eps, 12, 0.01);
	near((p.Eval(1 + eps).X - p.Eval(1).X) / eps, 12, 0.01);
	p.SetTangentWeight(CamPath::CH_X, true, true, 0.4, 1.8);
	near((p.Eval(1).X - p.Eval(1 - eps).X) / eps, 12, 0.02);
	near((p.Eval(1 + eps).X - p.Eval(1).X) / eps, 12, 0.02);
	p.SetTangentWeight(CamPath::CH_X, true, true, 1, 1);
	p.SetTangentMode(CamPath::CH_X, true, true, CamPath::TM_FLAT);
	near((p.Eval(1 + eps).X - p.Eval(1 - eps).X) / (2 * eps), 0, 0.01);
	p.SetTangentMode(CamPath::CH_X, true, true, CamPath::TM_LINEAR);
	near((p.Eval(1 + eps).X - p.Eval(1).X) / eps, 30, 0.01);
	auto before = p.CaptureState();
	p.SetTangent(CamPath::CH_X, true, true, std::numeric_limits<double>::quiet_NaN(), 0);
	check(p.CaptureState() == before, "Invalid slope leaves trajectory unchanged");
	p.SetTangentWeight(CamPath::CH_X, true, true, -1, 1);
	check(p.CaptureState() == before, "Negative weight is rejected");
	p.SetTangentMode(CamPath::CH_RYAW, true, true, CamPath::TM_FREE);
	p.SetTangent(CamPath::CH_RYAW, true, true, 20, 20);
	p.SetTangentMode(CamPath::CH_FOV, true, true, CamPath::TM_FREE);
	p.SetTangent(CamPath::CH_FOV, true, true, 0, 0);
	check(p.CaptureState().Keys.at(1).TRzOut == 20 && p.CaptureState().Keys.at(1).TfovOut == 0,
		  "Independent channel tangents are retained");
	auto original = p.CaptureState();
	p.SelectNone();
	p.SetDuration(16);
	near(p.GetDuration(), 16);
	p.RestoreState(original);
	check(p.CaptureState() == original, "Snapshot restores timestamps, selection and tangents together");
}
void profiles()
{
	for (auto method : {AutoTangentMethod::Spline, AutoTangentMethod::CatmullRom, AutoTangentMethod::Monotone})
	{
		for (double tension : {0.0, 0.35, 0.9})
		{
			CamPath path;
			populate(path);
			check(path.SetAutoTangentSettings({method, tension}), "Whole-path interpolation is accepted");
			for (size_t index = 1; index + 1 < path.GetSize(); ++index)
			{
				auto it = path.GetBegin();
				for (size_t i = 0; i < index; ++i) ++it;
				const double time = it.GetTime(), eps = 1e-6;
				auto before = path.Eval(time - eps), at = path.Eval(time), after = path.Eval(time + eps);
				near(at.X, it.GetValue().X);
				near((at.X - before.X) / eps, (after.X - at.X) / eps, 0.002);
				near((at.Fov - before.Fov) / eps, (after.Fov - at.Fov) / eps, 0.002);
				check((after.X - before.X) / (2 * eps) > 1, "Auto travel continues through ordinary keys");
				for (int ch = 0; ch < 7; ++ch)
				{
					double in, out, wi, wo;
					check(path.GetTangent((CamPath::Channel)ch, index, false, in, wi), "Graph reads effective incoming tangent");
					check(path.GetTangent((CamPath::Channel)ch, index, true, out, wo), "Graph reads effective outgoing tangent");
					near(in, out); near(wi, 1); near(wo, 1);
				}
			}
			if (tension == 0.35)
			{
				auto file = std::filesystem::temp_directory_path() / "hlae-auto-profile.xml";
				check(path.Save(file.wstring().c_str()), "Automatic profile saves to native XML");
				CamPath loaded;
				check(loaded.Load(file.wstring().c_str()), "Automatic profile loads from native XML");
				check(loaded.CaptureState() == path.CaptureState(), "Automatic profile and settings round-trip");
				for (int i = 0; i <= 20; ++i)
				{
					const double time = i * 8.0 / 20;
					auto expected = path.Eval(time), actual = loaded.Eval(time);
					near(actual.X, expected.X); near(actual.Fov, expected.Fov);
					near(std::abs(DotProduct(actual.R, expected.R)), 1);
				}
				std::filesystem::remove(file);
			}
			CamPath two;
			custom(two);
			two.SetAutoTangentSettings({method, tension});
			two.Add(0, CamPathValue(0, 0, 0, 0, 170, 0, 60));
			two.Add(2, CamPathValue(10, 0, 0, 0, -170, 0, 80));
			check(two.CanEval(), "Every automatic profile works with two keys");
			near(two.Eval(1).X, 5); near(two.Eval(1).Fov, 70);
			near(std::abs(two.Eval(1).R.ToQREulerAngles().ToQEulerAngles().Yaw), 180);
		}
	}
	CamPath path;
	custom(path);
	path.Add(0, CamPathValue(0, 0, 0, 0, 0, 0, 90));
	path.Add(1, CamPathValue(10, 0, 0, 0, 20, 0, 100));
	path.Add(4, CamPathValue(16, 0, 0, 0, 30, 0, 120));
	path.SetAutoTangentSettings({AutoTangentMethod::CatmullRom, 0});
	double slope, weight;
	check(path.GetTangent(CamPath::CH_X, 1, true, slope, weight), "Catmull-Rom tangent is accessible");
	near(slope, 8); // Nonuniform time intervals: (3 * 10 + 1 * 2) / 4.
	path.SetAutoTangentSettings({AutoTangentMethod::CatmullRom, 0.25});
	path.GetTangent(CamPath::CH_X, 1, false, slope, weight); near(slope, 6);
	path.SetAutoTangentSettings({AutoTangentMethod::CatmullRom, 1});
	path.GetTangent(CamPath::CH_X, 1, true, slope, weight); near(slope, 0);
	path.SelectAll();
	path.SetTangentMode(CamPath::CH_X, true, true, CamPath::TM_FREE);
	path.SetTangent(CamPath::CH_X, true, true, 12.3, 12.3);
	path.SetTangentWeight(CamPath::CH_X, true, true, 0.7, 0.7);
	auto manual = path.Eval(0.5);
	path.SetAutoTangentSettings({AutoTangentMethod::Monotone, 0.4});
	path.GetTangent(CamPath::CH_X, 1, true, slope, weight);
	near(slope, 12.3); near(weight, 0.7); near(path.Eval(0.5).X, manual.X);
	auto snapshot = path.CaptureState();
	for (double invalid : {-0.1, 1.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
	{
		check(!path.SetAutoTangentSettings({AutoTangentMethod::Spline, invalid}), "Invalid tension is rejected");
		check(path.CaptureState() == snapshot, "Rejected tension leaves path unchanged");
	}
	check(!path.SetAutoTangentSettings({AutoTangentMethod::Count, 0}), "Unknown profile is rejected");
	path.SetAutoTangentSettings({AutoTangentMethod::Spline, 0});
	path.RestoreState(snapshot);
	check(path.CaptureState() == snapshot, "Undo state includes interpolation profile and tension");
	check(!path.GetTangent(CamPath::CH_X, 99, true, slope, weight), "Invalid key index is rejected");
	CamPath shape;
	custom(shape);
	shape.SetAutoTangentSettings({AutoTangentMethod::Monotone, 0});
	const double times[] = {0, 0.1, 2, 2.2, 6}, positions[] = {0, 10, 10, 15, 14}, fovs[] = {85, 120, 120, 30, 35};
	for (int i = 0; i < 5; ++i)
		shape.Add(times[i], CamPathValue(positions[i], 0, 0, 0, 170 + 10 * i, 0, fovs[i]));
	for (int segment = 0; segment < 4; ++segment)
	{
		for (int i = 0; i <= 300; ++i)
		{
			auto value = shape.Eval(times[segment] + (times[segment + 1] - times[segment]) * i / 300);
			check(value.X >= std::min(positions[segment], positions[segment + 1]) - 1e-8 &&
				value.X <= std::max(positions[segment], positions[segment + 1]) + 1e-8, "Shape-preserving position does not overshoot");
			check(value.Fov >= std::min(fovs[segment], fovs[segment + 1]) - 1e-8 &&
				value.Fov <= std::max(fovs[segment], fovs[segment + 1]) + 1e-8, "Shape-preserving FOV does not overshoot");
			double yaw = value.R.ToQREulerAngles().ToQEulerAngles().Yaw;
			if (yaw < 0) yaw += 360;
			check(yaw >= 170 + 10 * segment - 1e-8 && yaw <= 180 + 10 * segment + 1e-8, "Monotone angles unwrap across 180 degrees");
		}
	}
}
void IgnoreConsole(const char *, ...) {}
void consoleProfiles()
{
	struct Args : advancedfx::ICommandArgs
	{
		std::vector<const char *> Values;
		int ArgC() override { return (int)Values.size(); }
		const char *ArgV(int i) override { return Values[i]; }
	};
	CamPath path;
	path.Add(0, CamPathValue());
	path.Add(2, CamPathValue(10, 0, 0, 0, 30, 0, 90));
	path.SelectAdd(size_t(0), size_t(0));
	const auto keys = path.CaptureState().Keys;
	auto command = [&](const char *option, const char *value) {
		Args args;
		args.Values = {"mirv_campath", "edit", "interp", option, value};
		MirvCampath_ConCommand(&args, IgnoreConsole, IgnoreConsole, &path, nullptr, nullptr, nullptr);
	};
	command("auto", "catmullrom");
	check(path.CanEval() && path.PositionInterpMethod_get() == CamPath::DI_CUSTOM &&
		path.RotationInterpMethod_get() == CamPath::QI_CUSTOM && path.FovInterpMethod_get() == CamPath::DI_CUSTOM,
		"Single console command enables the selected whole-path profile on every channel");
	check(path.GetAutoTangentSettings().Method == AutoTangentMethod::CatmullRom, "Console selects Catmull-Rom");
	command("tension", "0.35"); near(path.GetAutoTangentSettings().Tension, 0.35);
	check(path.CaptureState().Keys == keys, "Profile and tension commands preserve authored keys and selection");
	auto before = path.CaptureState();
	for (const char *bad : {"101", "-1", "nan", "garbage"})
	{
		command("tension", bad);
		check(path.CaptureState() == before, "Invalid console tension preserves the entire path");
	}
	command("auto", "unknown"); check(path.CaptureState() == before, "Unknown console profile preserves path");
	command("auto", "monotone"); check(path.GetAutoTangentSettings().Method == AutoTangentMethod::Monotone, "Console selects shape-preserving interpolation");
}
void files()
{
	CamPath p;
	populate(p);
	p.SetOffset(2.25);
	p.SetHold(true);
	p.SelectAdd(size_t(1), size_t(2));
	for (auto ch : {CamPath::CH_X, CamPath::CH_Y, CamPath::CH_Z, CamPath::CH_FOV, CamPath::CH_RPITCH,
					CamPath::CH_RYAW, CamPath::CH_RROLL})
	{
		p.SetTangentMode(ch, true, true, CamPath::TM_FREE);
		p.SetTangent(ch, true, true, 3.25, 3.25);
		p.SetTangentWeight(ch, true, true, 0.7, 0.7);
	}
	p.SetAutoTangentSettings({AutoTangentMethod::Monotone, 0.3});
	auto file = std::filesystem::temp_directory_path() / "hlae-paper-roundtrip.xml";
	check(p.Save(file.wstring().c_str()), "Native XML saves");
	CamPath q;
	check(q.Load(file.wstring().c_str()), "Native XML loads");
	auto expected = p.CaptureState(), actual = q.CaptureState();
	expected.Enabled = actual.Enabled;
	check(expected == actual, "Every channel slope, weight and native setting round-trips");
	for (int i = 0; i < 200; ++i)
	{
		double t = 8.0 * i / 200;
		near(p.Eval(t).X, q.Eval(t).X);
		near(p.Eval(t).Fov, q.Eval(t).Fov);
		near(std::abs(DotProduct(p.Eval(t).R, q.Eval(t).R)), 1, 1e-6);
	}
	for (const char *bad :
		 {"<notCampath/>", "<campath/>", "<campath><points><p t=\"1\" x=\"nan\"/></points></campath>",
		  "<campath><points><p t=\"1\" tx_w_out=\"nan\"/></points></campath>", "<campath><points>",
		  "<campath autoInterp=\"unknown\"><points><p t=\"0\"/><p t=\"1\"/></points></campath>",
		  "<campath autoTension=\"2\"><points><p t=\"0\"/><p t=\"1\"/></points></campath>",
		  "<campath autoTension=\"nan\"><points><p t=\"0\"/><p t=\"1\"/></points></campath>"})
	{
		{
			std::ofstream o(file);
			o << bad;
		}
		auto before = q.CaptureState();
		check(!q.Load(file.wstring().c_str()), "Invalid XML is rejected");
		check(q.CaptureState() == before, "Failed load preserves existing path");
	}
	{
		std::ofstream o(file);
		o << "<campath><points><p t=\"0\" x=\"1\"/><p t=\"2\" x=\"7\"/></points></campath>";
	}
	check(q.Load(file.wstring().c_str()), "Classic XML still loads");
	check(q.PositionInterpMethod_get() == CamPath::DI_DEFAULT, "Classic file retains original interpolation");
	check(q.GetAutoTangentSettings() == AutoTangentSettings{}, "Old XML resets profile and tension to original defaults");
	std::filesystem::remove(file);
}
} // namespace
int main()
{
	try
	{
		continuity();
		tangents();
		profiles();
		consoleProfiles();
		files();
		std::cout << checks << " TF2 camera checks passed\n";
	}
	catch (const std::exception &e)
	{
		std::cerr << "After " << checks << " checks: " << e.what() << "\n";
		return 1;
	}
}
