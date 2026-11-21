#include "stdafx.h"
#include "RecordingSettings.h"
#include "StringTools.h"
#include "RecordingProfilePersistence.h"
#include <filesystem>
#include <fstream>

#include <set>
#include <cmath>
#include <cstring>

namespace advancedfx {


// CRecordingSettings ///////////////////////////////////////////////////////

CRecordingSettings::CShared CRecordingSettings::m_Shared;

namespace {
 std::filesystem::path g_ProfileFile;
 std::string g_PreferredProfile = "afxDefault";
 bool g_ProfileLoadFailed = false;
 RecordingProfilePersistence::Snapshot ProfileValues() {
  RecordingProfilePersistence::Snapshot result;result.Selected=g_PreferredProfile;
  RecordingProfileInfo defaults;CRecordingSettings::Describe("afxDefault",defaults);result.DefaultOutput=defaults.OutputSettings;
  for (const auto &name : CRecordingSettings::GetNames()) {
   RecordingProfileInfo p;
   if (CRecordingSettings::Describe(name.c_str(),p) && !p.Protected && p.Type!=RecordingProfileInfo::Kind::Other) result.Profiles.push_back(std::move(p));
  }
  return result;
 }
 struct ProfileCommandSave {
  std::string Before=CRecordingSettings::ProfileSnapshot();
  ~ProfileCommandSave(){if (Before!=CRecordingSettings::ProfileSnapshot()) CRecordingSettings::SavePersistent();}
 };
}
std::string CRecordingSettings::ProfileSnapshot(){return RecordingProfilePersistence::Encode(ProfileValues());}
std::string CRecordingSettings::PreferredProfile(){return g_PreferredProfile;}
void CRecordingSettings::SetPreferredProfile(const char *name){
 if (name && GetByName(name) && g_PreferredProfile!=name) {g_PreferredProfile=name;SavePersistent();}
}
void CRecordingSettings::SavePersistent(){
 if (g_ProfileFile.empty() || g_ProfileLoadFailed) return;
 const auto snapshot=ProfileValues();
 if (!RecordingProfilePersistence::Validate(snapshot)) {Warning("Could not save invalid recording profiles.\n");return;}
 auto temp=g_ProfileFile;temp+=L".tmp";
 {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out<<RecordingProfilePersistence::Encode(snapshot);out.flush();if(!out){Warning("Could not write recording profile preferences.\n");return;}}
#ifdef _WIN32
 if (!MoveFileExW(temp.c_str(),g_ProfileFile.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) Warning("Could not replace recording profile preferences.\n");
#else
 std::error_code ec;std::filesystem::rename(temp,g_ProfileFile,ec);if(ec) Warning("Could not replace recording profile preferences.\n");
#endif
}
bool CRecordingSettings::ConfigurePersistence(const wchar_t *file){
 if (!g_ProfileFile.empty()) return !g_ProfileLoadFailed;
 if (!file || !*file) return false;
 g_ProfileFile=file;
 std::error_code ec;
 auto fail=[&]{g_ProfileLoadFailed=true;Warning("Recording profile preferences could not be loaded; the existing file was preserved.\n");return false;};
 if (!std::filesystem::exists(g_ProfileFile,ec)) return ec?fail():true;
 const auto size=std::filesystem::file_size(g_ProfileFile,ec);if(ec||size>2*1024*1024) return fail();
 std::ifstream in(g_ProfileFile,std::ios::binary);if(!in)return fail();
 const std::string text((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
 if(in.bad())return fail();
 RecordingProfilePersistence::Snapshot snapshot;
 if (!RecordingProfilePersistence::Decode(text,snapshot)) return fail();
 // Validate references before touching the registry, then build dependency-first.
 std::set<std::string> names;
 for(const auto &p:snapshot.Profiles){if(GetByName(p.Name.c_str()))return fail();names.insert(RecordingProfilePersistence::Key(p.Name));}
 auto known=[&](const std::string &name){return GetByName(name.c_str()) || names.count(RecordingProfilePersistence::Key(name));};
 if(!known(snapshot.DefaultOutput))return fail();
 for(const auto &p:snapshot.Profiles){
  if(p.Type==RecordingProfileInfo::Kind::Multi){for(const auto &n:p.Outputs)if(!known(n))return fail();}
  else if(p.Type!=RecordingProfileInfo::Kind::Ffmpeg && p.Type!=RecordingProfileInfo::Kind::FfmpegEx && !known(p.OutputSettings))return fail();
 }
 // Built-in samplers can refer to afxDefault; include those implicit edges too.
 std::map<std::string,const RecordingProfileInfo*> definitions;
 for(const auto &p:snapshot.Profiles)definitions.emplace(RecordingProfilePersistence::Key(p.Name),&p);
 std::set<std::string> checked;
 std::function<bool(const std::string&)> reachesDefault=[&](const std::string &name){
  const auto key=RecordingProfilePersistence::Key(name);
  if(key=="afxdefault")return true;
  if(!checked.insert(key).second)return false;
  auto it=definitions.find(key);
  if(it==definitions.end()){auto existing=GetByName(name.c_str());return existing && existing->InheritsFrom(m_Shared.m_DefaultSettings);}
  const auto &p=*it->second;
  if(p.Type==RecordingProfileInfo::Kind::Multi){for(const auto &n:p.Outputs)if(reachesDefault(n))return true;return false;}
  if(p.Type==RecordingProfileInfo::Kind::Ffmpeg||p.Type==RecordingProfileInfo::Kind::FfmpegEx)return false;
  return reachesDefault(p.OutputSettings);
 };
 if(reachesDefault(snapshot.DefaultOutput))return fail();
 std::vector<RecordingProfileInfo> pending=snapshot.Profiles;
 while(!pending.empty()){
  bool progress=false;
  for(auto i=pending.begin();i!=pending.end();){
   CRecordingSettings *setting=nullptr;
   if(i->Type==RecordingProfileInfo::Kind::Ffmpeg) setting=new CFfmpegRecordingSettings(i->Name.c_str(),false,i->FfmpegOptions.c_str(),i->FfmpegOptionsNoAlpha.c_str());
   else if(i->Type==RecordingProfileInfo::Kind::FfmpegEx) setting=new CFfmpegExRecordingSettings(i->Name.c_str(),false,i->FfmpegOptions.c_str());
   else if(i->Type==RecordingProfileInfo::Kind::Multi){
    bool ready=true;for(const auto &n:i->Outputs)ready &= GetByName(n.c_str())!=nullptr;
    if(ready){auto multi=new CMultiRecordingSettings(i->Name.c_str(),false);if(!multi->ApplyFields(*i)){multi->AddRef();multi->Release();return fail();}setting=multi;}
   } else if(auto output=GetByName(i->OutputSettings.c_str())){
    if(i->Type==RecordingProfileInfo::Kind::Sampler) setting=new CSamplingRecordingSettings(i->Name.c_str(),false,output,i->Method==0?EasySamplerSettings::ESM_Rectangle:EasySamplerSettings::ESM_Trapezoid,i->OutputFps,float(i->Exposure),i->Strength);
    else if(i->Type==RecordingProfileInfo::Kind::Interleave)setting=new CInterleaveRecordingSettings(i->Name.c_str(),false,output,size_t(i->InputCount));
    else if(auto main=dynamic_cast<CInterleaveRecordingSettings *>(output))setting=new CInterleaveInputRecordingSettings(i->Name.c_str(),false,main,i->InputIndex);
   }
   if(!setting){++i;continue;}
   m_Shared.m_NamedSettings.emplace(setting->GetName(),setting);i=pending.erase(i);progress=true;
  }
  if(!progress)return fail();
 }
 if(!static_cast<CDefaultRecordingSettings *>(m_Shared.m_DefaultSettings)->SetOutputSettings(GetByName(snapshot.DefaultOutput.c_str())))return fail();
 if(GetByName(snapshot.Selected.c_str()))g_PreferredProfile=snapshot.Selected;
 return true;
}

std::vector<std::string> CRecordingSettings::GetNames()
{
	std::vector<std::string> result;
	result.reserve(m_Shared.m_NamedSettings.size());
	for (const auto &setting : m_Shared.m_NamedSettings)
		if (setting.second.Settings)
			result.emplace_back(setting.second.Settings->GetName());
	return result;
}

bool CRecordingSettings::Describe(const char *name, RecordingProfileInfo &out)
{
	auto *setting = name ? GetByName(name) : nullptr;
	if (!setting) return false;
	out = {};
	out.Name = setting->GetName();
	out.Protected = setting->GetProtected();
	setting->DescribeFields(out);
	return true;
}

bool CRecordingSettings::DuplicateEditable(const char *source, const char *newName)
{
	if (!source || !newName || !*newName || std::strlen(newName) > 63 ||
		!_strnicmp(newName, "afx", 3))
		return false;
	for (const unsigned char *p = reinterpret_cast<const unsigned char *>(newName); *p; ++p)
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
			  (*p >= '0' && *p <= '9') || *p == '_' || *p == '-'))
			return false;
	if (GetByName(newName)) return false;
	auto *original = GetByName(source);
	auto *copy = original ? original->CloneEditable(newName) : nullptr;
	if (!copy) return false;
	m_Shared.m_NamedSettings.emplace(copy->GetName(), copy);
 SavePersistent();
	return true;
}

bool CRecordingSettings::ApplyEdits(const RecordingProfileInfo &info)
{
	auto *setting = GetByName(info.Name.c_str());
	const bool changed=setting && !setting->GetProtected() && setting->ApplyFields(info);
 if(changed)SavePersistent();
 return changed;
}

bool CRecordingSettings::RemoveEditable(const char *name)
{
	if (!name) return false;
	auto it = m_Shared.m_NamedSettings.find(CNamedSettingKey(name));
	if (it == m_Shared.m_NamedSettings.end() || !it->second.Settings ||
		it->second.Settings->GetProtected())
		return false;
	const bool removed=m_Shared.DeleteIfUnrefrenced(it);
 if(removed)SavePersistent();
 return removed;
}

CRecordingSettings::CShared::CShared()
{
	CRecordingSettings * classicSettings = new CClassicRecordingSettings();
	m_NamedSettings.emplace(classicSettings->GetName(), classicSettings);

	m_DefaultSettings = new CDefaultRecordingSettings(classicSettings);
	m_DefaultSettings->AddRef();
	m_NamedSettings.emplace(m_DefaultSettings->GetName(), m_DefaultSettings);

	{
		CRecordingSettings * settings = new CFfmpegRecordingSettings("afxFfmpeg", true, "-c:v libx264 -preset slow -crf 22 {QUOTE}{AFX_STREAM_PATH}\\\\video.mp4{QUOTE}");
		m_NamedSettings.emplace(settings->GetName(), settings);
	}

	{
		CRecordingSettings * settings = new CFfmpegRecordingSettings("afxFfmpegYuv420p", true, "-c:v libx264 -pix_fmt yuv420p -preset slow -crf 22 {QUOTE}{AFX_STREAM_PATH}\\\\video.mp4{QUOTE}");
		m_NamedSettings.emplace(settings->GetName(), settings);
	}

	{
		CRecordingSettings * settings = new CFfmpegRecordingSettings("afxFfmpegLosslessFast", true, "-c:v libx264rgb -preset ultrafast -crf 0 {QUOTE}{AFX_STREAM_PATH}\\\\video.mp4{QUOTE}");
		m_NamedSettings.emplace(settings->GetName(), settings);
	}

	{
		CRecordingSettings * settings = new CFfmpegRecordingSettings("afxFfmpegLosslessBest", true, "-c:v libx264rgb -preset veryslow -crf 0 {QUOTE}{AFX_STREAM_PATH}\\\\video.mp4{QUOTE}");
		m_NamedSettings.emplace(settings->GetName(), settings);
	}

	{
		CRecordingSettings * settings = new CFfmpegRecordingSettings("afxFfmpegRaw", true, "-c:v rawvideo {QUOTE}{AFX_STREAM_PATH}\\\\video.avi{QUOTE}");
		m_NamedSettings.emplace(settings->GetName(), settings);
	}

	{
		CRecordingSettings * settings = new CFfmpegRecordingSettings("afxFfmpegHuffyuv", true, "-c:v huffyuv {QUOTE}{AFX_STREAM_PATH}\\\\video.avi{QUOTE}");
		m_NamedSettings.emplace(settings->GetName(), settings);
	}

	{
		CRecordingSettings * settings = new CFfmpegRecordingSettings("afxFfmpegProres", true, "-c:v prores_ks -profile:v 4 -pix_fmt yuv444p10le {QUOTE}{AFX_STREAM_PATH}\\video.mov{QUOTE}");
		m_NamedSettings.emplace(settings->GetName(), settings);
	}

	{
		CRecordingSettings * settings = new CFfmpegRecordingSettings("afxFfmpegProresAlpha", true, "-c:v prores_ks -profile:v 4 -pix_fmt yuva444p10le {QUOTE}{AFX_STREAM_PATH}\\video.mov{QUOTE}");
		m_NamedSettings.emplace(settings->GetName(), settings);
	}

	{
  CRecordingSettings *settings=new CFfmpegRecordingSettings("afxFfmpegNvencColorAlpha",true,
   "-filter_complex {QUOTE}[0:v]format=rgba,setsar=1/1,split=2[c0][a0];[c0]format=yuv444p[c];[a0]alphaextract,format=yuv444p[a]{QUOTE} "
   "-map {QUOTE}[c]{QUOTE} -an -c:v h264_nvenc -preset p4 -tune lossless -profile:v high444p -pix_fmt yuv444p {QUOTE}{AFX_STREAM_PATH}\\color.mp4{QUOTE} "
   "-map {QUOTE}[a]{QUOTE} -an -c:v h264_nvenc -preset p4 -tune lossless -profile:v high444p -pix_fmt yuv444p {QUOTE}{AFX_STREAM_PATH}\\alpha.mp4{QUOTE}",
   "-an -c:v h264_nvenc -preset p4 -tune lossless -profile:v high444p -pix_fmt yuv444p {QUOTE}{AFX_STREAM_PATH}\\color.mp4{QUOTE}");
  m_NamedSettings.emplace(settings->GetName(),settings);
 }

	{
		CRecordingSettings * settings = new CSamplingRecordingSettings("afxSampler30", true, m_DefaultSettings, EasySamplerSettings::ESM_Trapezoid, 30.0f, 1.0f, 1.0f);
		m_NamedSettings.emplace(settings->GetName(), settings);
	}
}

CClassicRecordingSettings::CShared::~CShared()
{
	m_NamedSettings.clear();
	m_DefaultSettings->Release();
}

void CRecordingSettings::Console(ICommandArgs * args)
{
 ProfileCommandSave save;
	int argC = args->ArgC();
	const char * arg0 = args->ArgV(0);

	if (2 <= argC)
	{
		const char * arg1 = args->ArgV(1);

		if (0 == _stricmp("print", arg1))
		{
			for (auto it = m_Shared.m_NamedSettings.begin(); it != m_Shared.m_NamedSettings.end(); ++it)
			{
				advancedfx::Message("%s%s\n", it->second.Settings->GetName(), it->second.Settings->GetProtected() ? " (protected)" : "");
			}
			return;
		}
		else if (0 == _stricmp("edit", arg1) && 3 <= argC)
		{
			const char * arg2 = args->ArgV(2);

			if (CRecordingSettings * setting = CRecordingSettings::GetByName(arg2))
			{
				CSubCommandArgs subArgs(args, 3);
				setting->Console_Edit(&subArgs);
			}
			else
			{
				advancedfx::Warning("AFXERROR: There is no recording setting named %s.\n", arg2);
			}
			return;
		}
		else if (0 == _stricmp("remove", arg1) && 3 <= argC)
		{
			const char * arg2 = args->ArgV(2);

			auto it = m_Shared.m_NamedSettings.find(arg2);

			if (it != m_Shared.m_NamedSettings.end())
			{
				if (it->second.Settings->GetProtected())
				{
					advancedfx::Warning("AFXERROR: Setting %s is protected and thus can not be deleted.\n", arg2);
				}
				else if (!m_Shared.DeleteIfUnrefrenced(it))
				{
					advancedfx::Warning("AFXERROR: Could not delete %s, because it has further references.\n", arg2);
				}
			}
			else
			{
				advancedfx::Warning("AFXERROR: There is no recording setting named %s.\n", arg2);
			}
			return;
		}
		else if (0 == _strcmpi("add", arg1))
		{
			if (5 == argC && 0 == _stricmp("ffmpeg", args->ArgV(2)))
			{
				const char * arg3 = args->ArgV(3);

				if (StringIBeginsWith(arg3, "afx"))
				{
					advancedfx::Warning("AFXERROR: Custom presets must not begin with \"afx\".\n");
				}
				else if (nullptr != GetByName(arg3))
				{
					advancedfx::Warning("AFXERROR: There is already a setting named %s\n", arg3);
				}
				else
				{
					CRecordingSettings * settings = new CFfmpegRecordingSettings(arg3, false, args->ArgV(4));
					m_Shared.m_NamedSettings.emplace(settings->GetName(), settings);
				}
				return;
			}
			else if (5 == argC && 0 == _stricmp("ffmpegEx", args->ArgV(2)))
			{
				const char * arg3 = args->ArgV(3);

				if (StringIBeginsWith(arg3, "afx"))
				{
					advancedfx::Warning("AFXERROR: Custom presets must not begin with \"afx\".\n");
				}
				else if (nullptr != GetByName(arg3))
				{
					advancedfx::Warning("AFXERROR: There is already a setting named %s\n", arg3);
				}
				else
				{
					CRecordingSettings * settings = new CFfmpegExRecordingSettings(arg3, false, args->ArgV(4));
					m_Shared.m_NamedSettings.emplace(settings->GetName(), settings);
				}
				return;
			}
			else if (4 == argC && 0 == _stricmp("sampler", args->ArgV(2)))
			{
				const char * arg3 = args->ArgV(3);

				if (StringIBeginsWith(arg3, "afx"))
				{
					advancedfx::Warning("AFXERROR: Custom presets must not begin with \"afx\".\n");
				}
				else if (nullptr != GetByName(arg3))
				{
					advancedfx::Warning("AFXERROR: There is already a setting named %s\n", arg3);
				}
				else
				{
					CRecordingSettings * settings = new CSamplingRecordingSettings(arg3, false, m_Shared.m_DefaultSettings, EasySamplerSettings::ESM_Trapezoid, 30.0f, 1.0f, 1.0f);
					m_Shared.m_NamedSettings.emplace(settings->GetName(), settings);
				}
				return;
			}
			else if (4 == argC && 0 == _stricmp("multi", args->ArgV(2)))
			{
				const char * arg3 = args->ArgV(3);

				if (StringIBeginsWith(arg3, "afx"))
				{
					advancedfx::Warning("AFXERROR: Custom presets must not begin with \"afx\".\n");
				}
				else if (nullptr != GetByName(arg3))
				{
					advancedfx::Warning("AFXERROR: There is already a setting named %s\n", arg3);
				}
				else
				{
					CRecordingSettings * settings = new CMultiRecordingSettings(arg3, false);
					m_Shared.m_NamedSettings.emplace(settings->GetName(), settings);
				}
				return;
			}
			else if (4 <= argC && 0 == _stricmp("interleave", args->ArgV(2)))
			{
				size_t inputCount = argC - 3;

				std::set<std::string> m_InputNames;

				for(size_t i = 0; i < inputCount; i++) {
					const char * argI = args->ArgV(3+i);
					if (StringIBeginsWith(argI, "afx"))
					{
						advancedfx::Warning("AFXERROR: Custom presets must not begin with \"afx\".\n");
						return;
					}
					else if (nullptr != GetByName(argI))
					{
						advancedfx::Warning("AFXERROR: There is already a setting named %s\n", argI);
						return;
					}
					if(!m_InputNames.emplace(argI).second){
						advancedfx::Warning("AFXERROR: There is already am input named %s, names must be unique\n", argI);
						return;
					}
				}

				auto it = m_InputNames.begin();

				CInterleaveRecordingSettings * settings = new CInterleaveRecordingSettings(it->c_str(), false, m_Shared.m_DefaultSettings, m_InputNames.size());
				m_Shared.m_NamedSettings.emplace(it->c_str(), settings);
				it++;

				size_t index = 0;
				while(it != m_InputNames.end()) {
					index++;
					m_Shared.m_NamedSettings.emplace(it->c_str(), new CInterleaveInputRecordingSettings(it->c_str(), false, settings, index));
					it++;
				}
				
				return;
			}

			advancedfx::Message(
				"%s add ffmpeg <name> \"<yourOptionsHere>\" - Adds an FFMPEG setting, <yourOptionsHere> are output options, use {QUOTE} for \", {AFX_STREAM_PATH} for the folder path of the stream, \\{ for {, \\} for }. For an example see one of the afxFfmpeg* templates (edit them).\n"
				"%s add ffmpegEx <name> \"<yourOptionsHere>\" - Adds an extended FFMPEG setting, <yourOptionsHere> are output options, use {QUOTE} for \", {AFX_STREAM_PATH} for the folder path of the stream, \\{ for {, \\} for }. Further variables: {FFMPEG_PATH} {PIXEL_FORMAT} {FRAMERATE} {WIDTH} {HEIGHT} - For an example see one of the afxFfmpeg* templates (edit them).\n"
				"%s add sampler <name> - Adds a sampler with 30 fps and default settings, edit it afterwards to change them.\n"
				"%s add multi <name> - Adds multi settings, edit it afterwards to add settings to it.\n"
				"%s add interleave <name> (<nameX>)* - Adds a video interleave setting, named <name>, which is also the first of multiple possible inputs, with optional further inputs with the given names, that are ordered in the order given. You can edit it afterwards to change the default output settings to s.th. else.\n"
				, arg0
				, arg0
				, arg0
				, arg0
				, arg0
			);
			return;
		}
	}

	advancedfx::Message(
		"%s print - List currently registered settings\n"
		"%s edit <name> - Edit setting.\n"
		"%s remove <name> - Remove setting.\n"
		"%s add [...] - Add a setting.\n"
		, arg0
		, arg0
		, arg0
		, arg0
	);
}


// CClassicRecordingSettings ////////////////////////////////////////////////

void CClassicRecordingSettings::Console_Edit(ICommandArgs * args)
{
	advancedfx::Message("%s (type classic) recording setting options:\n", m_Name.c_str());
	advancedfx::Warning("The classic settings are controlled through mirv_streams settings and can not be edited.\n");
}

advancedfx::COutVideoStreamCreator * CClassicRecordingSettings::CreateOutVideoStreamCreator(const IRecordStreamSettings & streams, const IRecordStreamSettings& stream, float frameRate, const char * pathSuffix)
{
	std::wstring capturePath;
	if (stream.GetStreamFolder(capturePath)) {
		std::wstring widePathSuffix;
		if (UTF8StringToWideString(pathSuffix, widePathSuffix))
		{
			capturePath.append(widePathSuffix);

			advancedfx::StreamCaptureType captureType = stream.GetCaptureType();

			auto result = new advancedfx::CClassicRecordingSettingsCreator(capturePath, (captureType == advancedfx::StreamCaptureType::Depth24ZIP || captureType == advancedfx::StreamCaptureType::DepthFZIP), streams.GetFormatBmpNotTga());
			result->AddRef();
			return result;
		}
		else
		{
			advancedfx::Warning("AFXERROR: Could not convert \"%s\" from UTF8 to wide string.\n", pathSuffix);
		}
	}

	return nullptr;
}

// CFfmpegRecordingSettings ////////////////////////////////////////////////

bool CFfmpegRecordingSettings::ApplyFields(const RecordingProfileInfo &info)
{
	if (m_Protected || info.Type != RecordingProfileInfo::Kind::Ffmpeg ||
		info.FfmpegOptions.empty() || info.FfmpegOptions.size() > 4095 || info.FfmpegOptionsNoAlpha.size()>4095)
		return false;
	m_FfmpegOptions = info.FfmpegOptions;
 m_NoAlphaOptions = info.FfmpegOptionsNoAlpha;
	return true;
}

void CFfmpegRecordingSettings::Console_Edit(ICommandArgs * args)
{
	int argC = args->ArgC();
	const char * arg0 = args->ArgV(0);

	if (2 <= argC)
	{
		const char * arg1 = args->ArgV(1);

		if (0 == _stricmp("options", arg1))
		{
			if (3 <= argC)
			{
				if (m_Protected)
				{
					advancedfx::Warning("This setting is protected and can not be changed.\n");
					return;
				}

				m_FfmpegOptions = "";
				for(int i = 2; i < argC; i++) {
					if(2 < i ) m_FfmpegOptions += " ";
					m_FfmpegOptions += args->ArgV(i);
				}
				return;
			}

			advancedfx::Message(
				"%s options \"<yourOptionsHere>\" - Set output options, use {QUOTE} for \", {AFX_STREAM_PATH} for the folder path of the stream, \\{ for {, \\} for }.\n"
				"Current value: \"%s\"\n"
				, arg0
				, m_FfmpegOptions.c_str()
			);
			return;
		}
		else if (0 == _stricmp("options+", arg1))
		{
			if (3 <= argC)
			{
				if (m_Protected)
				{
					advancedfx::Warning("This setting is protected and can not be changed.\n");
					return;
				}

				for(int i = 2; i < argC; i++) {
					if(2 < i ) m_FfmpegOptions += " ";
					m_FfmpegOptions += args->ArgV(i);
				}
				return;
			}

			advancedfx::Message(
				"%s options+ \"<yourOptionsHere>\" - Append to options).\n"
				, arg0
			);
			return;
		}			
	}

	advancedfx::Message("%s (type ffmpeg) recording setting options:\n", m_Name.c_str());
	advancedfx::Message(
		"%s options [...] - FFMPEG options.\n"
		"%s options+ [...] - Append to FFMPEG options.\n"
		, arg0
		, arg0
	);
}

advancedfx::COutVideoStreamCreator* CFfmpegRecordingSettings::CreateOutVideoStreamCreator(const IRecordStreamSettings & streams, const IRecordStreamSettings& stream, float frameRate, const char * pathSuffix)
{
	std::wstring widePathSuffix;
	if (UTF8StringToWideString(pathSuffix, widePathSuffix))
	{
		std::wstring wideOptions;
		if (UTF8StringToWideString(m_FfmpegOptions.c_str(), wideOptions))
		{
			std::wstring capturePath;
			if (stream.GetStreamFolder(capturePath)) {
				capturePath.append(widePathSuffix);

				advancedfx::StreamCaptureType captureType = stream.GetCaptureType();

    auto command=[](const std::wstring &options){
     std::wstring result=L"{QUOTE}{FFMPEG_PATH}{QUOTE} -f rawvideo -pixel_format {PIXEL_FORMAT} -loglevel repeat+level+warning -framerate {FRAMERATE} -video_size {WIDTH}x{HEIGHT} -i pipe:0 ";
     if(options.find(L"-filter_complex")==std::wstring::npos) result+=L"-vf setsar=sar=1/1 ";
     return result+options;
    };
    std::wstring noAlpha;
    if(!m_NoAlphaOptions.empty() && !UTF8StringToWideString(m_NoAlphaOptions.c_str(),noAlpha))return nullptr;
    auto result = new advancedfx::CFfmpegRecordingSettingsCreator(capturePath, command(wideOptions), frameRate, noAlpha.empty()?L"":command(noAlpha));
				result->AddRef();
				return result;
			}
		}
		else
		{
			advancedfx::Warning("AFXERROR: Could not convert \"%s\" from UTF8 to wide string.\n", m_FfmpegOptions.c_str());
		}
	}
	else
	{
		advancedfx::Warning("AFXERROR: Could not convert \"%s\" from UTF8 to wide string.\n", pathSuffix);
	}

	return nullptr;
}



// CFfmpegRecordingSettings ////////////////////////////////////////////////

bool CFfmpegExRecordingSettings::ApplyFields(const RecordingProfileInfo &info)
{
	if (m_Protected || info.Type != RecordingProfileInfo::Kind::FfmpegEx ||
		info.FfmpegOptions.empty() || info.FfmpegOptions.size() > 4095)
		return false;
	m_FfmpegOptions = info.FfmpegOptions;
	return true;
}

void CFfmpegExRecordingSettings::Console_Edit(ICommandArgs * args)
{
	int argC = args->ArgC();
	const char * arg0 = args->ArgV(0);

	if (2 <= argC)
	{
		const char * arg1 = args->ArgV(1);

		if (0 == _stricmp("options", arg1))
		{
			if (3 <= argC)
			{
				if (m_Protected)
				{
					advancedfx::Warning("This setting is protected and can not be changed.\n");
					return;
				}

				m_FfmpegOptions = "";
				for(int i = 2; i < argC; i++) {
					if(2 < i ) m_FfmpegOptions += " ";
					m_FfmpegOptions += args->ArgV(i);
				}
				return;
			}

			advancedfx::Message(
				"%s options \"<yourOptionsHere>\" - Set output options use {QUOTE} for \", {AFX_STREAM_PATH} for the folder path of the stream, \\{ for {, \\} for }. Further variables: {FFMPEG_PATH} {PIXEL_FORMAT} {FRAMERATE} {WIDTH} {HEIGHT}\n"
				"Current value: \"%s\"\n"
				, arg0
				, m_FfmpegOptions.c_str()
			);
			return;
		}
		else if (0 == _stricmp("options+", arg1))
		{
			if (3 <= argC)
			{
				if (m_Protected)
				{
					advancedfx::Warning("This setting is protected and can not be changed.\n");
					return;
				}

				for(int i = 2; i < argC; i++) {
					if(2 < i ) m_FfmpegOptions += " ";
					m_FfmpegOptions += args->ArgV(i);
				}
				return;
			}

			advancedfx::Message(
				"%s options+ \"<yourOptionsHere>\" - Append to options.\n"
				, arg0
			);
			return;
		}		
	}

	advancedfx::Message("%s (type ffmpegEx) recording setting options:\n", m_Name.c_str());
	advancedfx::Message(
		"%s options [...] - FFMPEG options.\n"
		"%s options+ [...] - Append FFMPEG options.\n"
		, arg0
		, arg0
	);
}

advancedfx::COutVideoStreamCreator* CFfmpegExRecordingSettings::CreateOutVideoStreamCreator(const IRecordStreamSettings & streams, const IRecordStreamSettings& stream, float frameRate, const char * pathSuffix)
{
	std::wstring widePathSuffix;
	if (UTF8StringToWideString(pathSuffix, widePathSuffix))
	{
		std::wstring wideOptions;
		if (UTF8StringToWideString(m_FfmpegOptions.c_str(), wideOptions))
		{
			std::wstring capturePath;
			if (stream.GetStreamFolder(capturePath)) {
				capturePath.append(widePathSuffix);

				advancedfx::StreamCaptureType captureType = stream.GetCaptureType();

				auto result = new advancedfx::CFfmpegRecordingSettingsCreator(capturePath, wideOptions, frameRate);
				result->AddRef();
				return result;
			}
		}
		else
		{
			advancedfx::Warning("AFXERROR: Could not convert \"%s\" from UTF8 to wide string.\n", m_FfmpegOptions.c_str());
		}
	}
	else
	{
		advancedfx::Warning("AFXERROR: Could not convert \"%s\" from UTF8 to wide string.\n", pathSuffix);
	}

	return nullptr;
}


// CDefaultRecordingSettings ////////////////////////////////////////////////

void CDefaultRecordingSettings::Console_Edit(ICommandArgs * args)
{
	int argC = args->ArgC();
	const char * arg0 = args->ArgV(0);

	if (2 <= argC)
	{
		const char * arg1 = args->ArgV(1);

		if (0 == _stricmp("settings", arg1))
		{
			if (3 == argC)
			{
				CRecordingSettings * settings = CRecordingSettings::GetByName(args->ArgV(2));

				if (nullptr == settings)
				{
					advancedfx::Warning("AFXERROR: There is no setting named %s.\n", args->ArgV(2));
				}
				else if(settings->InheritsFrom(this))
				{
					advancedfx::Warning("AFXERROR: Can not assign a setting that depends on this setting.\n");
				}
				else
				{
					SetOutputSettings(settings);
				}

				return;
			}

			advancedfx::Message(
				"%s settings <settingsName> - Use settings with name <settingsName> as default settings.\n"
				"Current value: \"%s\"\n"
				, arg0
				, m_DefaultSettings ? m_DefaultSettings->GetName() : "[null]"
			);
			return;
		}
	}

	advancedfx::Message("%s (type default) recording setting options:\n", m_Name.c_str());
	advancedfx::Message(
		"%s settings [...] - Set default settings.\n"
		, arg0
	);
}

// CMultiRecordingSettings //////////////////////////////////////////////////


void CMultiRecordingSettings::Console_Edit(ICommandArgs * args)
{
	int argC = args->ArgC();
	const char * arg0 = args->ArgV(0);

	if (2 <= argC)
	{
		const char * arg1 = args->ArgV(1);

		if (0 == _stricmp("add", arg1))
		{
			if (3 == argC)
			{
				CRecordingSettings * settings = CRecordingSettings::GetByName(args->ArgV(2));

				if (nullptr == settings)
				{
					advancedfx::Warning("AFXERROR: There is no setting named %s.\n", args->ArgV(2));
				}
				else if (settings->InheritsFrom(this))
				{
					advancedfx::Warning("AFXERROR: Can not assign a setting that depends on this setting.\n");
				}
				else
				{
					if (settings) settings->AddRef();
					m_Settings.push_back(settings);
				}

				return;
			}

			advancedfx::Message(
				"%s add <settingsName> - Add settings with name <settingsName>.\n"
				, arg0
			);
			return;
		}
		else if (0 == _stricmp("remove", arg1))
		{
			if (3 == argC)
			{
				CRecordingSettings * settings = CRecordingSettings::GetByName(args->ArgV(2));

				if (nullptr == settings)
				{
					advancedfx::Warning("AFXERROR: There is no setting named %s.\n", args->ArgV(2));
				}
				else
				{
					for (auto it = m_Settings.begin(); it != m_Settings.end(); )
					{
						CRecordingSettings * itSettings = *it;
						if (itSettings == settings)
						{
							it = m_Settings.erase(it);
							itSettings->Release();
						}
						else
						{
							++it;
						}
					}
				}

				return;
			}

			advancedfx::Message(
				"%s remove <settingsName> - Remove settings with name <settingsName>.\n"
				, arg0
			);
			return;
		}
		else if (0 == _stricmp("print", arg1))
		{
			int idx = 0;
			for (auto it = m_Settings.begin(); it != m_Settings.end(); ++it)
			{
				CRecordingSettings * itSettings = *it;
				advancedfx::Message("%i: %s\n", idx, itSettings ? itSettings->GetName() : "[null]");
				++idx;
			}
			if (0 == idx) advancedfx::Message("[empty]\n");
			return;
		}
	}

	advancedfx::Message("%s (type multi) recording setting options:\n", m_Name.c_str());
	advancedfx::Message(
		"%s add <settingsName> - Add settings.\n"
		"%s remove <settingsName> - Remove settings.\n"
		"%s print <settingsName> - List settings.\n"
		, arg0
		, arg0
		, arg0
	);
}

// CSamplingRecordingSettings ///////////////////////////////////////////////



advancedfx::COutVideoStreamCreator* CSamplingRecordingSettings::CreateOutVideoStreamCreator(const IRecordStreamSettings & streams, const IRecordStreamSettings& stream, float frameRate, const char * pathSuffix)
{
	if (m_OutputSettings)
	{
		if (advancedfx::COutVideoStreamCreator* outVideoStreamCreator = m_OutputSettings->CreateOutVideoStreamCreator(streams, stream, m_OutFps, pathSuffix))
		{
			auto result = new advancedfx::CSamplingRecordingSettingsCreator(outVideoStreamCreator, frameRate, m_Method, m_OutFps ? 1.0 / m_OutFps : 0.0, m_Exposure, m_FrameStrength, streams.GetImageBufferPool());
			result->AddRef();
			return result;
		}
	}

	return nullptr;
}

bool CSamplingRecordingSettings::ApplyFields(const RecordingProfileInfo &info)
{
	if (m_Protected || info.Type != RecordingProfileInfo::Kind::Sampler ||
		!std::isfinite(info.OutputFps) || info.OutputFps <= 0 || info.OutputFps > 10000 ||
		!std::isfinite(info.Exposure) || info.Exposure < 0 || info.Exposure > 1 ||
		!std::isfinite(info.Strength) || info.Strength < 0 || info.Strength > 1 ||
		(info.Method != 0 && info.Method != 1))
		return false;
	auto *output = CRecordingSettings::GetByName(info.OutputSettings.c_str());
	if (!output || output->InheritsFrom(this))
		return false; // Reject self and indirect nested sampler cycles.
	output->AddRef();
	auto *previous = m_OutputSettings;
	m_OutputSettings = output;
	m_OutFps = info.OutputFps;
	m_Method = info.Method == 0 ? EasySamplerSettings::ESM_Rectangle : EasySamplerSettings::ESM_Trapezoid;
	m_Exposure = info.Exposure;
	m_FrameStrength = info.Strength;
	if (previous) previous->Release();
	return true;
}

void CSamplingRecordingSettings::Console_Edit(ICommandArgs * args)
{
	int argC = args->ArgC();
	const char * arg0 = args->ArgV(0);

	if (2 <= argC)
	{
		const char * arg1 = args->ArgV(1);

		if (0 == _stricmp("settings", arg1))
		{
			if (3 == argC)
			{
				CRecordingSettings * settings = CRecordingSettings::GetByName(args->ArgV(2));

				if (nullptr == settings)
				{
					advancedfx::Warning("AFXERROR: There is no setting named %s.\n", args->ArgV(2));
				}
				else if (settings->InheritsFrom(this))
				{
					advancedfx::Warning("AFXERROR: Can not assign a setting that depends on this setting.\n");
				}
				else
				{
					if (m_OutputSettings) m_OutputSettings->Release();
					m_OutputSettings = settings;
					if (m_OutputSettings) m_OutputSettings->AddRef();
				}

				return;
			}

			advancedfx::Message(
				"%s settings <settingsName> - Use settings with name <settingsName> as output settings.\n"
				"Current value: \"%s\"\n"
				, arg0
				, m_OutputSettings ? m_OutputSettings->GetName() : "[null]"
			);
			return;
		}
		else if (0 == _stricmp("fps", arg1))
		{
			if (3 == argC)
			{
				if (m_Protected)
				{
					advancedfx::Warning("This setting is protected and can not be changed.\n");
					return;
				}

				m_OutFps = (float)atof(args->ArgV(2));
				return;
			}

			advancedfx::Message(
				"%s fps <fValue> - Output FPS, has to be greater than 0.\n"
				"Current value: %f\n"
				, arg0
				, m_OutFps
			);
			return;
		}
		else if (0 == _stricmp("method", arg1))
		{
			if (3 == argC)
			{
				if (m_Protected)
				{
					advancedfx::Warning("This setting is protected and can not be changed.\n");
					return;
				}

				const char * arg2 = args->ArgV(2);

				if (0 == _stricmp(arg2, "rectangle"))
				{
					m_Method = EasySamplerSettings::ESM_Rectangle;
				}
				else if (0 == _stricmp(arg2, "trapezoid"))
				{
					m_Method = EasySamplerSettings::ESM_Trapezoid;
				}
				else
				{
					advancedfx::Warning("AFXERROR: Invalid value.\n");
				}

				return;
			}

			const char * curMethod = "[n/a]";

			switch (m_Method)
			{
			case EasySamplerSettings::ESM_Rectangle:
				curMethod = "rectangle";
				break;
			case EasySamplerSettings::ESM_Trapezoid:
				curMethod = "trapezoid";
				break;
			};

			advancedfx::Message(
				"%s method rectangle|trapezoid.\n"
				"Current value: %s\n"
				, arg0
				, curMethod
			);
			return;
		}
		else if (0 == _stricmp("exposure", arg1))
		{
			if (3 == argC)
			{
				if (m_Protected)
				{
					advancedfx::Warning("This setting is protected and can not be changed.\n");
					return;
				}

				m_Exposure = atof(args->ArgV(2));
				return;
			}

			advancedfx::Message(
				"%s exposure <fValue>.\n"
				"Current value: %f\n"
				, arg0
				, m_Exposure
			);
			return;
		}
		else if (0 == _stricmp("strength", arg1))
		{
			if (3 == argC)
			{
				if (m_Protected)
				{
					advancedfx::Warning("This setting is protected and can not be changed.\n");
					return;
				}

				m_FrameStrength = (float)atof(args->ArgV(2));
				return;
			}

			advancedfx::Message(
				"%s strength <fValue>.\n"
				"Current value: %f\n"
				, arg0
				, m_FrameStrength
			);
			return;
		}
	}

	advancedfx::Message("%s (type sampling) recording setting options:\n", m_Name.c_str());
	advancedfx::Message(
		"%s settings [...] - Output settings.\n"
		"%s fps [...] - Output fps.\n"
		"%s method [...] - Sampling method (default: trapezoid).\n"
		"%s exposure [...] - Frame exposure (0.0 (0\xc2\xb0 shutter angle) - 1.0 (360\xc2\xb0 shutter angle), default: 1.0).\n"
		"%s strength [...] - Frame strength (0.0 (max cross-frame blur) - 1.0 (no cross-frame blur), default: 1.0).\n"
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
	);
}


// CInterleaveInputRecordingSettings ///////////////////////////////////////////

CInterleaveInputRecordingSettings::CInterleaveInputRecordingSettings(const char * name, bool bProtected, CInterleaveRecordingSettings * pMainInterleaveInput, int index)
	: CRecordingSettings(name, bProtected)
	, m_pMainInterleaveInput(pMainInterleaveInput)
	, m_Index(index)
{
	m_pMainInterleaveInput->AddRef();
}

void CInterleaveInputRecordingSettings::DescribeFields(RecordingProfileInfo &out) const {out.Type=RecordingProfileInfo::Kind::InterleaveInput;out.OutputSettings=m_pMainInterleaveInput?m_pMainInterleaveInput->GetName():"";out.InputIndex=m_Index;}
void CInterleaveInputRecordingSettings::Console_Edit(ICommandArgs * args) {
	advancedfx::Message("%s (type interleave-other-input) has no options.\n", m_Name.c_str());
}

class advancedfx::COutVideoStreamCreator* CInterleaveInputRecordingSettings::CreateOutVideoStreamCreator(const IRecordStreamSettings & streams, const IRecordStreamSettings& stream, float fps, const char * pathSuffix)
{
	return m_pMainInterleaveInput->InputCreateOutVideoStreamCreator(streams, stream, fps, pathSuffix, m_Index);
}

bool CInterleaveInputRecordingSettings::InheritsFrom(CRecordingSettings * setting) const
{
	if (CRecordingSettings::InheritsFrom(setting)) return true;

	if (m_pMainInterleaveInput) if (m_pMainInterleaveInput->InheritsFrom(setting)) return true;

	return false;
}	

CInterleaveInputRecordingSettings::~CInterleaveInputRecordingSettings()
{
	m_pMainInterleaveInput->Release();
}

// CInterleaveRecordingSettings ////////////////////////////////////////////////

void CInterleaveRecordingSettings::Console_Edit(ICommandArgs * args)
{
	int argC = args->ArgC();
	const char * arg0 = args->ArgV(0);

	if (2 <= argC)
	{
		const char * arg1 = args->ArgV(1);

		if (0 == _stricmp("settings", arg1))
		{
			if (3 == argC)
			{
				CRecordingSettings * settings = CRecordingSettings::GetByName(args->ArgV(2));

				if (nullptr == settings)
				{
					advancedfx::Warning("AFXERROR: There is no setting named %s.\n", args->ArgV(2));
				}
				else if (settings->InheritsFrom(this))
				{
					advancedfx::Warning("AFXERROR: Can not assign a setting that depends on this setting.\n");
				}
				else
				{
					if (m_OutputSettings) m_OutputSettings->Release();
					m_OutputSettings = settings;
					if (m_OutputSettings) m_OutputSettings->AddRef();
				}

				return;
			}

			advancedfx::Message(
				"%s settings <settingsName> - Use settings with name <settingsName> as output settings.\n"
				"Current value: \"%s\"\n"
				, arg0
				, m_OutputSettings ? m_OutputSettings->GetName() : "[null]"
			);
			return;
		}
	}

	advancedfx::Message("%s (type interleave) recording setting options:\n", m_Name.c_str());
	advancedfx::Message(
		"%s settings [...] - Output settings.\n"
		, arg0
	);
}



} // namespace advancedfx
