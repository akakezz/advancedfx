#include "shared/CamPath.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
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
		  "<campath><points><p t=\"1\" tx_w_out=\"nan\"/></points></campath>", "<campath><points>"})
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
	std::filesystem::remove(file);
}
} // namespace
int main()
{
	try
	{
		continuity();
		tangents();
		files();
		std::cout << checks << " TF2 camera checks passed\n";
	}
	catch (const std::exception &e)
	{
		std::cerr << "After " << checks << " checks: " << e.what() << "\n";
		return 1;
	}
}
