#pragma once
#include "RecordingProfileInfo.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace advancedfx { namespace RecordingProfilePersistence {
struct Snapshot { std::string Selected="afxDefault",DefaultOutput="afxClassic";std::vector<RecordingProfileInfo> Profiles; };
inline std::string Key(std::string name){for(auto &c:name)c=char(std::tolower(static_cast<unsigned char>(c)));return name;}
inline bool Validate(const Snapshot &s){
 using Kind=RecordingProfileInfo::Kind;
 if(s.Profiles.size()>256 || s.Selected.size()>255 || s.DefaultOutput.empty() || s.DefaultOutput.size()>255)return false;
 std::map<std::string,const RecordingProfileInfo*>profiles;
 for(const auto&p:s.Profiles){
  const auto key=Key(p.Name);
  if(key.empty()||key.size()>255||key.compare(0,3,"afx")==0||!profiles.emplace(key,&p).second)return false;
  if(p.FfmpegOptions.size()>4095||p.FfmpegOptionsNoAlpha.size()>4095||p.OutputSettings.size()>255||p.Outputs.size()>256)return false;
  if(p.Type==Kind::Ffmpeg||p.Type==Kind::FfmpegEx){if(p.FfmpegOptions.empty())return false;}
  else if(p.Type==Kind::Sampler){
   if(p.OutputSettings.empty()||!std::isfinite(p.OutputFps)||p.OutputFps<=0||p.OutputFps>10000||!std::isfinite(p.Exposure)||p.Exposure<0||p.Exposure>1||!std::isfinite(p.Strength)||p.Strength<0||p.Strength>1||(p.Method!=0&&p.Method!=1))return false;
  } else if(p.Type==Kind::Multi){for(const auto&n:p.Outputs)if(n.empty()||n.size()>255)return false;}
  else if(p.Type==Kind::Interleave){if(p.OutputSettings.empty()||p.InputCount<1||p.InputCount>256)return false;}
  else if(p.Type==Kind::InterleaveInput){if(p.OutputSettings.empty()||p.InputIndex<1||p.InputIndex>255)return false;}
  else return false;
 }
 std::map<std::string,std::set<int>>indices;
 for(const auto&p:s.Profiles)if(p.Type==Kind::InterleaveInput){
  auto parent=profiles.find(Key(p.OutputSettings));
  if(parent==profiles.end()||parent->second->Type!=Kind::Interleave||p.InputIndex>=parent->second->InputCount||!indices[parent->first].insert(p.InputIndex).second)return false;
 }
 for(const auto&p:profiles)if(p.second->Type==Kind::Interleave && indices[p.first].size()!=size_t(p.second->InputCount-1))return false;
 RecordingProfileInfo defaults;defaults.Type=Kind::Sampler;defaults.OutputSettings=s.DefaultOutput;profiles["afxdefault"]=&defaults;
 std::map<std::string,int>state;
 std::function<bool(const std::string&)>visit=[&](const std::string&name){
  if(state[name]==1)return false;if(state[name]==2)return true;state[name]=1;
  auto it=profiles.find(name);
  if(it!=profiles.end()){
   const auto&p=*it->second;
   if(p.Type==Kind::Multi){for(const auto&n:p.Outputs)if(!visit(Key(n)))return false;}
   else if(p.Type==Kind::Sampler||p.Type==Kind::Interleave||p.Type==Kind::InterleaveInput){if(!visit(Key(p.OutputSettings)))return false;}
  }
  state[name]=2;return true;
 };
 for(const auto&p:profiles)if(!visit(p.first))return false;
 return true;
}
inline std::string Encode(const Snapshot&s){
 std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17)<<"HLAERecordingProfiles 3\n"<<std::quoted(s.Selected)<<' '<<std::quoted(s.DefaultOutput)<<'\n';
 for(const auto&p:s.Profiles){
  out<<int(p.Type)<<' '<<std::quoted(p.Name)<<' '<<std::quoted(p.OutputSettings)<<' '<<p.OutputFps<<' '<<p.Method<<' '<<p.Exposure<<' '<<p.Strength<<' '<<std::quoted(p.FfmpegOptions)<<' '<<std::quoted(p.FfmpegOptionsNoAlpha)<<' '<<p.InputCount<<' '<<p.InputIndex<<' '<<p.Outputs.size();
  for(const auto&n:p.Outputs)out<<' '<<std::quoted(n);out<<'\n';
 }
 return out.str();
}
inline bool Decode(const std::string&text,Snapshot&result){
 if(text.size()>2*1024*1024)return false;
 std::istringstream in(text);in.imbue(std::locale::classic());std::string magic;int version=0;Snapshot parsed;
 if(!(in>>magic>>version)||magic!="HLAERecordingProfiles"||version<1||version>3||!(in>>std::quoted(parsed.Selected)))return false;
 if(version>=3&&!(in>>std::quoted(parsed.DefaultOutput)))return false;
 in>>std::ws;
 while(!in.eof()){
  RecordingProfileInfo p;int kind=0;
  if(parsed.Profiles.size()>=256||!(in>>kind>>std::quoted(p.Name)>>std::quoted(p.OutputSettings)>>p.OutputFps>>p.Method>>p.Exposure>>p.Strength>>std::quoted(p.FfmpegOptions)))return false;
  if(version>=2&&!(in>>std::quoted(p.FfmpegOptionsNoAlpha)))return false;
  if(version>=3){size_t count=0;if(!(in>>p.InputCount>>p.InputIndex>>count)||count>256)return false;for(size_t i=0;i<count;++i){std::string n;if(!(in>>std::quoted(n)))return false;p.Outputs.push_back(std::move(n));}}
  p.Type=RecordingProfileInfo::Kind(kind);p.Protected=false;parsed.Profiles.push_back(std::move(p));in>>std::ws;
 }
 if(!Validate(parsed))return false;result=std::move(parsed);return true;
}
}}
