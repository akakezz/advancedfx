#!/usr/bin/env python3
"""Run the actual checkpoint startup/control functions with deterministic engine/OS stubs.

This checks retry and diagnostic behavior; native DLL bindings and bitstream
merging are tested separately by test_native_demo.cpp.
"""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compiler", default=os.environ.get("CXX", "c++"))
parser.add_argument("--build-dir", type=Path)
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
src=(root/'AfxHookSource/tf2/DemoPlayer.cpp').read_text()
def block(marker):
 start=src.index(marker);body=src.index('{',start);depth=1;end=body+1
 while depth:
  depth+=(src[end]=='{')-(src[end]=='}');end+=1
 return src[start:end]
preamble=r'''
#define _WIN64
#include "AfxHookSource/tf2/DemoPlayer.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <string>
using namespace AfxHookSource::Tf2Demo;
using DWORD=std::uint32_t;using LONG=std::int32_t;using PVOID=void*;using HANDLE=void*;
constexpr int NO_ERROR=0, IMAGE_DOS_SIGNATURE=0x5a4d, IMAGE_NT_SIGNATURE=0x4550, IMAGE_FILE_MACHINE_AMD64=0x8664;
constexpr DWORD EngineTimestamp=0x6ac41507,EngineImageSize=0x822000,EngineChecksum=0x4fb7d1;
struct IMAGE_DOS_HEADER{int e_magic,e_lfanew;};
struct IMAGE_NT_HEADERS64{int Signature;struct{int Machine;DWORD TimeDateStamp;}FileHeader;struct{DWORD SizeOfImage,CheckSum;}OptionalHeader;};
std::string logText;
void print(const char*f,va_list a){char b[2048];vsnprintf(b,sizeof(b),f,a);logText+=b;}
void Tier0_Msg(const char*f,...){va_list a;va_start(a,f);print(f,a);va_end(a);}
void Tier0_Warning(const char*f,...){va_list a;va_start(a,f);print(f,a);va_end(a);}
bool g_Attempted=false,g_Ready=false;char g_ProbeFailure[512]{},g_CheckpointCommandError[512]{};
void**g_PlayerTable=nullptr,**g_PlayerSlot=nullptr,**g_EntityListSlot=nullptr;
std::uint64_t g_Identity=0,g_SeekSerial=0;int g_LastSeekTarget=-1;unsigned g_SeekDepth=0,g_StartDepth=0;
using StartFn=bool(*)(void*,const char*,bool);using SeekFn=void(*)(void*,int,bool,bool);using EntitiesFn=bool(*)(void*,void*);
StartFn g_Start=nullptr;SeekFn g_Seek=nullptr;EntitiesFn g_Entities=nullptr;
using DecodeFn=bool(*)(void*,void*,void*,int,bool);using MergeFn=int(*)(void*,void*,void*,void*,int,int*,bool);
DecodeFn g_Decode=nullptr;MergeFn g_Merge=nullptr;
const void*g_DecodeClient=nullptr;
int g_SourceSdkVer=1;constexpr int SourceSdkVer_TF2=1;
alignas(16) unsigned char image[256]{}, player[0x6a0]{}, playingCode[14]{}, copyCode[64]{};
void*g_H_EngineDll=image;void*playerSlot=player;void*entityList=player;
void*playerVf[16]{},*handlerVf[24]{},*engineVf[77]{};void**engineObject=engineVf;
struct Engine{void*GetVEngineClient_css(){return &engineObject;}} engine;
Engine*g_VEngineClient=&engine;
bool haveRtti=true,validCode=true,haveDecode=true,haveMerge=true,haveCopy=true;
LONG hookError=0;int commits=0,attaches=0;
HANDLE GetCurrentThread(){return nullptr;}LONG DetourTransactionBegin(){return hookError;}
LONG DetourUpdateThread(HANDLE){return 0;}LONG DetourAttach(PVOID*,PVOID){++attaches;return 0;}
LONG DetourTransactionAbort(){return 0;}LONG DetourTransactionCommit(){++commits;return 0;}
bool MyStart(void*,const char*,bool);void MySeek(void*,int,bool,bool);bool MyEntities(void*,void*);bool MyDecode(void*,void*,void*,int,bool){return true;}
bool Readable(const void*p,size_t){return p!=nullptr;}bool Match(const void*,const char*){return validCode;}
template<class T>T Read(const void*p,size_t offset){T v;memcpy(&v,(const unsigned char*)p+offset,sizeof(v));return v;}
template<class T>void put(void*p,size_t offset,T v){memcpy((unsigned char*)p+offset,&v,sizeof(v));}
namespace Afx{namespace BinUtils{
struct MemRange{size_t Start=0,End=0;MemRange()=default;MemRange(size_t a,size_t b):Start(a),End(b){}bool IsEmpty()const{return Start==End;}};
size_t FindClassVtable(void*,const char*name,int,int){return haveRtti?reinterpret_cast<size_t>(strstr(name,"CDemoPlayer")?playerVf:handlerVf):0;}
MemRange FindPatternString(MemRange range,const char*){return range;}
}}
void*FindDiagnosticFunction(const char*message,const char*){
 if(strstr(message,"MergeDeltas"))return haveMerge?(void*)MyDecode:nullptr;
 if(strstr(message,"CopyExisting"))return haveCopy?copyCode:nullptr;
 return haveDecode?(void*)MyDecode:nullptr;
}
constexpr int GENERIC_WRITE=1,FILE_SHARE_READ=1,CREATE_NEW=1,FILE_ATTRIBUTE_NORMAL=1;
HANDLE const INVALID_HANDLE_VALUE=(HANDLE)(intptr_t)-1;bool fileAvailable=true;
HANDLE CreateFileW(const wchar_t*,int,int,void*,int,int,void*){return fileAvailable?(HANDLE)1:INVALID_HANDLE_VALUE;}
std::string fileOutput;
DWORD GetLastError(){return 80;}bool CloseHandle(HANDLE){return true;}bool WriteFile(HANDLE,const char*data,DWORD bytes,DWORD*written,void*){fileOutput.append(data,bytes);*written=bytes;return true;}
std::string ExportCheckpoint(const Status&){return "<test/>";}
std::uint64_t GetTickCount64(){static std::uint64_t clock=1000;return ++clock;}
DWORD GetCurrentProcessId(){return 77;}
int restartBehavior=0;bool nativeStartAccepted=true,nativePacketAccepted=true;
bool NativeStart(void*,const char*,bool);
void NativeSeek(void*,int,bool,bool);
bool NativeEntities(void*,void*){return nativePacketAccepted;}
'''
functions = block('bool ProbeFailed(') + "\n" + block('bool CheckpointFailed(') + "\n"
functions += block('struct CheckpointCapture') + " g_Checkpoint;\n"
functions += block('enum Event') + ";\n" + block('struct Record') + ";\n" + block('struct Trace') + " g_Trace;\n"
for name in ["Record Capture(Event type)", "void Append(const Record &record)",
             "bool Write(const char *text, size_t bytes)", "void Flush(size_t limit)",
             "void ObserveCheckpointState(const char *where)", "void CaptureFailed(const char *reason)",
             "bool MyStart(void *player", "void MySeek(void *player"]:
    functions += block(name) + "\n"
functions += "void InspectFrames(const void*,Record&) {}\n" + block("bool MyEntities(void *handler") + "\n"
functions += block('bool InitializeCapture()') + "\n" + block('bool Initialize()') + "\n"
functions += "namespace AfxHookSource { namespace Tf2Demo {\n"
for name in ['bool GetStatus(Status &status)','bool BeginCheckpointCapture()','bool SaveCheckpointCapture(const wchar_t *file)','void EndCheckpointCapture()','void PrintCheckpointStatus()', 'void OnNetUpdateEnd()']:
 functions+=block(name)+'\n'
functions+='}}\n'
tests=r'''
bool NativeStart(void*,const char*,bool){
 if(restartBehavior==1)EndCheckpointCapture();
 if(restartBehavior==2)g_Checkpoint.Armed=false;
 return nativeStartAccepted;
}
void NativeSeek(void*p,int,bool,bool){
 MyStart(p,"demo",false);
 unsigned char packet[0x40]{};
 MyEntities((unsigned char*)p+0x10,packet);
 OnNetUpdateEnd();
}
int checks=0;
void check(bool value,const char*reason){++checks;if(!value)throw std::runtime_error(reason);}
int tick(const void*){return 100;}
void reset(){
 EndCheckpointCapture();logText.clear();g_ProbeFailure[0]=0;g_Attempted=g_Ready=false;g_Decode=nullptr;g_Merge=nullptr;
 g_Trace.CheckpointRequested=g_Trace.CheckpointUnexpectedOff=false;g_Trace.CheckpointSession=g_Trace.CheckpointRestarts=g_Trace.CheckpointSeeks=0;
 g_Trace.File=INVALID_HANDLE_VALUE;g_Trace.Head=g_Trace.Count=0;g_Trace.Failed=false;
 fileOutput.clear();restartBehavior=0;nativeStartAccepted=nativePacketAccepted=true;g_SeekDepth=g_StartDepth=0;
 haveRtti=validCode=haveDecode=haveMerge=haveCopy=true;hookError=0;commits=attaches=0;playerSlot=player;entityList=player;
 memset(image,0,sizeof(image));memset(player,0,sizeof(player));
 auto dos=(IMAGE_DOS_HEADER*)image;dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=32;
 auto pe=(IMAGE_NT_HEADERS64*)(image+32);pe->Signature=IMAGE_NT_SIGNATURE;pe->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
 pe->FileHeader.TimeDateStamp=EngineTimestamp;pe->OptionalHeader.SizeOfImage=EngineImageSize;pe->OptionalHeader.CheckSum=EngineChecksum;
 put(player,0,(void**)playerVf);playerVf[3]=(void*)tick;playerVf[5]=(void*)NativeStart;playerVf[14]=(void*)NativeSeek;handlerVf[23]=(void*)NativeEntities;player[0x5b0]=1;put(player,0x5bc,-1);
 unsigned char code[]={0x48,0x8b,0x0d,0,0,0,0,0x48,0x8b,0x01,0x48,0xff,0x60,0x30};memcpy(playingCode,code,sizeof(code));
 auto rel=(intptr_t)&playerSlot-(intptr_t)(playingCode+7);check(rel==int32_t(rel),"test player slot is in RIP-relative reach");put(playingCode,3,int32_t(rel));engineVf[76]=playingCode;
 rel=(intptr_t)&entityList-(intptr_t)(copyCode+7);check(rel==int32_t(rel),"test entity list is in RIP-relative reach");put(copyCode,3,int32_t(rel));
}
int main(){try{
 reset();playerSlot=nullptr;
 check(!BeginCheckpointCapture()&&!g_Checkpoint.Armed&&g_Attempted,"unready player cannot arm capture");
 check(logText.find("demo-player instance")!=std::string::npos,"startup names unavailable player");
 playerSlot=player;check(BeginCheckpointCapture()&&g_Checkpoint.Armed,"begin retries initialization after player becomes ready");
 check(commits==2,"observer and decoder hooks each install once");
 check(!BeginCheckpointCapture()&&g_Checkpoint.Armed&&commits==2,"duplicate begin preserves active capture");
 g_Checkpoint.Seeded=true;g_Checkpoint.Messages=7;g_Checkpoint.Reset();check(g_Checkpoint.Armed&&!g_Checkpoint.Seeded&&!g_Checkpoint.Messages,"demo restart reset preserves armed state");
 check(!SaveCheckpointCapture(L"capture.xml")&&logText.find("No initial full")!=std::string::npos,"capture requests a full packet when unseeded");
 EndCheckpointCapture();logText.clear();check(!SaveCheckpointCapture(L"capture.xml")&&logText.find("Capture is off")!=std::string::npos,"off state does not suggest filenames or seeking");
 reset();auto pe=(IMAGE_NT_HEADERS64*)(image+32);pe->FileHeader.TimeDateStamp=0x12345678;
 check(!BeginCheckpointCapture()&&!g_Checkpoint.Armed&&!attaches,"changed engine build never enables hooks");
 check(logText.find("12345678")!=std::string::npos&&logText.find("6AC41507")!=std::string::npos,"build mismatch reports actual and expected identity");
 logText.clear();PrintCheckpointStatus();check(logText.find("Unsupported engine.dll build")!=std::string::npos,"status retains startup failure");
 reset();haveDecode=false;check(!BeginCheckpointCapture()&&!g_Checkpoint.Armed,"missing decoder does not arm");
 check(logText.find("RecvTable_Decode")!=std::string::npos,"decoder binding failure is identifiable");
 haveDecode=true;check(BeginCheckpointCapture()&&g_Checkpoint.Armed&&commits==2,"decoder binding can retry with observer already ready");
 reset();player[0x5c4]=1;check(!BeginCheckpointCapture()&&logText.find("loading")!=std::string::npos,"loading startup explains readiness");
 player[0x5c4]=0;put(player,0x5bc,90000);check(!BeginCheckpointCapture()&&logText.find("seek is still running")!=std::string::npos,"seek startup explains readiness");
 put(player,0x5bc,-1);check(BeginCheckpointCapture(),"begin succeeds after loading and seeking finish");
 reset();hookError=5;check(!BeginCheckpointCapture()&&logText.find("Detours error 5")!=std::string::npos,"hook installation failure includes native error code");
 hookError=0;check(BeginCheckpointCapture(),"hook installation failure is retryable");
 g_Checkpoint.Seeded=true;check(!SaveCheckpointCapture(L"capture.xml")&&logText.find("network-update boundary")!=std::string::npos,"missing capture boundary is explicit");
 g_Checkpoint.Boundary=true;g_Checkpoint.Client=player;fileAvailable=false;
 check(!SaveCheckpointCapture(L"capture.xml")&&logText.find("Windows error 80")!=std::string::npos,"file creation failure includes OS code");
 fileAvailable=true;check(SaveCheckpointCapture(L"capture.xml")&&!g_CheckpointCommandError[0],"successful export clears last command failure");
 EndCheckpointCapture();check(!g_Checkpoint.Armed&&!g_CheckpointCommandError[0],"end clears command diagnostic");
 reset();check(BeginCheckpointCapture(),"Arm before a real native-hook seek/restart sequence");
 MySeek(player,90000,false,true);
 check(g_Checkpoint.Armed&&g_Trace.CheckpointRequested&&g_Checkpoint.Seeded&&g_Checkpoint.Messages==1,
       "Native seek, nested restart and full packet preserve capture intent and armed state");
 check(g_Trace.CheckpointSeeks==1&&g_Trace.CheckpointRestarts==1&&g_SeekDepth==0&&g_StartDepth==0,
       "Native lifecycle counts and nesting return to baseline");
 check(!g_Trace.CheckpointUnexpectedOff&&g_Checkpoint.Boundary,"Completed network update has a capture boundary");
 MySeek(player,89900,false,true);
 check(g_Checkpoint.Armed&&g_Checkpoint.Messages==1&&g_Trace.CheckpointRestarts==2,
       "Repeated backward restart discards old packets while remaining armed");
 restartBehavior=1;MySeek(player,89800,false,true);
 check(!g_Checkpoint.Armed&&!g_Trace.CheckpointRequested&&!g_Trace.CheckpointUnexpectedOff,
       "An explicit end inside native restart stays off and is not silently rearmed");
 check(logText.find("ended by checkpoint end (native seek depth 1, playback start depth 1)")!=std::string::npos,
       "End executed during reload is identifiable in the console");
 reset();check(BeginCheckpointCapture(),"Arm before unexpected state loss");
 g_Trace.File=(HANDLE)1;restartBehavior=2;MySeek(player,80000,false,true);
 check(!g_Checkpoint.Armed&&g_Trace.CheckpointRequested&&g_Trace.CheckpointUnexpectedOff,
       "Unexpected off state preserves separate capture intent for diagnosis");
 check(logText.find("unexpectedly became off at playback restart return")!=std::string::npos,
       "Unexpected loss identifies the first observed native callback boundary");
 int lostEvents=0;for(size_t i=0;i<g_Trace.Count;++i)if(g_Trace.Records[i].Type==CheckpointLost){
   ++lostEvents;check(!g_Trace.Records[i].CaptureArmed&&g_Trace.Records[i].CaptureRequested,"Trace records actual and requested state separately");
 }
 check(lostEvents==1,"Unexpected off is recorded once without per-frame spam");
 Flush(g_Trace.Count);std::istringstream csv(fileOutput);std::string row;int rows=0;
 while(std::getline(csv,row)){check(std::count(row.begin(),row.end(),',')==26,"Trace rows contain all 27 version-two columns");++rows;}
 check(rows>0&&fileOutput.find("checkpoint_lost")!=std::string::npos,"Trace flush exports capture loss lifecycle event");
 logText.clear();PrintCheckpointStatus();check(logText.find("session 1; requested 1, observer hooks 1, decoder hook 1")!=std::string::npos,
       "Status exposes session intent and installed hooks after lost state");
 EndCheckpointCapture();g_Trace.File=INVALID_HANDLE_VALUE;
 reset();check(BeginCheckpointCapture(),"Arm before native start rejection");nativeStartAccepted=false;
 check(!MyStart(player,"demo",false)&&g_Checkpoint.Armed,"Rejected native playback start preserves capture and original result");
 nativeStartAccepted=true;nativePacketAccepted=false;MySeek(player,90000,false,true);
 check(g_Checkpoint.Armed&&g_Checkpoint.Error&&!g_Checkpoint.Boundary,"Rejected entity packet invalidates data without silently disarming");
 std::cout<<checks<<" checkpoint startup checks passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<"\n"<<logText;return 1;}}
'''
with tempfile.TemporaryDirectory(prefix="tf2-checkpoint-startup-") as temporary:
    out = args.build_dir or Path(temporary)
    out.mkdir(parents=True, exist_ok=True)
    source = out / "startup.cpp"
    executable = out / "startup"
    source.write_text(preamble + functions + tests)
    command = [args.compiler, "-std=c++17", "-g", "-I" + str(root), str(source), "-o", str(executable)]
    if args.sanitize:
        command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, check=True)
    subprocess.run([str(executable.resolve())], check=True)
