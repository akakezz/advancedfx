"""Round-trip actual profile persistence methods through fresh process registries."""
from pathlib import Path
import argparse,os,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--build-dir',type=Path,default=Path(tempfile.gettempdir())/'hlae-recording-persistence');a=p.parse_args();a.build_dir.mkdir(parents=True,exist_ok=True)
r=Path(__file__).resolve().parents[2];s=(r/'shared/RecordingSettings.cpp').read_text();methods=s[s.index('namespace {\n std::filesystem::path g_ProfileFile'):s.index('std::vector<std::string> CRecordingSettings::GetNames()')]
source=r'''
#include "shared/RecordingProfilePersistence.h"
#include <filesystem>
#include <fstream>
#include <cassert>
#include <memory>
#include <cstring>
#include <iostream>
namespace advancedfx {
void Warning(const char*,...){}
namespace EasySamplerSettings{enum Method{ESM_Rectangle,ESM_Trapezoid};}
class CRecordingSettings {
public:
 RecordingProfileInfo Info;CRecordingSettings*Output=nullptr;std::vector<CRecordingSettings*>Children;
 CRecordingSettings(const char*n,bool p){Info.Name=n;Info.Protected=p;}virtual ~CRecordingSettings()=default;
 const char*GetName()const{return Info.Name.c_str();}
 bool InheritsFrom(CRecordingSettings*s)const {if(this==s)return true;if(Output&&Output->InheritsFrom(s))return true;for(auto x:Children)if(x->InheritsFrom(s))return true;return false;}
 void AddRef(){}void Release(){delete this;}
 struct Less{bool operator()(const std::string&a,const std::string&b)const{return RecordingProfilePersistence::Key(a)<RecordingProfilePersistence::Key(b);}};
 struct Shared {std::map<std::string,CRecordingSettings*,Less>m_NamedSettings;CRecordingSettings*m_DefaultSettings=nullptr;~Shared(){for(auto x:m_NamedSettings)delete x.second;}};static Shared m_Shared;
 static CRecordingSettings*GetByName(const char*n){auto i=m_Shared.m_NamedSettings.find(n);return i==m_Shared.m_NamedSettings.end()?nullptr:i->second;}
 static std::vector<std::string>GetNames(){std::vector<std::string>v;for(auto x:m_Shared.m_NamedSettings)v.push_back(x.second->GetName());return v;}
 static bool Describe(const char*n,RecordingProfileInfo&out){auto s=GetByName(n);if(!s)return false;out=s->Info;if(s->Output)out.OutputSettings=s->Output->GetName();return true;}
 static std::string ProfileSnapshot();static std::string PreferredProfile();static void SetPreferredProfile(const char*);static void SavePersistent();static bool ConfigurePersistence(const wchar_t*);
};CRecordingSettings::Shared CRecordingSettings::m_Shared;
struct CDefaultRecordingSettings:CRecordingSettings{CDefaultRecordingSettings(CRecordingSettings*p):CRecordingSettings("afxDefault",true){Output=p;}bool SetOutputSettings(CRecordingSettings*p){if(!p||p->InheritsFrom(this))return false;Output=p;return true;}};
struct CFfmpegRecordingSettings:CRecordingSettings{CFfmpegRecordingSettings(const char*n,bool p,const char*o,const char*rgb=""):CRecordingSettings(n,p){Info.Type=RecordingProfileInfo::Kind::Ffmpeg;Info.FfmpegOptions=o;Info.FfmpegOptionsNoAlpha=rgb;}};
struct CFfmpegExRecordingSettings:CFfmpegRecordingSettings{CFfmpegExRecordingSettings(const char*n,bool p,const char*o):CFfmpegRecordingSettings(n,p,o){Info.Type=RecordingProfileInfo::Kind::FfmpegEx;}};
struct CSamplingRecordingSettings:CRecordingSettings{CSamplingRecordingSettings(const char*n,bool p,CRecordingSettings*o,EasySamplerSettings::Method m,float fps,float exp,float str):CRecordingSettings(n,p){Info.Type=RecordingProfileInfo::Kind::Sampler;Output=o;Info.Method=int(m);Info.OutputFps=fps;Info.Exposure=exp;Info.Strength=str;}};
struct CMultiRecordingSettings:CRecordingSettings{CMultiRecordingSettings(const char*n,bool p):CRecordingSettings(n,p){Info.Type=RecordingProfileInfo::Kind::Multi;}bool ApplyFields(const RecordingProfileInfo&i){Info=i;for(const auto&n:i.Outputs){auto p=GetByName(n.c_str());if(!p)return false;Children.push_back(p);}return true;}};
struct CInterleaveRecordingSettings:CRecordingSettings{CInterleaveRecordingSettings(const char*n,bool p,CRecordingSettings*o,size_t count):CRecordingSettings(n,p){Info.Type=RecordingProfileInfo::Kind::Interleave;Output=o;Info.InputCount=count;}};
struct CInterleaveInputRecordingSettings:CRecordingSettings{CInterleaveInputRecordingSettings(const char*n,bool p,CInterleaveRecordingSettings*o,int index):CRecordingSettings(n,p){Info.Type=RecordingProfileInfo::Kind::InterleaveInput;Output=o;Info.InputIndex=index;}};
'''+methods+r'''
}
int main(int argc,char**argv){using namespace advancedfx;assert(argc==3);std::filesystem::path file=argv[1];std::string mode=argv[2];
 auto add=[](CRecordingSettings*p){CRecordingSettings::m_Shared.m_NamedSettings.emplace(p->GetName(),p);return p;};
 auto classic=add(new CRecordingSettings("afxClassic",true));auto defaults=static_cast<CDefaultRecordingSettings*>(add(new CDefaultRecordingSettings(classic)));CRecordingSettings::m_Shared.m_DefaultSettings=defaults;
 add(new CSamplingRecordingSettings("afxSampler30",true,defaults,EasySamplerSettings::ESM_Trapezoid,30,1,1));
 if(mode=="bad"){
  assert(!CRecordingSettings::ConfigurePersistence(file.wstring().c_str()));assert(CRecordingSettings::GetNames().size()==3);CRecordingSettings::SavePersistent();return 0;
 }
 if(mode=="missing"||mode=="implicitcycle"){
  RecordingProfilePersistence::Snapshot x;RecordingProfileInfo i;i.Name="sampler";i.Type=RecordingProfileInfo::Kind::Sampler;i.Protected=false;i.OutputSettings=mode=="missing"?"notPresent":"afxSampler30";x.Profiles.push_back(i);if(mode=="implicitcycle")x.DefaultOutput="sampler";
  {std::ofstream out(file);out<<RecordingProfilePersistence::Encode(x);}
  assert(!CRecordingSettings::ConfigurePersistence(file.wstring().c_str())&&CRecordingSettings::GetNames().size()==3);return 0;
 }
 assert(CRecordingSettings::ConfigurePersistence(file.wstring().c_str()));
 const std::string options="-c:v libx264 {QUOTE}C:\\capture folder\\video.mp4{QUOTE}\nquoted \"text\" ";
 if(mode=="seed"){
  auto encoder=add(new CFfmpegRecordingSettings("encoder",false,options.c_str(),"color-only"));
  add(new CSamplingRecordingSettings("zSampler",false,encoder,EasySamplerSettings::ESM_Trapezoid,59.94f,0.5f,0.75f));
  auto multi=new CMultiRecordingSettings("bundle",false);RecordingProfileInfo info;info.Name="bundle";info.Type=RecordingProfileInfo::Kind::Multi;info.Protected=false;info.Outputs={"zSampler","encoder"};assert(multi->ApplyFields(info));add(multi);
  auto inter=static_cast<CInterleaveRecordingSettings*>(add(new CInterleaveRecordingSettings("inter",false,encoder,2)));add(new CInterleaveInputRecordingSettings("second",false,inter,1));
  add(new CFfmpegExRecordingSettings("ex",false,"full ffmpeg command"));assert(defaults->SetOutputSettings(encoder));CRecordingSettings::SetPreferredProfile("bundle");CRecordingSettings::SavePersistent();
 }else{
  assert(mode=="load"&&CRecordingSettings::PreferredProfile()=="bundle");auto encoder=CRecordingSettings::GetByName("encoder");assert(encoder&&encoder->Info.FfmpegOptions==options&&encoder->Info.FfmpegOptionsNoAlpha=="color-only");
  auto sampler=CRecordingSettings::GetByName("zSampler");assert(sampler->Output==encoder&&sampler->Info.OutputFps==59.94f&&sampler->Info.Exposure==0.5);
  assert(defaults->Output==encoder);assert(CRecordingSettings::GetByName("bundle")->Children.size()==2);
  assert(CRecordingSettings::GetByName("second")->Output==CRecordingSettings::GetByName("inter"));assert(CRecordingSettings::GetByName("second")->Info.InputIndex==1);
 }
 RecordingProfilePersistence::Snapshot decoded;assert(RecordingProfilePersistence::Decode(CRecordingSettings::ProfileSnapshot(),decoded));auto invalid=decoded;invalid.Profiles[0].Name="afxProtected";assert(!RecordingProfilePersistence::Validate(invalid));
 std::cout<<"Recording profiles/default route/selection round-trip across sessions via the production store\n";
}
'''
f=a.build_dir/'profiles.cpp';f.write_text(source);cmd=['g++','-std=c++17','-O1','-g','-I'+str(r),str(f),'-o',str(a.build_dir/'profiles')]
if a.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
subprocess.run(cmd,check=True);env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
def run(file,mode):subprocess.run([str(a.build_dir/'profiles'),str(file),mode],env=env,check=True)
file=a.build_dir/'RecordingProfiles.txt';file.unlink(missing_ok=True);run(file,'seed');run(file,'load');original=file.read_bytes();file.write_text('corrupt data');run(file,'bad');assert file.read_text()=='corrupt data'
run(a.build_dir/'missing.txt','missing');run(a.build_dir/'cycle.txt','implicitcycle');file.write_bytes(original)
