// Cinematic project persistence. Value data only: engine pointers and preview state are never saved.
namespace
{
std::string g_CameraBakeReport;
bool ProjectPose(std::ostream &out, const advancedfx::CameraPose &p)
{
	if (!p.Valid())
		return false;
	for (double v : p.Position)
		out << v << ' ';
	out << p.Rotation.W << ' ' << p.Rotation.X << ' ' << p.Rotation.Y << ' ' << p.Rotation.Z << ' ' << p.Fov << '\n';
	return bool(out);
}
bool ProjectPose(std::istream &in, advancedfx::CameraPose &p)
{
	for (double &v : p.Position)
		if (!(in >> v))
			return false;
	return bool(in >> p.Rotation.W >> p.Rotation.X >> p.Rotation.Y >> p.Rotation.Z >> p.Fov) && p.Valid();
}
bool ProjectRig(std::ostream &out, const NamedCameraRig &r, int depth = 0)
{
	if (depth > 1)
		return false;
	out << r.Id << ' ' << r.AttachMode << ' ' << r.Rig.Attached << ' ' << r.Rig.InheritRotation << ' '
		<< r.EntityAddress << ' ' << r.TargetHandle << ' ' << r.CacheIdentity << ' ' << std::quoted(r.DemoKey) << ' '
		<< std::quoted(r.TargetModel) << '\n';
	if (!ProjectPose(out, r.Rig.Pose) || !ProjectPose(out, r.LastWorld))
		return false;
	out << bool(r.Target) << '\n';
	if (r.Target && !Mirv_Tf2_SaveCamera(out, r.Target.get()))
		return false;
	out << bool(r.Aim) << '\n';
	if (r.Aim)
	{
		auto &a = *r.Aim;
		if (a.Source_get() && !a.HasTf2EntitySource())
			return false;
		const auto &p = a.GetTargetPoint();
		out << a.Active << ' ' << a.SoftDeactivate << ' ' << a.SnapTo << ' ' << a.LimitVelocity << ' '
			<< a.LimitAcceleration << ' ' << int(a.Origin) << ' ' << int(a.Angles) << ' ' << int(a.Up) << ' '
			<< a.EntityIndex << ' ' << a.OffSet.X << ' ' << a.OffSet.Y << ' ' << a.OffSet.Z << ' ' << p.X << ' ' << p.Y
			<< ' ' << p.Z << ' ' << bool(a.Source_get()) << '\n';
	}
	out << bool(r.AimTarget) << '\n';
	return (!r.AimTarget || ProjectRig(out, *r.AimTarget, depth + 1)) && bool(out);
}
bool ProjectRig(std::istream &in, NamedCameraRig &r, int depth = 0)
{
	if (depth > 1)
		return false;
	if (!(in >> r.Id >> r.AttachMode >> r.Rig.Attached >> r.Rig.InheritRotation >> r.EntityAddress >> r.TargetHandle >>
		  r.CacheIdentity >> std::quoted(r.DemoKey) >> std::quoted(r.TargetModel)) ||
		r.Id >= 0x7fffffffffffffffull || r.AttachMode < 0 || r.AttachMode > 3 || r.DemoKey.size() > 4096 ||
		r.TargetModel.size() > 4096)
		return false;
	if (!ProjectPose(in, r.Rig.Pose) || !ProjectPose(in, r.LastWorld))
		return false;
	bool present;
	if (!(in >> present))
		return false;
	if (present)
	{
		auto c = Mirv_Tf2_LoadCamera(in);
		if (!c)
			return false;
		c->AddRef();
		r.Target = {c, [](IMirvVecAngCalc *p) { p->Release(); }};
	}
	if (r.Rig.Attached && !r.Target)
		return false;
	r.TargetLevel = -1;
	r.Available = !r.Rig.Attached;
	if (!(in >> present))
		return false;
	if (present)
	{
		r.Aim = std::make_shared<Aiming>();
		auto &a = *r.Aim;
		int origin, angles, up;
		Vector3 point;
		bool source;
		if (!(in >> a.Active >> a.SoftDeactivate >> a.SnapTo >> a.LimitVelocity >> a.LimitAcceleration >> origin >>
			  angles >> up >> a.EntityIndex >> a.OffSet.X >> a.OffSet.Y >> a.OffSet.Z >> point.X >> point.Y >>
			  point.Z >> source))
			return false;
		if (origin < 0 || origin > 1 || angles < 0 || angles > 1 || up < 0 || up > 1 ||
			!std::isfinite(a.LimitVelocity) || a.LimitVelocity < 0 || !std::isfinite(a.LimitAcceleration) ||
			a.LimitAcceleration < 0)
			return false;
		for (double v : {a.OffSet.X, a.OffSet.Y, a.OffSet.Z, point.X, point.Y, point.Z})
			if (!std::isfinite(v))
				return false;
		a.Origin = Aiming::Origin_e(origin);
		a.Angles = Aiming::Angles_e(angles);
		a.Up = Aiming::Up_e(up);
		a.TargetPoint(point);
		if (source)
			a.RebuildCalc();
	}
	if (!(in >> present))
		return false;
	if (present)
	{
		r.AimTarget = std::make_shared<NamedCameraRig>();
		if (!ProjectRig(in, *r.AimTarget, depth + 1))
			return false;
	}
	return !r.Aim || !r.Aim->Source_get() || bool(r.AimTarget);
}
bool ProjectWrite(const wchar_t *name, const std::string &data)
{
	if (data.size() > 64 * 1024 * 1024)
		return false;
	auto file = std::filesystem::path(name), tmp = file;
	tmp += L".writing-" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count());
	std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	out.write(data.data(), data.size());
	out.flush();
	bool ok = bool(out);
	out.close();
	ok = ok && !out.fail();
	std::error_code error;
#ifdef _WIN32
	if (ok)
		ok = !!MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
	if (ok)
	{
		std::filesystem::rename(tmp, file, error);
		ok = !error;
	}
#endif
	if (!ok)
		std::filesystem::remove(tmp, error);
	return ok;
}
} // namespace
extern CommandSystem g_CommandSystem;
bool MirvCam_SaveProject(const wchar_t *file)
{
	std::lock_guard<std::recursive_mutex> lock(g_CameraMutex);
	SyncRigLevel();
	std::ostringstream out;
	out.imbue(std::locale::classic());
	out << std::setprecision(17) << "HLAE_CINEMATIC 1\n";
	auto &paths = g_Hook_VClient_RenderView.m_CamPaths;
	auto state = paths.CaptureState();
	out << state.Paths.size() << ' ' << state.Active << '\n';
	for (auto &entry : state.Paths)
	{
		CamPath path;
		path.RestoreState(*entry.Data);
		std::string xml;
		if (!path.SaveToString(xml))
			return false;
		out << entry.Id << ' ' << std::quoted(entry.Name) << ' ' << entry.Muted << ' ' << entry.Solo << ' '
			<< entry.Visible << ' ' << std::quoted(xml) << '\n';
		if (out.tellp() > 64 * 1024 * 1024)
			return false;
	}
	out << g_CameraRigs.size() << ' ' << std::quoted(g_CameraRigName) << '\n';
	for (auto &item : g_CameraRigs)
	{
		out << std::quoted(item.first) << '\n';
		if (!ProjectRig(out, item.second))
			return false;
	}
	out << std::quoted(g_SequenceDemo) << ' ' << g_CameraClips.size() << '\n';
	for (auto &c : g_CameraClips)
		out << c.Id << ' ' << c.Target << ' ' << c.Time << ' ' << c.Blend << ' ' << c.Campath << '\n';
	auto &markers = g_CommandSystem.GetTickMarkers();
	out << markers.size() << '\n';
	for (auto &m : markers)
		out << m.Tick << ' ' << std::quoted(m.Name) << ' ' << std::quoted(m.Command) << ' ' << m.Enabled << ' '
			<< m.RunOnSeek << '\n';
	out << "END\n";
	return bool(out) && ProjectWrite(file, out.str());
}
bool MirvCam_LoadProject(const wchar_t *file)
{
	std::lock_guard<std::recursive_mutex> lock(g_CameraMutex);
	std::error_code error;
	auto size = std::filesystem::file_size(std::filesystem::path(file), error);
	if (error || size > 64 * 1024 * 1024)
		return false;
	std::ifstream input(std::filesystem::path(file), std::ios::binary);
	std::string text(size, '\0');
	if (!input.read(text.data(), size))
		return false;
	std::istringstream in(text);
	in.imbue(std::locale::classic());
	std::string magic;
	int version;
	size_t count;
	if (!(in >> magic >> version) || magic != "HLAE_CINEMATIC" || version != 1)
		return false;
	CamPathCollection::State paths;
	std::set<unsigned long long> pathIds, rigIds, clipIds;
	if (!(in >> count >> paths.Active) || !count || count > 128 || paths.Active >= count)
		return false;
	for (size_t i = 0; i < count; ++i)
	{
		CamPathCollection::Entry entry;
		std::string xml;
		if (!(in >> entry.Id >> std::quoted(entry.Name) >> entry.Muted >> entry.Solo >> entry.Visible >>
			  std::quoted(xml)) ||
			entry.Id >= 0x7fffffffffffffffull || entry.Name.empty() || entry.Name.size() > 128 ||
			!pathIds.insert(entry.Id).second)
			return false;
		for (unsigned char ch : entry.Name)
			if (ch < 32)
				return false;
		for (const auto &other : paths.Paths)
			if (!_stricmp(other.Name.c_str(), entry.Name.c_str()))
				return false;
		CamPath path;
		if (!path.LoadFromString(xml))
			return false;
		entry.Data = std::make_shared<const CamPath::State>(path.CaptureState());
		paths.Paths.push_back(entry);
	}
	std::map<std::string, NamedCameraRig> rigs;
	std::string selected;
	if (!(in >> count >> std::quoted(selected)) || count > 128)
		return false;
	for (size_t i = 0; i < count; ++i)
	{
		std::string name;
		NamedCameraRig r;
		if (!(in >> std::quoted(name)) || name.empty() || name.size() > 128 || name.find('\0') != std::string::npos ||
			!ProjectRig(in, r) || !rigIds.insert(r.Id).second || !rigs.emplace(name, std::move(r)).second)
			return false;
	}
	if (!selected.empty() && !rigs.count(selected))
		return false;
	std::string demo;
	std::vector<advancedfx::CameraClip> clips;
	if (!(in >> std::quoted(demo) >> count) || count > 256 || demo.size() > 4096)
		return false;
	for (size_t i = 0; i < count; ++i)
	{
		advancedfx::CameraClip c;
		if (!(in >> c.Id >> c.Target >> c.Time >> c.Blend >> c.Campath) || c.Id >= 0x7fffffffffffffffull ||
			!clipIds.insert(c.Id).second || !std::isfinite(c.Time) || std::abs(c.Time) > 1e9 ||
			!std::isfinite(c.Blend) || c.Blend < 0 || c.Blend > 3600)
			return false;
		// Missing references are retained, visibly unavailable, so removed sources can be corrected.
		if (!clips.empty() &&
			(c.Time - clips.back().Time < 1e-9 || clips.back().Time + clips.back().Blend > c.Time + 1e-9))
			return false;
		clips.push_back(c);
	}
	CommandSystem checked(nullptr, nullptr, nullptr);
	if (!(in >> count) || count > 10000)
		return false;
	for (size_t i = 0; i < count; ++i)
	{
		CommandSystem::TickMarker m;
		if (!(in >> m.Tick >> std::quoted(m.Name) >> std::quoted(m.Command) >> m.Enabled >> m.RunOnSeek))
			return false;
		m.Id = checked.AddTickMarker(m.Tick, m.Name, m.Command);
		if (!m.Id || !checked.SetTickMarker(m))
			return false;
	}
	if (!(in >> magic) || magic != "END")
		return false;
	in >> std::ws;
	if (!in.eof())
		return false;
	// No live state changes until every section has passed validation.
	AfxHookSource::CameraOverlay::FlushRecovery();
	MirvCam_EnableSequence(false);
	MirvCam_EndRigPreview();
	MirvCam_ReleaseLiveAttachment();
	g_LegacyCameraName.clear();
	g_EditingCameraName.clear();
	g_Hook_VClient_RenderView.m_CamPath.Enabled_set(false);
	g_Hook_VClient_RenderView.m_CamPaths.RestoreState(paths);
	g_Hook_VClient_RenderView.m_CamPaths.ResetProjectHistory();
	g_CameraRigs = std::move(rigs);
	g_CameraRigName = selected;
	g_CameraClips = std::move(clips);
	g_SequenceDemo = demo.empty() ? g_CameraDemoKey : demo;
	g_SequenceLevel = g_LevelInitCount;
	for (auto &r : g_CameraRigs)
		g_NextCameraId = (std::max)(g_NextCameraId, r.second.Id + 1);
	for (auto &c : g_CameraClips)
		g_NextClipId = (std::max<unsigned long long>)(g_NextClipId, c.Id + 1);
	g_CameraUndo.clear();
	g_CameraRedo.clear();
	g_CommandSystem.ClearTickMarkers();
	for (auto m : checked.GetTickMarkers())
	{
		m.Id = g_CommandSystem.AddTickMarker(m.Tick, m.Name, m.Command);
		g_CommandSystem.SetTickMarker(m);
	}
	++g_CameraCaptureRevision;
	AfxHookSource::CameraOverlay::ResetProjectRecovery();
	return true;
}
std::string MirvCam_BakeStatus()
{
	std::lock_guard<std::recursive_mutex> lock(g_CameraMutex);
	return g_CameraBakeReport;
}
bool MirvCam_Bake(const wchar_t *file, const char *source, double begin, double end, double step)
{
	std::lock_guard<std::recursive_mutex> lock(g_CameraMutex);
	g_CameraBakeReport = "Bake failed: invalid range, step, source or sample limit.";
	if (!std::isfinite(begin) || !std::isfinite(end) || !std::isfinite(step) || begin >= end || step <= 0 ||
		(end - begin) / step > 50000)
		return false;
	const bool sequence = !std::strcmp(source, "sequence");
	auto original = FindRig(source);
	if (!sequence && original == g_CameraRigs.end())
		return false;
	if (sequence && g_CameraClips.empty())
		return false;
	const auto id = sequence ? 0 : original->second.Id;
	struct Restore
	{
		std::map<std::string, NamedCameraRig> Rigs;
		double Time, Step;
		int Frame;
		unsigned long long SequenceCamera;
		std::string Failure;
		bool Baking;
		std::shared_ptr<const advancedfx::SceneCache::Frame> BakeFrame;
		Restore()
			: Rigs(g_CameraRigs), Time(g_CameraEvaluationTime), Step(g_CameraEvaluationStep),
			  Frame(g_CameraEvaluationFrame), SequenceCamera(g_SequenceCamera), Failure(g_CameraFailureReason),
			  Baking(g_BakingCamera), BakeFrame(g_BakeFrame)
		{
			for (auto &r : g_CameraRigs)
				r.second = CloneCameraDefinition(r.second);
		}
		~Restore()
		{
			g_CameraRigs = std::move(Rigs);
			g_CameraEvaluationTime = Time;
			g_CameraEvaluationStep = Step;
			g_CameraEvaluationFrame = Frame;
			g_SequenceCamera = SequenceCamera;
			g_CameraFailureReason = Failure;
			g_BakingCamera = Baking;
			g_BakeFrame = BakeFrame;
		}
	} restore;
	g_BakingCamera = true;
	g_CameraEvaluationFrame = -1;
	std::set<double> times;
	for (size_t i = 0; i <= size_t(std::ceil((end - begin) / step)); ++i)
		times.insert((std::min)(end, begin + i * step));
	if (sequence)
		for (auto &c : g_CameraClips)
			if (c.Time >= begin && c.Time <= end)
			{
				times.insert(c.Time);
				if (c.Blend == 0 && c.Time > begin)
					times.insert((std::max)(begin, c.Time - 1e-6));
				if (c.Time + c.Blend <= end)
					times.insert(c.Time + c.Blend);
			}
	CamPath baked;
	baked.PositionInterpMethod_set(CamPath::DI_LINEAR);
	baked.RotationInterpMethod_set(CamPath::QI_SLINEAR);
	baked.FovInterpMethod_set(CamPath::DI_LINEAR);
	advancedfx::CameraPose previous;
	bool first = true;
	size_t changes = 0;
	for (double time : times)
	{
		advancedfx::SceneCache::Frame frame;
		g_BakeFrame = AfxHookSource::CameraOverlay::SampleCameraScene(time, frame)
						  ? std::make_shared<const advancedfx::SceneCache::Frame>(std::move(frame))
						  : nullptr;
		g_CameraEvaluationTime = time;
		advancedfx::CameraPose pose;
		auto rig = FindRig(std::to_string(id));
		if (!(sequence ? EvaluateSequence(time, pose)
					   : rig != g_CameraRigs.end() && EvaluateNamedRig(rig->second, pose)))
		{
			g_CameraBakeReport = "Bake failed at client time " + std::to_string(time) + ": " + g_CameraFailureReason +
								 ". Existing file retained.";
			return false;
		}
		if (!first)
		{
			double delta = std::abs(pose.Fov - previous.Fov);
			for (int i = 0; i < 3; ++i)
				delta += std::abs(pose.Position[i] - previous.Position[i]);
			const auto &a = pose.Rotation;
			const auto &b = previous.Rotation;
			delta += 1 - std::abs(a.W * b.W + a.X * b.X + a.Y * b.Y + a.Z * b.Z) / (a.Norm() * b.Norm());
			changes += delta > 1e-9;
		}
		previous = pose;
		first = false;
		const auto a = pose.Rotation.ToQREulerAngles().ToQEulerAngles();
		baked.Add(time,
				  CamPathValue(pose.Position[0], pose.Position[1], pose.Position[2], a.Pitch, a.Yaw, a.Roll, pose.Fov));
	}
	const bool saved = baked.ExportClassic(file, step);
	g_CameraBakeReport = (saved ? "Saved " : "Failed to save ") + std::to_string(times.size()) + " poses from " +
						 (sequence ? std::string("whole sequence") : "camera #" + std::to_string(id)) +
						 ", client seconds " + std::to_string(begin) + " .. " + std::to_string(end) + "; " +
						 std::to_string(changes) + " changed samples.";
	if (saved && !changes)
		g_CameraBakeReport += " All sampled poses are identical.";
	return saved;
}
