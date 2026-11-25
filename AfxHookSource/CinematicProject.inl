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
	const auto &motion = r.Rig.Motion;
	if (!motion.Valid())
		return false;
	out << motion.FollowHalfLife << ' ' << motion.RotationHalfLife << ' ' << motion.MaxDistance << ' '
		<< motion.TeleportDistance << ' ' << motion.OrbitSpeed << ' ' << motion.OrbitEpoch << ' '
		<< motion.BoneHalfLife << ' ' << motion.TurnRate << '\n';
	out << motion.PreserveFraming << ' ' << motion.FramingMargin << ' ' << motion.BonePositionInfluence << ' '
		<< motion.BoneRotationInfluence << ' ' << motion.BonePositionLimit << ' ' << motion.BoneRotationLimit
		<< '\n';
	out << r.HasBoneReference << '\n';
	if (r.HasBoneReference && !ProjectPose(out, r.BoneReference))
		return false;
	out << bool(r.Rig.CollisionOverride) << '\n';
	if (r.Rig.CollisionOverride)
	{
		const auto &c = *r.Rig.CollisionOverride;
		out << c.Attached << ' ' << c.Players << ' ' << c.Slide << ' ' << c.IgnoreLocal << ' ' << c.Debug << ' '
			<< c.Radius << ' ' << c.ReturnHalfLife << ' ' << c.ReturnSpeed << ' ' << c.AttachedSoftDistance << ' '
			<< c.SubjectClearance << ' ' << c.NearPlane << ' ' << c.FollowRecoveryDelay << '\n';
	}
	out << r.Modifiers.size() << '\n';
	for (const auto &link : r.Modifiers)
		out << link.Id << ' ' << link.Influence << ' ' << link.Enabled << '\n';
	if (!ProjectPose(out, r.Rig.Pose) || !ProjectPose(out, r.LastWorld) || !ProjectPose(out, r.BaseWorld))
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
bool ProjectRig(std::istream &in, NamedCameraRig &r, int version, int depth = 0)
{
	if (depth > 1)
		return false;
	if (!(in >> r.Id >> r.AttachMode >> r.Rig.Attached >> r.Rig.InheritRotation >> r.EntityAddress >> r.TargetHandle >>
		  r.CacheIdentity >> std::quoted(r.DemoKey) >> std::quoted(r.TargetModel)) ||
		r.Id >= 0x7fffffffffffffffull || r.AttachMode < 0 || r.AttachMode > 3 || r.DemoKey.size() > 4096 ||
		r.TargetModel.size() > 4096)
		return false;
	if (version >= 2)
	{
		auto &motion = r.Rig.Motion;
		if (!(in >> motion.FollowHalfLife >> motion.RotationHalfLife >> motion.MaxDistance >>
			  motion.TeleportDistance >> motion.OrbitSpeed >> motion.OrbitEpoch) ||
			!motion.Valid())
			return false;
	}
	if (version >= 3)
		if (!(in >> r.Rig.Motion.BoneHalfLife >> r.Rig.Motion.TurnRate) || !r.Rig.Motion.Valid())
			return false;
	if (version >= 6)
	{
		auto &m = r.Rig.Motion;
		if (!(in >> m.PreserveFraming >> m.FramingMargin >> m.BonePositionInfluence >>
			  m.BoneRotationInfluence >> m.BonePositionLimit >> m.BoneRotationLimit) ||
			!m.Valid() || !(in >> r.HasBoneReference))
			return false;
		if (r.HasBoneReference && !ProjectPose(in, r.BoneReference))
			return false;
	}
	else
		r.Rig.Motion.PreserveFraming = false; // Keep previously authored follow behavior on import.
	if (version >= 8)
	{
		bool overrideCollision = false;
		if (!(in >> overrideCollision))
			return false;
		if (overrideCollision)
		{
			MirvCameraCollisionSettings c;
			if (!(in >> c.Attached >> c.Players >> c.Slide >> c.IgnoreLocal >> c.Debug >> c.Radius >>
				  c.ReturnHalfLife >> c.ReturnSpeed >> c.AttachedSoftDistance >> c.SubjectClearance >>
				  c.NearPlane >> c.FollowRecoveryDelay) ||
				!c.Valid())
				return false;
			r.Rig.CollisionOverride = c.ForAttachment();
		}
	}
	if (version >= 4)
	{
		size_t count;
		if (!(in >> count) || count > 64)
			return false;
		for (size_t i = 0; i < count; ++i)
		{
			advancedfx::CameraModifierLink link;
			if (!(in >> link.Id >> link.Influence >> link.Enabled) || !std::isfinite(link.Influence) ||
				link.Influence < 0 || link.Influence > 1)
				return false;
			r.Modifiers.push_back(link);
		}
	}
	if (!ProjectPose(in, r.Rig.Pose) || !ProjectPose(in, r.LastWorld))
		return false;
	if (version >= 4)
	{
		if (!ProjectPose(in, r.BaseWorld))
			return false;
	}
	else
		r.BaseWorld = r.LastWorld;
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
		if (!ProjectRig(in, *r.AimTarget, version, depth + 1))
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
	out << std::setprecision(17) << "HLAE_CINEMATIC 11\n";
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
	{
		out << c.Id << ' ' << c.Target << ' ' << c.Time << ' ' << c.Blend << ' ' << c.Campath << ' '
			<< c.BlendCurve << ' ' << c.TransitionOffset << ' ' << c.RotationCurve << ' ' << c.FovCurve
			<< ' ' << c.FreezeOutgoing << ' ' << c.CurvedPath
			<< ' ' << c.HandleOut[0] << ' ' << c.HandleOut[1] << ' ' << c.HandleOut[2]
			<< ' ' << c.HandleIn[0] << ' ' << c.HandleIn[1] << ' ' << c.HandleIn[2] << '\n';
		out << c.Modifiers.size() << '\n';
		for (const auto &m : c.Modifiers)
			out << m.Id << ' ' << m.Influence << ' ' << m.Enabled << ' ' << m.Begin << ' ' << m.End << ' '
				<< m.FadeIn << ' ' << m.FadeOut << '\n';
	}
	auto &markers = g_CommandSystem.GetTickMarkers();
	out << markers.size() << '\n';
	for (auto &m : markers)
		out << m.Tick << ' ' << std::quoted(m.Name) << ' ' << std::quoted(m.Command) << ' ' << m.Enabled << ' '
			<< m.RunOnSeek << '\n';
	out << g_CameraModifiers.size() << '\n';
	for (const auto &m : g_CameraModifiers)
	{
		CamPath data;
		std::string xml;
		if (!data.SetModifiers({m.Settings}) || !data.SaveToString(xml))
			return false;
		out << m.Id << ' ' << std::quoted(m.Name) << ' ' << std::quoted(xml) << '\n';
	}
 out << g_SequenceModifiers.size() << '\n';
 for (const auto &m : g_SequenceModifiers)
  out << m.Id << ' ' << m.Definition << ' ' << m.Track << ' ' << m.Begin << ' ' << m.End << ' '
   << m.FadeIn << ' ' << m.FadeOut << ' ' << m.Influence << ' ' << m.Enabled << '\n';
	if (!advancedfx::TimelineTimebase().Write(out)) return false;
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
	if (!(in >> magic >> version) || magic != "HLAE_CINEMATIC" ||
		(version != 1 && version != 2 && version != 3 && version != 4 && version != 5 && version != 6 &&
		 version != 7 && version != 8 && version != 9 && version != 10 && version != 11))
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
		if (!(in >> std::quoted(name)) || name.empty() || name.size() > 128 ||
			name.find('\0') != std::string::npos || !ProjectRig(in, r, version) ||
			!rigIds.insert(r.Id).second || !rigs.emplace(name, std::move(r)).second)
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
		if (version >= 7 && (!(in >> c.BlendCurve) || c.BlendCurve < 0 || c.BlendCurve > 2))
			return false;
		// Before format 9 a single curve was applied to position, rotation and FOV.
		if (version < 9)
			c.RotationCurve = c.FovCurve = c.BlendCurve;
		if (version >= 9 &&
			(!(in >> c.TransitionOffset >> c.RotationCurve >> c.FovCurve >> c.FreezeOutgoing >>
			   c.CurvedPath >> c.HandleOut[0] >> c.HandleOut[1] >> c.HandleOut[2] >>
			   c.HandleIn[0] >> c.HandleIn[1] >> c.HandleIn[2]) || !c.ValidTransition()))
			return false;
		if (version >= 5)
		{
			size_t links;
			if (!(in >> links) || links > 64)
				return false;
			for (size_t n = 0; n < links; ++n)
			{
				advancedfx::CameraClipModifier m;
				if (!(in >> m.Id >> m.Influence >> m.Enabled >> m.Begin >> m.End >> m.FadeIn >> m.FadeOut) ||
					!m.Valid())
					return false;
				c.Modifiers.push_back(m);
			}
		}
		// Missing references are retained, visibly unavailable, so removed sources can be corrected.
		if (!clips.empty() &&
			(c.Time - clips.back().Time < 1e-9 ||
			 (version >= 9 ? c.TransitionStart() - clips.back().TransitionStart() < 1e-9
			               : clips.back().Time + clips.back().Blend > c.Time + 1e-9)))
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
	std::vector<MirvCameraModifier> modifiers;
	std::set<std::uint64_t> modifierIds;
	if (version >= 4)
	{
		if (!(in >> count) || count > 128)
			return false;
		for (size_t i = 0; i < count; ++i)
		{
			MirvCameraModifier m;
			std::string xml;
			CamPath data;
			if (!(in >> m.Id >> std::quoted(m.Name) >> std::quoted(xml)) || !m.Id ||
				m.Id >= 0x7fffffffffffffffull || !modifierIds.insert(m.Id).second || m.Name.empty() ||
				m.Name.size() > 128 || !data.LoadFromString(xml) || data.GetModifiers().size() != 1)
				return false;
			m.Settings = data.GetModifiers()[0];
			if (m.Settings.Type != CamPath::ModifierType::Noise &&
				m.Settings.Type != CamPath::ModifierType::Oscillation &&
				m.Settings.Type != CamPath::ModifierType::Limits &&
				m.Settings.Type != CamPath::ModifierType::Remap)
				return false;
			modifiers.push_back(m);
		}
	}
	for (const auto &entry : rigs)
		for (const auto &link : entry.second.Modifiers)
			if (!modifierIds.count(link.Id))
				return false;
	for (const auto &clip : clips)
		for (const auto &link : clip.Modifiers)
			if (!modifierIds.count(link.Id))
				return false;
 std::vector<advancedfx::SequenceModifierClip> adjustments;
 if (version >= 10)
 {
  std::set<std::uint64_t> adjustmentIds;
  if (!(in >> count) || count > advancedfx::SequenceModifierClip::MaxClips) return false;
  for (size_t i = 0; i < count; ++i)
  {
   advancedfx::SequenceModifierClip m;
   if (!(in >> m.Id >> m.Definition >> m.Track >> m.Begin >> m.End >> m.FadeIn >> m.FadeOut >> m.Influence >> m.Enabled) ||
    m.Id >= 0x7fffffffffffffffull || !m.Valid() || !modifierIds.count(m.Definition) || !adjustmentIds.insert(m.Id).second ||
    std::any_of(adjustments.begin(), adjustments.end(), [&](const auto &other){return m.Overlaps(other);})) return false;
   adjustments.push_back(m);
  }
  std::sort(adjustments.begin(), adjustments.end(), advancedfx::SequenceModifierClip::Less);
 }
 advancedfx::MusicalTimeline timebase;
 if (version >= 11 && !advancedfx::MusicalTimeline::Read(in,timebase)) return false;
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
	g_CameraModifiers = std::move(modifiers);
	for (const auto &m : g_CameraModifiers)
		g_NextModifierId = (std::max)(g_NextModifierId, m.Id + 1);
	g_CameraRigs = std::move(rigs);
	g_CameraRigName = selected;
	g_CameraClips = std::move(clips);
 advancedfx::TimelineTimebase() = timebase;
 g_SequenceModifiers = std::move(adjustments);
 for (const auto &m : g_SequenceModifiers)
  g_NextSequenceModifierId = (std::max)(g_NextSequenceModifierId, m.Id + 1);
	g_SequenceDemo = demo.empty() ? g_CameraDemoKey : demo;
	g_SequenceLevel = g_LevelInitCount;
	for (auto &r : g_CameraRigs)
		g_NextCameraId = (std::max)(g_NextCameraId, r.second.Id + 1);
	for (auto &c : g_CameraClips)
		g_NextClipId = (std::max<unsigned long long>)(g_NextClipId, c.Id + 1);
	g_CameraUndo.clear();
	g_CameraRedo.clear();
	g_CameraUndoIndices.clear();
	g_CameraRedoIndices.clear();
	g_CameraRedoGeneration = CinematicRedoGeneration();
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
	const bool sequence = source && !std::strcmp(source, "sequence");
	const bool campath = source && !std::strcmp(source, "campath");
	auto original = source ? FindRig(source) : g_CameraRigs.end();
	if (!sequence && !campath && original == g_CameraRigs.end())
		return false;
	if (sequence && g_CameraClips.empty())
		return false;
	const auto id = sequence || campath ? 0 : original->second.Id;
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
			{
				const double start = c.TransitionStart(), finish = start + c.Blend;
				if (start >= begin && start <= end)
				{
					times.insert(start);
					if (c.Blend == 0 && start > begin)
					{
						const double previousClock =
							double(std::nextafter(float(start), -std::numeric_limits<float>::infinity()));
						const double padding = (std::max)(1e-6, start - previousClock);
						times.insert((std::max)(begin, start - padding));
					}
				}
				if (finish >= begin && finish <= end)
					times.insert(finish);
			}
 // Preserve adjustment envelope boundaries and hard cuts even when the
 // export sampling grid does not land exactly on them.
 if (sequence)
  for (const auto &m : g_SequenceModifiers)
  {
   for (double t : {m.Begin, m.Begin + m.FadeIn, m.End - m.FadeOut, m.End})
    if (begin <= t && t <= end) times.insert(t);
   for (double t : {m.Begin, m.End})
    if (t > begin && t <= end)
    {
     const double previous = double(std::nextafter(float(t), -std::numeric_limits<float>::infinity()));
     times.insert((std::max)(begin, t - (std::max)(1e-6, t - previous)));
    }
  }
	// Preserve authored key/hold boundaries when routing path exports through
	// the same sampler as camera sequences.
	if (campath || sequence)
	{
		auto &paths = g_Hook_VClient_RenderView.m_CamPaths;
		for (size_t index = 0; index < paths.GetCount(); ++index)
		{
			if (campath && index != paths.GetActive())
				continue;
			if (sequence && std::none_of(g_CameraClips.begin(), g_CameraClips.end(), [&](const auto &clip)
										 { return clip.Campath && clip.Target == paths.GetId(index); }))
				continue;
			auto path = paths.GetPath(index);
			if (!path)
				continue;
			for (auto key = path->GetBegin(); key != path->GetEnd(); ++key)
				for (double t : {key.GetTime() + path->GetOffset(),
								 key.GetTime() + key.GetValue().HoldDuration + path->GetOffset()})
					if (begin <= t && t <= end)
						times.insert(t);
		}
	}
	if (times.size() > 100000)
	{
		g_CameraBakeReport = "Bake failed: too many sample boundaries.";
		return false;
	}
	CamPath baked;
	baked.SetHold(!campath || g_Hook_VClient_RenderView.m_CamPath.GetHold());
	baked.PositionInterpMethod_set(CamPath::DI_LINEAR);
	baked.RotationInterpMethod_set(CamPath::QI_SLINEAR);
	baked.FovInterpMethod_set(CamPath::DI_LINEAR);
	auto bakedState = baked.CaptureState();
	advancedfx::CameraPose previous;
	bool first = true;
	size_t changes = 0;
	std::vector<size_t> clipSamples(g_CameraClips.size(), 0);
	for (double time : times)
	{
		advancedfx::SceneCache::Frame frame;
		g_BakeFrame = !campath && AfxHookSource::CameraOverlay::SampleCameraScene(time, frame)
						  ? std::make_shared<const advancedfx::SceneCache::Frame>(std::move(frame))
						  : nullptr;
		g_CameraEvaluationTime = time;
		advancedfx::CameraPose pose;
		auto rig = FindRig(std::to_string(id));
		if (!(sequence	? EvaluateSequence(time, pose)
			  : campath ? EvaluatePlaybackPose(time, pose,
											   g_Hook_VClient_RenderView.m_CamPaths.GetId(
												   g_Hook_VClient_RenderView.m_CamPaths.GetActive()))
						: rig != g_CameraRigs.end() && EvaluateIndividualCamera(rig->second, pose)))
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
		if (sequence)
		{
			auto next =
				std::upper_bound(g_CameraClips.begin(), g_CameraClips.end(), time,
								 [](double t, const advancedfx::CameraClip &c) { return t < c.Time; });
			const auto index = next == g_CameraClips.begin() ? 0 : size_t(next - g_CameraClips.begin() - 1);
			++clipSamples[index];
		}
		previous = pose;
		first = false;
		const auto q = pose.Rotation.Normalized();
		bakedState.Keys.emplace(time, CamPathValue(pose.Position[0], pose.Position[1], pose.Position[2], q.W,
												   q.X, q.Y, q.Z, pose.Fov, false));
	}
	// Install once: Add copies and validates the entire map on each insertion.
	baked.RestoreState(bakedState);
	const bool saved = baked.ExportClassic(file, step);
	g_CameraBakeReport = (saved ? "Saved " : "Failed to save ") + std::to_string(times.size()) +
						 " poses from " +
						 (sequence	? std::string("whole sequence")
						  : campath ? std::string("active campath")
									: "camera #" + std::to_string(id)) +
						 ", client seconds " + std::to_string(begin) + " .. " + std::to_string(end) + "; " +
						 std::to_string(changes) + " changed samples.";
	if (saved)
		g_CameraBakeReport += " XML reload verified for every exported pose.";
	if (saved && !changes)
		g_CameraBakeReport += " All sampled poses are identical.";
	if (saved && sequence)
	{
		const auto visited =
			std::count_if(clipSamples.begin(), clipSamples.end(), [](size_t n) { return n != 0; });
		g_CameraBakeReport += " Sampled " + std::to_string(visited) + " of " +
							  std::to_string(g_CameraClips.size()) + " sequence clips.";
		if (!changes)
			for (size_t i = 0; i < g_CameraClips.size() && i < 8; ++i)
			{
				const auto &clip = g_CameraClips[i];
				g_CameraBakeReport +=
					"\nClip #" + std::to_string(clip.Id) + (clip.Campath ? " campath #" : " camera #") +
					std::to_string(clip.Target) + " at client " + std::to_string(clip.Time) + ": " +
					std::to_string(clipSamples[i]) + " samples.";
			}
	}

	return saved;
}
