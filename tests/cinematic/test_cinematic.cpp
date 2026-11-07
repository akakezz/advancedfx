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
	if (!(std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= tolerance))
		std::cerr << "Expected near: " << a << " vs " << b << " tolerance " << tolerance << "\n";
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
void editing()
{
	CamPath path;
	custom(path);
	path.Add(0, CamPathValue(0, 0, 0, 20, 170, 0, 60));
	path.Add(2, CamPathValue(30, 10, 5, 30, -170, 10, 80));
	path.Add(5, CamPathValue(50, 0, 10, 10, -120, 30, 90));
	path.SelectAll();
	for (int ch = 0; ch < 7; ++ch)
	{
		path.SetTangentMode((CamPath::Channel)ch, true, true, CamPath::TM_FREE);
		path.SetTangent((CamPath::Channel)ch, true, true, ch == 4 ? -180 : 3 + ch, ch == 4 ? 300 : 11 + ch);
		path.SetTangentWeight((CamPath::Channel)ch, true, true, 0.4, 1.8);
	}
	auto original = path.CaptureState();
	std::vector<CamPathValue> samples;
	for (int i = 0; i <= 300; ++i)
		samples.push_back(path.Eval(i / 60.0));
	check(path.Insert(1.25), "Insert subdivides edited seven-channel curves");
	for (int i = 0; i <= 300; ++i)
	{
		auto value = path.Eval(i / 60.0), expected = samples[i];
		near(value.X, expected.X, 1e-7);
		near(value.Y, expected.Y, 1e-7);
		near(value.Z, expected.Z, 1e-7);
		near(value.Fov, expected.Fov, 1e-7);
		near(std::abs(DotProduct(value.R, expected.R)), 1, 1e-8);
		for (int ch = 4; ch < 7; ++ch)
			near(CamPath::GetChannelValue(value, (CamPath::Channel)ch),
				 CamPath::GetChannelValue(expected, (CamPath::Channel)ch), 1e-7);
	}
	auto inserted = path.CaptureState();
	check(!path.Insert(1.25) && path.CaptureState() == inserted,
		  "Duplicate insertion leaves all data unchanged");
	path.RestoreState(original);
	check(path.CaptureState() == original, "Subdivision is a single reversible data edit");
	CamPath held;
	custom(held);
	held.Add(0, CamPathValue(0, 0, 0, 0, 0, 0, 60));
	held.Add(1, CamPathValue(10, 5, 0, 20, 30, 10, 80));
	held.Add(4, CamPathValue(40, 10, 0, 30, 60, 20, 90));
	check(held.SetKeyHold(1, 2), "Hold stores arrival and independent release");
	for (int i = 0; i <= 20; ++i)
	{
		auto value = held.Eval(1 + i / 20.0), arrival = held.Eval(1);
		near(value.X, arrival.X);
		near(value.Fov, arrival.Fov);
		near(std::abs(DotProduct(value.R, arrival.R)), 1);
	}
	check(held.GetSpeed(1 - 1e-5) < 0.01 && held.GetSpeed(2 + 1e-5) < 0.01,
		  "Smooth holds arrive and depart without velocity jumps");
	auto beforeBadHold = held.CaptureState();
	check(!held.SetKeyHold(1, 4) && held.CaptureState() == beforeBadHold,
		  "Hold cannot consume travel to a different next pose");
	check(held.Insert(1.5), "Insert can subdivide a held pose");
	near(held.Eval(1.9).X, 10);
	CamPath single;
	single.Add(10, CamPathValue(3, 4, 5, 0, 0, 0, 70));
	check(single.SetKeyHold(0, 12) && single.CanEval(), "A single held key forms a playable stationary shot");
	near(single.GetDuration(), 2);
	near(single.Eval(11).Y, 4);
	CamPath travel;
	travel.PositionInterpMethod_set(CamPath::DI_LINEAR);
	travel.FovInterpMethod_set(CamPath::DI_LINEAR);
	travel.RotationInterpMethod_set(CamPath::QI_SLINEAR);
	travel.Add(0, CamPathValue(0, 0, 0, 0, 0, 0, 90));
	travel.Add(1, CamPathValue(10, 0, 0, 0, 1, 0, 90));
	travel.Add(4, CamPathValue(110, 0, 0, 0, 11, 0, 90));
	check(travel.RetimeByDistance(10), "Distance retiming adjusts existing arrival times");
	near(travel.GetDuration(), 10);
	for (int i = 1; i < 100; ++i)
	{
		near(travel.Eval(i / 10.0).X, i * 1.1, 0.002);
		near(travel.GetSpeed(i / 10.0), 11, 0.02);
	}
	const auto travelState = travel.CaptureState();
	check(!travel.RetimeByDistance(-1) && travel.CaptureState() == travelState,
		  "Invalid retiming preserves the shot");
	auto file = std::filesystem::temp_directory_path() / "hlae-editing-roundtrip.xml";
	check(path.Save(file.wstring().c_str()), "Edited Euler winding saves");
	CamPath loaded;
	check(loaded.Load(file.wstring().c_str()), "Edited Euler winding loads");
	check(loaded.CaptureState() == path.CaptureState(), "Euler subdivision metadata round-trips");
	check(held.Save(file.wstring().c_str()) && loaded.Load(file.wstring().c_str()),
		  "Keyframe holds round-trip");
	check(loaded.CaptureState() == held.CaptureState(), "Arrival/release and held pose persist");
	check(travel.Save(file.wstring().c_str()) && loaded.Load(file.wstring().c_str()),
		  "Distance timing round-trips");
	near(loaded.GetSpeed(5), 11, 0.02);
	std::filesystem::remove(file);
}
void distanceAndHoldEdits()
{
	CamPath path;
	populate(path);
	path.SelectNone();
	path.SetTangentMode(CamPath::CH_Y, true, true, CamPath::TM_FREE);
	path.SetTangent(CamPath::CH_Y, true, true, -7, -7);
	path.SetTangentWeight(CamPath::CH_Y, true, true, 0.6, 1.4);
	path.SelectNone();
	auto source = path.CaptureState();
	check(path.RetimeByDistance(12), "Weighted nonlinear path supports distance retiming");
	auto target = path.CaptureState();
	CamPath raw;
	target.DistanceTiming = false;
	raw.RestoreState(target);
	auto a = source.Keys.begin(), b = target.Keys.begin();
	for (size_t segment = 1; segment < source.Keys.size(); ++segment)
	{
		auto nextA = std::next(a), nextB = std::next(b);
		CamPath original;
		original.RestoreState(source);
		for (int i = 0; i <= 100; ++i)
		{
			const double u = i / 100.0;
			auto before = original.Eval(a->first + (nextA->first - a->first) * u);
			auto after = raw.Eval(b->first + (nextB->first - b->first) * u);
			near(before.X, after.X, 1e-6);
			near(before.Y, after.Y, 1e-6);
			near(before.Z, after.Z, 1e-6);
			near(before.Fov, after.Fov, 1e-6);
			near(std::abs(DotProduct(before.R, after.R)), 1, 1e-6);
		}
		a = nextA;
		b = nextB;
	}
	double minSpeed = 1e30, maxSpeed = 0;
	for (int i = 1; i < 240; ++i)
	{
		double speed = path.GetSpeed(12 * i / 240.0);
		minSpeed = (std::min)(minSpeed, speed);
		maxSpeed = (std::max)(maxSpeed, speed);
	}
	check(minSpeed > 0 && maxSpeed / minSpeed < 1.03,
		  "Distance timing flows through weighted nonlinear intervals at consistent speed");
	CamPath held;
	custom(held);
	held.Add(0, CamPathValue(0, 0, 0, 0, 0, 0, 90));
	held.Add(2, CamPathValue(20, 0, 0, 0, 10, 0, 90));
	held.Add(6, CamPathValue(60, 0, 0, 0, 20, 0, 90));
	check(held.SetKeyHold(1, 3), "Hold added before duration edits");
	held.SetDuration(12);
	near(held.GetDuration(), 12);
	auto state = held.CaptureState();
	auto mid = std::next(state.Keys.begin());
	near(mid->first, 4);
	near(mid->second.HoldDuration, 2);
	held.SelectNone();
	held.SelectAdd(size_t(1), size_t(1));
	auto before = held.CaptureState();
	held.SetStart(11);
	check(held.CaptureState() == before, "Moving a held key through the next arrival is atomic and rejected");
	check(held.RetimeByDistance(14), "Distance timing supports held intervals");
	auto heldState = held.CaptureState();
	auto heldKey = std::next(heldState.Keys.begin());
	near(held.Eval(heldKey->first + 1).X, 20);
	check(held.GetSpeed(heldKey->first - 1e-5) < 0.05 &&
			  held.GetSpeed(heldKey->first + heldKey->second.HoldDuration + 1e-5) < 0.05,
		  "Distance-timed travel smoothly arrives at and leaves a hold");
	for (const char *bad : {"garbage", "nan", "-1", "2junk"})
	{
		const auto previous = held.CaptureState();
		std::string xml =
			"<campath><points><p t=\"0\" holdDuration=\"" + std::string(bad) + "\"/></points></campath>";
		check(!held.LoadFromString(xml) && held.CaptureState() == previous,
			  "Invalid hold data cannot replace the document");
	}
}
void collections()
{
	CamPath active;
	custom(active);
	active.Add(0, CamPathValue(0, 0, 0, 0, 0, 0, 90));
	active.Add(4, CamPathValue(40, 0, 0, 0, 0, 0, 90));
	CamPathCollection paths(active);
	check(!paths.IsDirty(), "Initial document is clean");
	paths.BeginEdit();
	check(paths.Rename("Opening & dolly"), "Named path accepts XML-sensitive text");
	paths.FinishEdit();
	check(paths.IsDirty() && paths.CanUndo(), "Path rename enters authoring history");
	check(paths.Undo() && paths.GetName(0) == "Path 1", "Rename undoes");
	check(paths.Undo(true) && paths.GetName(0) == "Opening & dolly", "Rename redoes");
	paths.BeginEdit();
	check(paths.NewPath("Close-up", true), "Duplicate copies an ordinary native path");
	check(paths.IsMuted(1), "Duplicate does not change playback until unmuted");
	paths.SetFlags(1, false, false, true);
	paths.FinishEdit();
	check(paths.GetCount() == 2 && paths.GetActive() == 1, "Duplicate becomes the editable shot");
	auto data = active.CaptureState();
	data.Keys.clear();
	data.Keys.emplace(2, CamPathValue(100, 0, 0, 0, 0, 0, 70));
	data.Keys.emplace(3, CamPathValue(110, 0, 0, 0, 0, 0, 70));
	paths.BeginEdit();
	active.RestoreState(data);
	paths.FinishEdit();
	active.Enabled_set(true);
	auto *playback = paths.GetPlaybackPath(2.5);
	check(playback && playback->Eval(2.5).Fov == 70, "Latest starting path wins overlaps");
	check(paths.Select(0), "Editing can select a different shot");
	playback = paths.GetPlaybackPath(2.5);
	check(playback && playback->Eval(2.5).Fov == 70, "Playback is independent of editing selection");
	playback = paths.GetPlaybackPath(1);
	check(playback && playback->Eval(1).Fov == 90, "Earlier shot plays in its own range");
	check(!paths.GetPlaybackPath(9), "Outside all shots returns control to the game");
	paths.FinishEdit();
	check(paths.Undo(), "Undo crosses shot selection without adding navigation steps");
	check(active.Enabled_get(), "Undo does not change playback ownership");
	check(paths.Undo(true), "Cross-path edit redoes");
	auto file = std::filesystem::temp_directory_path() / "hlae-path-collection.xml";
	check(paths.SaveAll(file.wstring().c_str()), "All named paths save together");
	check(!paths.IsDirty(), "Saving all records the document save point");
	const auto saved = paths.CaptureState();
	for (int i = 0; i < 10; ++i)
	{
		active.SelectNone();
		check(!paths.IsDirty() && !paths.IsDirty(paths.GetActive()), "Selection-only changes stay clean");
		active.SelectAll();
		check(!paths.IsDirty(), "Repeated selection does not change the saved document");
	}
	const bool wasEnabled = active.Enabled_get();
	active.Enabled_set(!wasEnabled);
	check(!paths.IsDirty(), "Playback enable is excluded from saved-content comparisons");
	active.Enabled_set(wasEnabled);
	active.SetFov(55);
	check(paths.IsDirty() && paths.IsDirty(paths.GetActive()), "Pose edits invalidate a clean dirty cache");
	paths.RestoreState(saved);
	check(!paths.IsDirty(), "Restoring saved content invalidates a dirty cache");
	paths.Rename("Temporary name");
	check(paths.IsDirty(paths.GetActive()), "Metadata edits remain dirty with cached native content");
	paths.RestoreState(saved);
	check(!paths.IsDirty(), "Restoring metadata clears dirty state");
	paths.SetFlags(0, !paths.IsMuted(0), paths.IsSolo(0), paths.IsVisible(0));
	check(paths.IsDirty() && paths.IsDirty(0), "Inactive path flags participate in dirty state");
	paths.RestoreState(saved);
	paths.Remove(0);
	check(paths.IsDirty(), "Removed saved paths remain dirty");
	paths.RestoreState(saved);
	check(!paths.IsDirty(), "Restoring a removed path cannot reuse a stale comparison");
	CamPath other;
	CamPathCollection loaded(other);
	check(loaded.LoadAll(file.wstring().c_str()), "Collection loads atomically");
	check(CamPathCollection::SameContent(saved, loaded.CaptureState()),
		  "Names and every native shot round-trip");
	check(!loaded.IsDirty(), "Loaded collection is clean");
	check(loaded.LoadAll(file.wstring().c_str(), true) && loaded.IsDirty(),
		  "Recovery requires saving the recovered work");
	check(loaded.GetCollectionFileName().empty(),
		  "Recovery never writes over an original project implicitly");
	check(paths.SaveOne(file.wstring().c_str()), "Selected shot saves as classic-compatible XML");
	CamPath one;
	check(one.Load(file.wstring().c_str()), "Save-one is an ordinary campath file");
	check(one.GetSize() == active.GetSize(), "Save-one contains the selected shot only");
	const auto beforeBad = paths.CaptureState();
	{
		std::ofstream out(file);
		out << "<campaths active=\"5\"><campath name=\"bad\"><points/></campath></campaths>";
	}
	check(!paths.LoadAll(file.wstring().c_str()) &&
			  CamPathCollection::SameContent(beforeBad, paths.CaptureState()),
		  "Malformed collection leaves every shot intact");
	paths.BeginEdit();
	check(paths.Remove(paths.GetActive()), "Named path can be removed");
	paths.FinishEdit();
	check(paths.GetCount() == 1 && paths.Undo() && paths.GetCount() == 2, "Removing a shot is reversible");
	std::filesystem::remove(file);
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
	command("auto", "minimumjerk");
	check(path.GetAutoTangentSettings().Method == AutoTangentMethod::MinimumJerk,
		  "Console selects minimum jerk");
	auto call = [&](std::initializer_list<const char *> values)
	{
		Args args;
		args.Values = values;
		MirvCampath_ConCommand(&args, IgnoreConsole, IgnoreConsole, &path, nullptr, nullptr, nullptr);
	};
	call({"mirv_campath", "modifier", "noise", "0", "2", "position", "4", "2"});
	check(path.GetModifiers().size() == 1, "Console creates range noise");
	call({"mirv_campath", "modifier", "set", "0", "seed", "17"});
	check(path.GetModifiers()[0].Seed == 17, "Console edits deterministic seed");
	const auto validModifier = path.CaptureState();
	call({"mirv_campath", "modifier", "set", "0", "frequency", "nan"});
	check(path.CaptureState() == validModifier, "Invalid console modifier retains data");
	call({"mirv_campath", "modifier", "clear"});
	check(path.GetModifiers().empty(), "Console clears modifiers");
	call({"mirv_campath", "modifier", "oscillation", "0", "2", "fov", "5", "2"});
	check(path.GetModifiers().size() == 1 &&
			  path.GetModifiers()[0].Type == CamPath::ModifierType::Oscillation,
		  "Console creates oscillation");
	call({"mirv_campath", "modifier", "envelope", "0", "point", "0.5", "0.25"});
	check(path.GetModifiers()[0].Envelope.size() == 3, "Console authors modifier strength keys");
	call({"mirv_campath", "modifier", "set", "0", "phase", "0.25"});
	near(path.GetModifiers()[0].Phase, .25);
	call({"mirv_campath", "modifier", "envelope", "0", "remove", "0.5"});
	check(path.GetModifiers()[0].Envelope.size() == 2, "Console removes interior envelope points");
	call({"mirv_campath", "modifier", "smooth", "0", "2", "position", "0.5"});
	check(path.GetModifiers().size() == 2 && path.GetModifiers()[1].Type == CamPath::ModifierType::Smooth,
		  "Console creates smoothing");
	call({"mirv_campath", "modifier", "set", "1", "window", "0.4"});
	near(path.GetModifiers()[1].Window, .4);
	call({"mirv_campath", "modifier", "set", "0", "type", "smooth"});
	check(path.GetModifiers()[0].Type == CamPath::ModifierType::Smooth &&
			  path.GetModifiers()[0].Amplitude[3] == 1,
		  "Console switches modifier type with validated filter strengths");
	call({"mirv_campath", "modifier", "spring", "0", "2", "angles", "0.5", "3"});
	check(path.GetModifiers().size() == 3 && path.GetModifiers()[2].Type == CamPath::ModifierType::Spring,
		  "Console creates spring response");
	call({"mirv_campath", "modifier", "clear"});
	call({"mirv_campath", "speedProfile", "point", "0", "0"});
	check(path.GetDistanceTiming() && path.GetSpeedProfile().front().Speed == 0,
		  "Console edits whole-path speed profile");
	call({"mirv_campath", "speedProfile", "point", "1", "0"});
	check(path.GetSpeedProfile().back().Speed == 1, "Zero-area speed command cannot replace valid ramp");
	call({"mirv_campath", "speedProfile", "reset"});
	check(path.GetSpeedProfile().empty(), "Console restores constant speed");
}
void independentTimingAndExport()
{
	CamPath path;
	custom(path);
	path.FovInterpMethod_set(CamPath::DI_LINEAR);
	path.Add(0, CamPathValue(0, 0, 0, 0, 0, 0, 60));
	path.Add(2, CamPathValue(100, 0, 0, 0, 90, 0, 100));
	const auto original = path.Eval(0.5);
	path.SetDistanceTiming(true);
	near(path.Eval(0.5).X, 25, 0.003);
	near(path.Eval(0.5).Fov, 70);
	near(std::abs(DotProduct(original.R, path.Eval(0.5).R)), 1);
	path.PositionInterpMethod_set(CamPath::DI_LINEAR);
	near(path.Eval(0.5).X, 25, 0.003);
	path.PositionInterpMethod_set(CamPath::DI_CUSTOM);
	near(path.Eval(0.5).X, 25, 0.003);
	path.SetDistanceTimingPose(true);
	check(path.Eval(0.5).Fov > 72, "Whole-pose distance timing remains available explicitly");
	path.SetDistanceTimingPose(false);
	std::vector<CamPathValue> before;
	for (int i = 0; i <= 100; ++i)
		before.push_back(path.Eval(i / 50.0));
	check(path.Insert(0.5), "Position-only distance path can be split without changing independent channels");
	for (int i = 0; i <= 100; ++i)
	{
		auto value = path.Eval(i / 50.0);
		near(value.X, before[i].X, 0.01);
		near(value.Fov, before[i].Fov, 1e-6);
		near(std::abs(DotProduct(value.R, before[i].R)), 1, 1e-6);
	}
	std::string xml;
	check(path.SaveToString(xml), "Independent timing saves");
	CamPath loaded;
	check(loaded.LoadFromString(xml) && !loaded.GetDistanceTimingPose(), "Position-only mode round-trips");
	auto mode = xml.find(" distanceTimingMode=\"position\"");
	check(mode != std::string::npos, "Explicit mode is written");
	xml.erase(mode, std::string(" distanceTimingMode=\"position\"").size());
	check(loaded.LoadFromString(xml) && loaded.GetDistanceTimingPose(),
		  "Older distance files retain their whole-pose timing");
	CamPath spin;
	custom(spin);
	spin.Add(10, CamPathValue(0, 0, 0, 100, 0, 0, 60));
	spin.Add(12, CamPathValue(40, 10, 0, 100, 720, 0, 100));
	check(spin.GetBegin().GetValue().HasEuler, "Capture preserves pitch beyond the quaternion Euler fold");
	near((++spin.GetBegin()).GetValue().GetEulerAngles().Yaw, 720);
	near(spin.Eval(11).GetEulerAngles().Yaw, 360, 1e-5);
	spin.SetAngles(0, 0, 30, false, false, true);
	near(spin.GetBegin().GetValue().GetEulerAngles().Pitch, 100);
	near((++spin.GetBegin()).GetValue().GetEulerAngles().Yaw, 720);
	CamPath turn;
	custom(turn);
	turn.Add(0, CamPathValue(0, 0, 0, 0, 720, 0, 90));
	turn.Add(1, CamPathValue(1, 0, 0, 0, 810, 0, 90));
	turn.Rotate(0, 90, 0);
	near(turn.GetBegin().GetValue().GetEulerAngles().Yaw, 810, 1e-5);
	CamPath wrap;
	custom(wrap);
	wrap.Add(0, CamPathValue(0, 0, 0, 0, 170, 0, 90));
	wrap.Add(1, CamPathValue(1, 0, 0, 0, -170, 0, 90));
	near(std::abs(wrap.Eval(0.5).GetEulerAngles().Yaw), 180, 1e-5);
	const auto authored = spin.CaptureState();
	auto file = std::filesystem::temp_directory_path() / "hlae-classic-export.xml";
	check(spin.ExportClassic(file.wstring().c_str(), 1.0 / 120), "Classic bake samples the actual evaluator");
	check(spin.CaptureState() == authored, "Export leaves keys and savepoints unchanged");
	CamPath baked;
	check(baked.Load(file.wstring().c_str()), "Baked XML uses the native reader");
	check(baked.PositionInterpMethod_get() == CamPath::DI_LINEAR &&
			  baked.RotationInterpMethod_get() == CamPath::QI_SLINEAR && !baked.HasKeyHolds() &&
			  !baked.GetDistanceTiming(),
		  "Classic file requires no custom evaluator or hold metadata");
	for (int i = 0; i <= 240; ++i)
	{
		auto a = spin.Eval(10 + i / 120.0), b = baked.Eval(10 + i / 120.0);
		near(a.X, b.X, 0.001);
		near(a.Fov, b.Fov, 0.001);
		near(std::abs(DotProduct(a.R, b.R)), 1, 1e-6);
	}
	std::ifstream input(file);
	std::string data((std::istreambuf_iterator<char>(input)), {});
	input.close();
	check(data.find("eulerPitch") == std::string::npos && data.find("tx_mode") == std::string::npos &&
			  data.find("distanceTiming") == std::string::npos,
		  "Classic export strips branch-specific extensions");
	check(!spin.ExportClassic(file.wstring().c_str(), 0) &&
			  !spin.ExportClassic(file.wstring().c_str(), 1e-12),
		  "Invalid or excessive sample requests are rejected");
	check(spin.ExportClassic(file.wstring().c_str(), 2),
		  "Fast spins are refined even at a low nominal sample rate");
	check(baked.Load(file.wstring().c_str()) && baked.GetSize() > 8,
		  "Rotation subdivision prevents full turns aliasing into a stationary camera");
	std::filesystem::remove(file);
}
void shotControlsAndSelectionCost()
{
	CamPath active;
	populate(active);
	CamPathCollection document(active);
	active.Enabled_set(true);
	document.BeginEdit();
	document.NewPath("Alternate", true);
	document.FinishEdit();
	check(document.IsMuted(1) && document.GetPlaybackIndex(1) == 0,
		  "Duplicating a path does not steal playback");
	document.BeginEdit();
	document.SetFlags(1, false, true, false);
	document.FinishEdit();
	check(document.GetPlaybackIndex(1) == 1 && !document.IsVisible(1),
		  "Solo and visibility are independent playback / drawing controls");
	document.SetFlags(1, true, true, false);
	check(document.GetPlaybackIndex(1) == document.GetCount(),
		  "Muting a solo path does not unexpectedly enable other takes");
	document.SetFlags(1, false, true, false);
	check(document.Undo() && document.IsMuted(1) && document.IsVisible(1), "Path controls undo together");
	check(document.Undo(true) && document.IsSolo(1) && !document.IsVisible(1), "Path controls redo together");
	auto file = std::filesystem::temp_directory_path() / "hlae-shot-flags.xml";
	check(document.SaveOne(file.wstring().c_str()), "Single-path flags save with its name");
	CamPath loadedPath;
	CamPathCollection loaded(loadedPath);
	check(loaded.LoadOne(file.wstring().c_str()) && loaded.IsSolo(0) && !loaded.IsVisible(0),
		  "Single-path flags round-trip");
	check(document.SaveAll(file.wstring().c_str()) && loaded.LoadAll(file.wstring().c_str()) &&
			  loaded.IsSolo(1) && !loaded.IsVisible(1),
		  "Collection flags round-trip");
	std::filesystem::remove(file);
	const auto geometry = active.GetGeometryRevision();
	for (int i = 0; i < 1000; ++i)
	{
		active.SelectNone();
		active.SelectAdd(size_t(i % 5), size_t(i % 5));
	}
	check(active.GetGeometryRevision() == geometry,
		  "Selection does not invalidate trajectory sampling or arc-length data");
	auto keys = active.CaptureState().Keys;
	for (auto &key : keys)
		key.second.Selected = !key.second.Selected;
	active.ReplaceKeys(keys);
	check(active.GetGeometryRevision() == geometry,
		  "Graph selection replacement also keeps geometry caches valid");
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

void reviewedRegressions()
{
	CamPath path;
	populate(path);
	CamPathCollection doc(path);
	const auto dir = std::filesystem::temp_directory_path();
	const auto a = dir / "hlae-save-a.xml", b = dir / "hlae-save-b.xml";
	check(doc.SaveOne(a.wstring().c_str()), "Save first target");
	doc.BeginEdit();
	path.SetPosition(100, 0, 0);
	doc.FinishEdit();
	check(doc.SaveOne(b.wstring().c_str()), "Save as new target");
	check(doc.Undo() && doc.GetFileName() == b.wstring(), "Undo retains current save destination");
	path.SetOffset(10);
	path.SelectNone();
	path.SelectAdd(size_t(1), size_t(1));
	path.Clear();
	near(path.GetOffset(), 10);
	check(path.GetSize() == 4, "Partial clear retains other keys and offset");
	doc.SetFlags(0, true, true, false);
	doc.Remove(0);
	check(!doc.IsMuted(0) && !doc.IsSolo(0) && doc.IsVisible(0) && !path.HasKeyHolds(),
		  "Last-path removal resets placeholder flags");
	populate(path);
	path.SetHold(true);
	check(doc.GetPlaybackIndex(-1, false) == 0, "Whole-path hold covers before-start time");
	doc.NewPath("Empty");
	check(doc.GetPlaybackIndex(-1, false) == 0, "Unused path does not change before-start hold");
	CamPath held;
	custom(held);
	held.Add(0, CamPathValue(0, 0, 0, 0, 0, 0, 90));
	held.Add(1, CamPathValue(0, 0, 0, 0, 0, 0, 90));
	held.Add(2, CamPathValue(10, 0, 0, 0, 0, 0, 90));
	check(held.SetKeyHold(0, 1), "Valid shared hold boundary");
	held.SelectNone();
	held.SelectAdd(size_t(1), size_t(1));
	const auto original = held.CaptureState();
	held.SetPosition(5, 0, 0);
	check(held.CaptureState() == original, "Legacy position edit rejects invalid held boundary atomically");
	held.SetAngles(0, 90, 0);
	check(held.CaptureState() == original, "Legacy angle edit validates held boundary");
	held.SetFov(110);
	check(held.CaptureState() == original, "Legacy FOV edit validates held boundary");
	std::string xml;
	held.SaveToString(xml);
	CamPath reload;
	check(reload.LoadFromString(xml), "Rejected edits leave reloadable data");
	std::filesystem::remove(a);
	std::filesystem::remove(b);
}
void minimumJerkAndModifiers()
{
	CamPath p;
	populate(p);
	AutoTangentSettings settings;
	settings.Method = AutoTangentMethod::MinimumJerk;
	check(p.SetAutoTangentSettings(settings), "Minimum-jerk profile accepted");
	for (double t : {1., 3., 4.})
	{
		const double e = 1e-4;
		const double left = (p.Eval(t).X - 2 * p.Eval(t - e).X + p.Eval(t - 2 * e).X) / (e * e),
					 right = (p.Eval(t + 2 * e).X - 2 * p.Eval(t + e).X + p.Eval(t).X) / (e * e);
		near(left, right, 0.05);
		check(p.GetSpeed(t) > 1, "Minimum jerk flows through ordinary keys");
	}
	std::vector<CamPathValue> before;
	for (int i = 0; i <= 200; ++i)
		before.push_back(p.Eval(8.0 * i / 200));
	check(p.Insert(2.2), "Quintic can be subdivided without a new key type");
	for (int i = 0; i <= 200; ++i)
	{
		near(p.Eval(8.0 * i / 200).X, before[i].X, 1e-7);
		near(p.Eval(8.0 * i / 200).Fov, before[i].Fov, 1e-7);
		near(std::abs(DotProduct(p.Eval(8.0 * i / 200).R, before[i].R)), 1, 1e-7);
	}
	std::string xml;
	p.SaveToString(xml);
	CamPath loaded;
	check(loaded.LoadFromString(xml), "Quintic derivatives round trip");
	for (int i = 0; i <= 100; ++i)
		near(loaded.Eval(8.0 * i / 100).X, p.Eval(8.0 * i / 100).X, 1e-7);
	const auto jerkOriginal = p.CaptureState();
	check(p.RetimeByDistance(12), "Minimum jerk supports distance retiming");
	near(p.GetDuration(), 12, 1e-6);
	CamPath originalJerk, retimedJerk;
	originalJerk.RestoreState(jerkOriginal);
	auto jerkRetimed = p.CaptureState();
	jerkRetimed.DistanceTiming = false;
	retimedJerk.RestoreState(jerkRetimed);
	auto leftBefore = jerkOriginal.Keys.cbegin(), leftAfter = jerkRetimed.Keys.cbegin();
	for (; std::next(leftBefore) != jerkOriginal.Keys.end(); ++leftBefore, ++leftAfter)
		for (int i = 0; i <= 40; ++i)
		{
			const double u = i / 40.0;
			auto a = originalJerk.Eval(leftBefore->first +
									   u * (std::next(leftBefore)->first - leftBefore->first)),
				 b = retimedJerk.Eval(leftAfter->first +
									  u * (std::next(leftAfter)->first - leftAfter->first));
			near(a.X, b.X, 1e-6);
			near(a.Y, b.Y, 1e-6);
			near(a.Z, b.Z, 1e-6);
			near(a.Fov, b.Fov, 1e-6);
			near(std::abs(DotProduct(a.R, b.R)), 1, 1e-6);
		}

	CamPath weightedJerk;
	weightedJerk.RestoreState(jerkOriginal);
	check(weightedJerk.BakeTangents(), "Quintic tangents bake");
	weightedJerk.SelectAll();
	weightedJerk.SetTangentWeight(CamPath::CH_X, true, true, 0.5, 1.5);
	std::vector<double> weightedBefore;
	for (int i = 0; i <= 100; ++i)
		weightedBefore.push_back(weightedJerk.Eval(i * .08).X);
	check(weightedJerk.Insert(2.4), "Weighted fallback inserts as the actual cubic evaluator");
	for (int i = 0; i <= 100; ++i)
		near(weightedJerk.Eval(i * .08).X, weightedBefore[i], 1e-6);
	CamPath noise;
	populate(noise);
	const auto authored = noise.CaptureState().Keys;
	CamPath::NoiseModifier m;
	m.Begin = 0.5;
	m.End = 5;
	m.FadeIn = 0.5;
	m.FadeOut = 1;
	m.Amplitude[0] = 5;
	m.Amplitude[4] = 2;
	m.Frequency = 2.3;
	check(noise.SetModifiers({m}), "Range noise accepted");
	near(noise.Eval(0).X, noise.EvalUnmodified(0).X);
	near(noise.Eval(5).X, noise.EvalUnmodified(5).X);
	const auto sample = noise.Eval(2.1);
	noise.Eval(4.9);
	check(noise.Eval(2.1) == sample, "Noise scrubbing is deterministic");
	check(std::abs(sample.X - noise.EvalUnmodified(2.1).X) > 0.01, "Noise affects selected range/channel");
	near(sample.Fov, noise.EvalUnmodified(2.1).Fov);
	check(noise.CaptureState().Keys == authored, "Modifiers retain authored key data");
	noise.SaveToString(xml);
	check(loaded.LoadFromString(xml), "Noise saves and loads");
	check(loaded.Eval(2.1) == sample, "Noise playback survives file round trip");
	m.End = m.Begin;
	const auto valid = noise.CaptureState();
	check(!noise.SetModifiers({m}) && noise.CaptureState() == valid,
		  "Invalid modifier range is rejected atomically");
	CamPath held;
	custom(held);
	held.Add(0, CamPathValue(0, 0, 0, 0, 0, 0, 90));
	check(held.SetKeyHold(0, 4), "Stationary shot hold");
	m.Begin = 0;
	m.End = 4;
	m.Amplitude[0] = 3;
	check(held.SetModifiers({m}), "Explicit noise can affect a hold");
	check(std::abs(held.Eval(2).X) > 0.001, "Held base receives selected noise");
	near(held.EvalUnmodified(2).X, 0);
	CamPath classic;
	for (int i = 0; i < 5; ++i)
		classic.Add(i, CamPathValue(i * i * 10, i * 5, 0, 0, i * 20, 0, 90 + i * 5));
	std::vector<CamPathValue> classicValues;
	for (int i = 0; i <= 100; ++i)
		classicValues.push_back(classic.Eval(i * .04));
	check(classic.MakeScalarCurvesEditable() && classic.RotationInterpMethod_get() == CamPath::QI_DEFAULT,
		  "Classic scalar editing preserves quaternion backend");
	for (int i = 0; i <= 100; ++i)
	{
		near(classic.Eval(i * .04).X, classicValues[i].X, 1e-7);
		near(classic.Eval(i * .04).Fov, classicValues[i].Fov, 1e-7);
		near(std::abs(DotProduct(classic.Eval(i * .04).R, classicValues[i].R)), 1, 1e-7);
	}
	const auto file = std::filesystem::temp_directory_path() / "hlae-noise-bake.xml";
	check(noise.ExportClassic(file.wstring().c_str(), 1.0 / 120), "Modifiers bake into classic XML");
	CamPath baked;
	check(baked.Load(file.wstring().c_str()), "Noise bake reloads as classic linear data");
	check(baked.GetModifiers().empty() && baked.GetSpeedProfile().empty(),
		  "Classic bake contains no editor extensions");
	for (int i = 0; i <= 240; ++i)
	{
		const double t = 8.0 * i / 240;
		near(baked.Eval(t).X, noise.Eval(t).X, 0.01);
	}
	std::filesystem::remove(file);
	CamPath jerkHold;
	populate(jerkHold);
	jerkHold.SetAutoTangentSettings(settings);
	check(jerkHold.SetKeyHold(1, 1.5), "Minimum-jerk hold accepted");
	near(jerkHold.Eval(1.2).X, 30);
	check(jerkHold.GetSpeed(1 - 1e-5) < 0.01 && jerkHold.GetSpeed(1.5 + 1e-5) < 0.01,
		  "Quintic holds have smooth arrival and release");
}
void speedRamps()
{
	CamPath p;
	populate(p);
	const auto originals = p.CaptureState();
	std::vector<CamPath::SpeedKey> profile{{0, 0}, {0.25, 1}, {0.75, 1}, {1, 0}};
	check(p.ApplySpeedProfile(profile, 10), "Whole-path speed ramp retimes existing keys");
	near(p.GetDuration(), 10, 1e-6);
	check(p.GetSpeed(0.001) < p.GetSpeed(4) * 0.1 && p.GetSpeed(9.999) < p.GetSpeed(4) * 0.1,
		  "Ramp eases at path endpoints");
	for (auto it = p.GetBegin(); it != p.GetEnd(); ++it)
		if (it.GetTime() > 0 && it.GetTime() < 10)
		{
			const double e = 1e-3;
			check(p.GetSpeed(it.GetTime()) > 1, "Speed profile does not stop at ordinary camera keys");
			near(p.GetSpeed(it.GetTime() - e), p.GetSpeed(it.GetTime() + e), 0.5);
		}
	std::string xml;
	p.SaveToString(xml);
	CamPath loaded;
	check(loaded.LoadFromString(xml), "Speed profile round trip");
	for (int i = 0; i <= 100; ++i)
		near(p.Eval(i * .1).X, loaded.Eval(i * .1).X, 1e-6);
	const auto valid = p.CaptureState();
	check(!p.ApplySpeedProfile({{0, 0}, {1, 0}}, 10) && p.CaptureState() == valid,
		  "Zero-area speed ramp rejected");
	check(!p.ApplySpeedProfile({{0, 1}, {0.5, -1}, {1, 1}}, 10), "Negative speed rejected");
	for (int i = 0; i < 12; ++i)
		check(p.ApplySpeedProfile(i % 2 ? profile : std::vector<CamPath::SpeedKey>{{0, 1}, {0.5, 2}, {1, 1}},
								  10),
			  "Repeated live ramps remain retimable");
	CamPath source;
	source.RestoreState(originals);
	p.SetDistanceTiming(false);
	auto retimed = p.CaptureState();
	// Ramping may change arrivals, but authored positions and key count stay intact.
	check(p.GetSize() == source.GetSize(), "Speed ramps preserve native key count");
	auto originalKey = originals.Keys.begin();
	for (const auto &key : retimed.Keys)
	{
		near(key.second.X, originalKey->second.X);
		++originalKey;
	}
}

void reviewBoundaryRegressions()
{
	CamPath p;
	custom(p);
	p.Add(0, CamPathValue(0, 0, 0, 0, 0, 0, 90));
	p.Add(2, CamPathValue(10, 0, 0, 0, 0, 0, 90));
	const std::vector<CamPath::SpeedKey> narrow{{0, 0}, {0.500001, 0}, {0.500002, 1}, {0.500003, 0}, {1, 0}};
	check(p.ApplySpeedProfile(narrow, 2), "Narrow positive-area ramp is usable");
	near(p.Eval(0.5).X, 0);
	near(p.Eval(1.5).X, 10);
	near(p.GetDuration(), 2);
	std::string xml;
	check(p.SaveToString(xml), "Narrow ramp serializes");
	CamPath loaded;
	check(loaded.LoadFromString(xml), "Accepted narrow ramp reloads");
	for (int i = 0; i <= 80; ++i)
	{
		double t = 1.000002 + i * 0.00000005;
		near(p.Eval(t).X, loaded.Eval(t).X, 1e-6);
	}
	const auto valid = p.CaptureState();
	for (double invalid : {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
						   std::numeric_limits<double>::quiet_NaN()})
		check(!p.SetOffset(invalid) && p.CaptureState() == valid,
			  "Non-finite offset is rejected without mutation");
	for (const char *invalid : {"nan", "inf", "1e999", "junk", "2junk"})
	{
		auto bad = xml;
		const auto root = bad.find("<campath ");
		bad.insert(root + 9, std::string("offset=\"") + invalid + "\" ");
		const auto previous = loaded.CaptureState();
		check(!loaded.LoadFromString(bad) && loaded.CaptureState() == previous, "Bad XML offsets are atomic");
	}
	auto invalid = valid;
	invalid.Offset = std::numeric_limits<double>::infinity();
	p.RestoreState(invalid);
	check(!p.SaveToString(xml), "Invalid state cannot be serialized");
	p.RestoreState(valid);
	CamPath crowded;
	custom(crowded);
	crowded.PositionInterpMethod_set(CamPath::DI_LINEAR);
	crowded.Add(1e8, CamPathValue(0, 0, 0, 0, 0, 0, 90));
	crowded.Add(1e8 + 1, CamPathValue(1e-8, 0, 0, 0, 0, 0, 90));
	crowded.Add(1e8 + 2, CamPathValue(1e8, 0, 0, 0, 0, 0, 90));
	const auto crowdedState = crowded.CaptureState();
	check(!crowded.RetimeByDistance(2) && crowded.CaptureState() == crowdedState,
		  "Retiming cannot drop keys when timestamps lose precision");
	CamPath fov;
	custom(fov);
	fov.Add(0, CamPathValue(0, 0, 0, 0, 0, 0, 200));
	fov.Add(2, CamPathValue(10, 0, 0, 0, 0, 0, 200));
	CamPath::NoiseModifier noise;
	noise.Begin = 0;
	noise.End = 2;
	noise.Amplitude[3] = 20;
	check(fov.SetModifiers({noise}), "Wide-FOV modifier accepted");
	near(fov.Eval(0).Fov, 200);
	near(fov.Eval(1e-6).Fov, 200, 1e-8);
	for (int i = 0; i <= 100; ++i)
		check(fov.Eval(i * .02).Fov <= 200 + 1e-9 && fov.Eval(i * .02).Fov >= 0.01 - 1e-9,
			  "Noise respects limits without snapping the wide base");
	fov.SetFov(90);
	noise.Amplitude[3] = 500;
	fov.SetModifiers({noise});
	for (int i = 0; i <= 100; ++i)
		check(fov.Eval(i * .02).Fov >= 0.01 && fov.Eval(i * .02).Fov <= 179,
			  "Ordinary FOV noise remains bounded");
}
void transformedCurves()
{
	for (int method = 0; method < 4; ++method)
	{
		CamPath original;
		populate(original);
		AutoTangentSettings settings;
		settings.Method = (AutoTangentMethod)method;
		original.SetAutoTangentSettings(settings);
		CamPath rotated;
		rotated.RestoreState(original.CaptureState());
		check(rotated.Rotate(0, 30, 0), "Global profile rotates with effective derivatives");
		const double c = std::cos(30 * 3.14159265358979323846 / 180), sn = 0.5;
		// Original bounds are X 0..240 and Y 0..80.
		for (int i = 0; i <= 100; ++i)
		{
			const double t = i * .08;
			auto a = original.Eval(t), b = rotated.Eval(t);
			near(b.X, 120 + c * (a.X - 120) - sn * (a.Y - 40), 1e-6);
			near(b.Y, 40 + sn * (a.X - 120) + c * (a.Y - 40), 1e-6);
			near(b.Z, a.Z, 1e-6);
			near(b.Fov, a.Fov, 1e-6);
		}
	}
	CamPath weighted;
	populate(weighted);
	weighted.SelectAll();
	for (int ch = 0; ch < 3; ++ch)
	{
		weighted.SetTangentMode((CamPath::Channel)ch, true, true, CamPath::TM_FREE);
		weighted.SetTangent((CamPath::Channel)ch, true, true, 8 * (ch + 1), 8 * (ch + 1));
		weighted.SetTangentWeight((CamPath::Channel)ch, true, true, 0.5, 1.5);
	}
	CamPath source;
	source.RestoreState(weighted.CaptureState());
	check(weighted.AnchorTransform(0, 0, 0, 0, 0, 0, 100, 200, 300, 0, 30, 0),
		  "Shared weighted time handles anchor without distortion");
	for (int i = 0; i <= 100; ++i)
	{
		auto a = source.Eval(i * .08), b = weighted.Eval(i * .08);
		near(b.X, 100 + std::sqrt(3.0) / 2 * a.X - 0.5 * a.Y, 1e-6);
		near(b.Y, 200 + 0.5 * a.X + std::sqrt(3.0) / 2 * a.Y, 1e-6);
		near(b.Z, 300 + a.Z, 1e-6);
	}
	CamPath mixed;
	populate(mixed);
	mixed.SelectAll();
	mixed.SetTangentMode(CamPath::CH_X, true, true, CamPath::TM_FREE);
	mixed.SetTangent(CamPath::CH_X, true, true, 8, 8);
	mixed.SetTangentWeight(CamPath::CH_X, true, true, 0.5, 1.5);
	const auto before = mixed.CaptureState();
	check(!mixed.Rotate(0, 30, 0) && mixed.CaptureState() == before,
		  "Incompatible mixed time weights reject atomically");
	check(mixed.Rotate(0, 90, 0), "Axis permutation also permutes independent time weights");
	CamPath beforeMixed;
	beforeMixed.RestoreState(before);
	for (int i = 0; i <= 100; ++i)
	{
		auto a = beforeMixed.Eval(i * .08), b = mixed.Eval(i * .08);
		near(b.X, 160 - a.Y, 1e-6);
		near(b.Y, a.X - 80, 1e-6);
	}
	CamPath classic;
	for (int i = 0; i < 5; ++i)
		classic.Add(i, CamPathValue(10 * i, 4 * i, 2 * i, i * 7, i * 15, i * 3, 90));
	CamPath classicBefore;
	classicBefore.RestoreState(classic.CaptureState());
	check(classic.AnchorTransform(0, 0, 0, 0, 0, 0, 20, 30, 40, 10, 20, 30),
		  "Classic quaternion path anchors");
	const auto q = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(10, 20, 30)));
	for (int i = 0; i <= 80; ++i)
		near(std::abs(DotProduct(classic.Eval(i * .05).R, q * classicBefore.Eval(i * .05).R)), 1, 1e-6);
	CamPath single;
	single.Add(1, CamPathValue(0, 0, 0, 0, 0, 0, 90));
	check(single.AnchorTransform(0, 0, 0, 0, 0, 0, 10, 20, 30, 0, 0, 0),
		  "Unevaluable captured pose remains transformable");
	near(single.GetBegin().GetValue().X, 10);
}
void nextEditingRegressions()
{
	CamPath p;
	populate(p);
	const auto position = p.GetPositionRevision(), geometry = p.GetGeometryRevision();
	p.SetFov(100);
	check(p.GetPositionRevision() == position && p.GetGeometryRevision() > geometry,
		  "FOV edits retain positional trajectory caches");
	p.FovInterpMethod_set(CamPath::DI_LINEAR);
	check(p.GetPositionRevision() == position,
		  "FOV interpolation changes retain positional trajectory samples");
	p.SetOffset(5);
	check(p.GetPositionRevision() == position, "Offset changes retain path-relative positional samples");
	p.SelectNone();
	p.SelectAdd(size_t(1), size_t(1));
	p.SetTangentWeight(CamPath::CH_X, true, true, .75, 1.25);
	const auto before = p.GetRevision();
	check(p.EditTangent(1, CamPath::CH_X, true, 12, 1.5, true, true), "Atomic linked handle edit succeeds");
	check(p.GetRevision() == before + 1, "One handle edit emits one change notification");
	double in, wi, out, wo;
	p.GetTangent(CamPath::CH_X, 1, false, in, wi);
	p.GetTangent(CamPath::CH_X, 1, true, out, wo);
	near(in, 12);
	near(out, 12);
	near(wi, .75);
	near(wo, 1.5);
	const auto valid = p.CaptureState();
	check(!p.EditTangent(1, CamPath::CH_X, true, std::numeric_limits<double>::quiet_NaN(), 1, true, false) &&
			  p.CaptureState() == valid,
		  "Invalid handle edit leaves the entire path intact");
	check(!p.GetTangent((CamPath::Channel)-1, 0, true, out, wo) &&
			  !p.GetAcceleration((CamPath::Channel)7, 0, true, out),
		  "Invalid tangent channels reject safely");
	check(p.SetKeyHold(2, 3.5), "Hold expands the tangent index map");
	for (size_t i = 0; i < p.GetSize(); ++i)
		check(p.GetTangent(CamPath::CH_X, i, false, in, wi) && p.GetTangent(CamPath::CH_X, i, true, out, wo),
			  "Indexed tangents remain available across synthetic hold keys");
	check(p.DeleteSelected(), "Removing an indexed key succeeds");
	for (size_t i = 0; i < p.GetSize(); ++i)
		check(p.GetTangent(CamPath::CH_X, i, true, out, wo), "Tangent lookup updates after deletion");
	p.SetDistanceTiming(true);
	const auto poseRevision = p.GetGeometryRevision(), spatialRevision = p.GetPositionRevision();
	p.SetDistanceTimingPose(true);
	check(p.GetGeometryRevision() > poseRevision && p.GetPositionRevision() == spatialRevision,
		  "Pose-clock changes refresh graph evaluation without invalidating spatial geometry");
	CamPath line;
	custom(line);
	line.PositionInterpMethod_set(CamPath::DI_LINEAR);
	line.Add(0, CamPathValue(0, 0, 0, 0, 0, 0, 90));
	line.Add(2, CamPathValue(100, 0, 0, 0, 0, 0, 90));
	const auto samples = line.SampleInterval(0, 2, .01, 1024);
	check(samples.size() >= 2 && samples.size() < 32,
		  "Straight trajectory sampling avoids 1024 evaluations per interval");
	near(samples.front().first, 0);
	near(samples.back().first, 2);
	CamPath::NoiseModifier noise;
	noise.Begin = 0;
	noise.End = 2;
	noise.Amplitude[1] = 5;
	line.SetModifiers({noise});
	const auto curved = line.SampleInterval(0, 2, .02, 512);
	check(curved.size() <= 512 && curved.size() > samples.size(),
		  "Trajectory sampling adapts to visible noise within its budget");
	for (int i = 1; i < 400; ++i)
	{
		const double t = i * .005;
		auto right = std::lower_bound(curved.begin(), curved.end(), t,
									  [](const auto &sample, double time) { return sample.first < time; });
		if (right == curved.begin() || right == curved.end())
			continue;
		const auto left = std::prev(right);
		const double u = (t - left->first) / (right->first - left->first);
		near(line.Eval(t).Y, left->second.Y + (right->second.Y - left->second.Y) * u, .05);
	}
}
void moreModifiers()
{
	CamPath p;
	custom(p);
	p.PositionInterpMethod_set(CamPath::DI_LINEAR);
	p.Add(0, CamPathValue(0, 0, 0, 0, 0, 0, 90));
	p.Add(4, CamPathValue(0, 0, 0, 0, 720, 0, 110));
	const auto authored = p.CaptureState().Keys;
	CamPath::NoiseModifier wave;
	wave.Type = CamPath::ModifierType::Oscillation;
	wave.Begin = 0;
	wave.End = 4;
	wave.Phase = .25;
	wave.Frequency = 1;
	wave.Amplitude[0] = 10;
	check(p.SetModifiers({wave}), "Oscillation is accepted");
	near(p.Eval(1).X, 10);
	near(p.Eval(1.5).X, -10);
	near(p.Eval(0).X, 0);
	near(p.Eval(4).X, 0);
	wave.Envelope = {{0, 0}, {.25, 1}, {.5, 0}, {1, 1}};
	check(p.SetModifiers({wave}), "Modifier strength keys accepted");
	near(p.Eval(1).X, 10);
	near(p.Eval(2).X, 0);
	wave.Strength = .5;
	check(p.SetModifiers({wave}), "Global modifier strength accepted");
	near(p.Eval(1).X, 5);
	CamPath::NoiseModifier noise;
	noise.Begin = 0;
	noise.End = 4;
	noise.Frequency = 3;
	noise.Amplitude[0] = 4;
	noise.Octaves = 4;
	noise.Roughness = .65;
	check(p.SetModifiers({noise}), "Layered noise accepted");
	const auto noisy = p.Eval(1.37);
	p.Eval(3.8);
	check(p.Eval(1.37) == noisy, "Layered noise is independent of evaluation order");
	for (int i = 0; i <= 400; ++i)
		check(std::abs(p.Eval(i * .01).X) <= 4 + 1e-9, "Layered noise is normalized to its amplitude");
	CamPath::NoiseModifier smooth;
	smooth.Type = CamPath::ModifierType::Smooth;
	smooth.Begin = 0;
	smooth.End = 4;
	smooth.Window = .3;
	smooth.Amplitude[0] = 1;
	smooth.Amplitude[5] = 1;
	check(p.SetModifiers({noise, smooth}), "Whole-path smoothing accepted");
	double filteredEnergy = 0, noiseEnergy = 0;
	const auto sample = p.Eval(1.37);
	p.Eval(2.8);
	check(p.Eval(1.37) == sample, "Smoothing is deterministic during backward scrubbing");
	for (int i = 31; i < 370; ++i)
	{
		double t = i * .01;
		filteredEnergy += std::pow(p.Eval(t).X - p.Eval(t - .01).X, 2);
	}
	p.SetModifiers({noise});
	for (int i = 31; i < 370; ++i)
	{
		double t = i * .01;
		noiseEnergy += std::pow(p.Eval(t).X - p.Eval(t - .01).X, 2);
	}
	check(filteredEnergy < noiseEnergy * .5, "Smoothing reduces high-frequency movement");
	p.SetModifiers({noise, smooth});
	const double noiseThenSmooth = p.Eval(1.37).X;
	p.SetModifiers({smooth, noise});
	check(std::abs(p.Eval(1.37).X - noiseThenSmooth) > .01, "Modifier ordering changes the result");
	CamPath springPath;
	custom(springPath);
	springPath.PositionInterpMethod_set(CamPath::DI_LINEAR);
	for (const auto &key : std::vector<std::pair<double, double>>{{0, 0}, {1, 0}, {1.1, 100}, {4, 100}})
		springPath.Add(key.first, CamPathValue(key.second, 0, 0, 0, 0, 0, 90));
	CamPath::NoiseModifier spring;
	spring.Type = CamPath::ModifierType::Spring;
	spring.Begin = 0;
	spring.End = 4;
	spring.Frequency = 1;
	spring.Damping = .3;
	spring.Amplitude[0] = 1;
	check(springPath.SetModifiers({spring}), "Spring response accepted");
	double peak = 0;
	for (int i = 110; i < 300; ++i)
		peak = (std::max)(peak, springPath.Eval(i * .01).X);
	check(peak > 110, "Underdamped spring has controlled overshoot");
	near(springPath.Eval(3.5).X, 100, 2);
	const auto springSample = springPath.Eval(1.5);
	springPath.Eval(3);
	check(springPath.Eval(1.5) == springSample, "Spring results do not depend on playback direction");
	for (double damping : {.05, 1.0, 2.0})
	{
		spring.Damping = damping;
		springPath.SetModifiers({spring});
		for (int i = 0; i <= 100; ++i)
			check(std::isfinite(springPath.Eval(i * .04).X),
				  "Spring remains finite across supported damping modes");
	}
	p.SetModifiers({wave, noise, smooth, spring});
	std::string xml;
	check(p.SaveToString(xml), "Every modifier type serializes");
	CamPath loaded;
	check(loaded.LoadFromString(xml), "Every modifier type reloads");
	check(loaded.GetModifiers() == p.GetModifiers(), "Modifier parameters and envelopes round-trip exactly");
	for (int i = 0; i <= 200; ++i)
	{
		near(loaded.Eval(i * .02).X, p.Eval(i * .02).X, 1e-7);
		near(loaded.Eval(i * .02).GetEulerAngles().Yaw, p.Eval(i * .02).GetEulerAngles().Yaw, 1e-7);
	}
	check(p.CaptureState().Keys == authored, "All modifiers preserve authored camera keys");
	CamPath pole;
	for (int i = 0; i < 5; ++i)
		pole.Add(i, CamPathValue(0, 0, 0, 80 + i * 5, 10, 20, 90));
	CamPath::Modifier poleFilter = smooth;
	poleFilter.Amplitude[0] = poleFilter.Amplitude[5] = 0;
	poleFilter.Amplitude[4] = .5;
	check(pole.SetModifiers({poleFilter}),
		  "Classic quaternion orientations accept a selected Euler filter channel");
	auto previousPole = pole.Eval(.3).R;
	for (int i = 31; i < 370; ++i)
	{
		auto next = pole.Eval(i * .01).R;
		check(std::abs(DotProduct(previousPole, next)) > .999998,
			  "Filtering near a pitch pole preserves equivalent Euler continuity");
		previousPole = next;
	}
	std::vector<CamPath::Modifier> tooManyFilters(9, smooth);
	check(!p.SetModifiers(tooManyFilters), "Active filter stages have a bounded memory budget");

	const auto valid = p.CaptureState();
	auto invalid = wave;
	invalid.Envelope = {{0, 1}, {.5, 1}, {.5, 0}, {1, 1}};
	check(!p.SetModifiers({invalid}) && p.CaptureState() == valid,
		  "Colliding envelope points reject atomically");
	invalid = noise;
	invalid.Octaves = 9;
	check(!p.SetModifiers({invalid}), "Invalid octave count rejected");
	invalid = smooth;
	invalid.CameraSpace = true;
	check(!p.SetModifiers({invalid}), "Unsupported filter coordinate mode rejected");
	invalid = spring;
	invalid.Damping = 0;
	check(!p.SetModifiers({invalid}), "Zero spring damping rejected");
	invalid = wave;
	invalid.Strength = std::numeric_limits<double>::quiet_NaN();
	auto state = valid;
	state.Modifiers = {invalid};
	p.RestoreState(state);
	check(!p.SaveToString(xml), "Invalid restored modifier data cannot be saved");
	p.RestoreState(valid);
	p.SaveToString(xml);
	auto badXml = xml;
	const auto tag = badXml.find("<oscillation");
	badXml.replace(tag + 1, 11, "badModifier");
	check(!loaded.LoadFromString(badXml), "Unknown modifier nodes reject instead of silently losing effects");
}

void modifierPersistenceAndExport()
{
	CamPath path;
	custom(path);
	path.Add(0, CamPathValue(0, 0, 0, 0, 0, 0, 90));
	path.Add(4, CamPathValue(100, 0, 0, 0, 60, 0, 100));
	CamPath::Modifier noise;
	noise.Begin = 0;
	noise.End = 4;
	noise.Amplitude[0] = 3;
	noise.Octaves = 3;
	CamPath::Modifier wave = noise;
	wave.Type = CamPath::ModifierType::Oscillation;
	wave.Amplitude[0] = 0;
	wave.Amplitude[3] = 2;
	wave.Phase = .25;
	wave.Envelope = {{0, 0}, {.5, 1}, {1, 0}};
	CamPath::Modifier smooth = noise;
	smooth.Type = CamPath::ModifierType::Smooth;
	smooth.Amplitude[0] = .8;
	CamPath::Modifier spring = smooth;
	spring.Type = CamPath::ModifierType::Spring;
	spring.Amplitude[0] = .4;
	check(path.SetModifiers({noise, wave, smooth, spring}), "Mixed modifier stack accepted for persistence");
	CamPathCollection collection(path);
	const auto original = path.GetModifiers();
	collection.BeginEdit();
	auto edited = original;
	edited[1].Envelope[1].Value = .2;
	path.SetModifiers(edited);
	collection.FinishEdit();
	check(collection.Undo() && path.GetModifiers() == original,
		  "Undo restores modifier envelopes and settings");
	check(collection.Undo(true) && path.GetModifiers() == edited, "Redo restores modifier envelope edits");
	const auto file = std::filesystem::temp_directory_path() / "hlae-range-collection.xml";
	check(collection.NewPath("Alternate with modifiers", true) && collection.SaveAll(file.wstring().c_str()),
		  "Named collections save all modifier types");
	CamPath restored;
	CamPathCollection loaded(restored);
	check(loaded.LoadAll(file.wstring().c_str()) && restored.GetModifiers() == edited,
		  "Collection reload retains active modifier data");
	check(loaded.GetPath(0)->GetModifiers() == edited, "Inactive collection paths retain modifier data");
	std::filesystem::remove(file);
	const auto classic = std::filesystem::temp_directory_path() / "hlae-range-classic.xml";
	check(path.ExportClassic(classic.wstring().c_str(), 1.0 / 120),
		  "All modifier types bake through classic export");
	CamPath baked;
	check(baked.Load(classic.wstring().c_str()), "Baked modifiers reload in classic format");
	check(baked.GetModifiers().empty(), "Classic export requires no modifier runtime");
	for (int i = 0; i <= 200; ++i)
	{
		const double time = i * .02;
		near(baked.Eval(time).X, path.Eval(time).X, .05);
		near(baked.Eval(time).Fov, path.Eval(time).Fov, .02);
	}
	std::filesystem::remove(classic);
}
} // namespace
int main()
{
	try
	{
		continuity();
		tangents();
		profiles();
		editing();
		distanceAndHoldEdits();
		collections();
		consoleProfiles();
		independentTimingAndExport();
		shotControlsAndSelectionCost();
		files();
		reviewedRegressions();
		minimumJerkAndModifiers();
		speedRamps();
		reviewBoundaryRegressions();
		transformedCurves();
		nextEditingRegressions();
		moreModifiers();
		modifierPersistenceAndExport();
		std::cout << checks << " TF2 camera checks passed\n";
	}
	catch (const std::exception &e)
	{
		std::cerr << "After " << checks << " checks: " << e.what() << "\n";
		return 1;
	}
}
