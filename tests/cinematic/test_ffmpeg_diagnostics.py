"""Exercise production FFmpeg shutdown/draining with a late encoder failure."""
import argparse
import os
import subprocess
import tempfile
from pathlib import Path
p = argparse.ArgumentParser()
p.add_argument('--sanitize', action='store_true')
p.add_argument('--build-dir', type=Path, default=Path(tempfile.gettempdir()) / 'hlae-encoder-log')
a = p.parse_args(); a.build_dir.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parents[2]
source = (root / 'shared/AfxOutStreams.cpp').read_text()
header = (root / 'shared/AfxOutStreams.h').read_text()
fields = header[header.index('PROCESS_INFORMATION m_ProcessInfo'):header.index('template<bool bThreadSafe> class COutFFMPEGVideoStream')]
fields = fields[:fields.rindex('};')]
def function(name):
    start = source.index(name); end = source.index('{', start) + 1; depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}'); end += 1
    return source[start:end]
code = r'''
#include <string>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <cassert>
#include <vector>
#include <iostream>
#include <thread>
#include "shared/RecordingLog.h"
using advancedfx::RecordingLog;
using DWORD=unsigned long;using BOOL=int;using HANDLE=void*;using CHAR=char;
const HANDLE INVALID_HANDLE_VALUE=reinterpret_cast<HANDLE>(-1);
constexpr int FALSE=0,TRUE=1,WAIT_OBJECT_0=0,WAIT_TIMEOUT=258;
struct PROCESS_INFORMATION{HANDLE hProcess=nullptr,hThread=nullptr;};
struct OVERLAPPED{HANDLE hEvent=nullptr;};
std::string stderrPipe,logText,warnings;std::vector<HANDLE>closed;
bool exited=false,injectFailure=true;DWORD exitStatus=1;
namespace advancedfx {
void Warning(const char*fmt,...){char text[2048];va_list a;va_start(a,fmt);vsnprintf(text,sizeof(text),fmt,a);va_end(a);warnings+=text;}
void Message(const char*,...){}
}
BOOL PeekNamedPipe(HANDLE h,void*,DWORD,void*,DWORD*available,void*){*available=h==reinterpret_cast<HANDLE>(3)?stderrPipe.size():0;return TRUE;}
BOOL ReadFile(HANDLE,void*buffer,DWORD bytes,DWORD*read,void*){*read=bytes;std::memcpy(buffer,stderrPipe.data(),bytes);stderrPipe.erase(0,bytes);return TRUE;}
BOOL WriteFile(HANDLE,const void*buffer,DWORD bytes,DWORD*written,void*){logText.append(static_cast<const char*>(buffer),bytes);*written=bytes;return TRUE;}
BOOL CloseHandle(HANDLE h){closed.push_back(h);return TRUE;}
DWORD WaitForSingleObject(HANDLE,DWORD){
 if(!exited){exited=true;if(injectFailure)stderrPipe="OpenEncodeSessionEx failed: incompatible client key (21)\nNothing was written\n";}
 return WAIT_OBJECT_0;
}
BOOL GetExitCodeProcess(HANDLE,DWORD*out){*out=exitStatus;return TRUE;}
#define min(a,b) ((a)<(b)?(a):(b))
class COutFFMPEGVideoStreamImpl {public:
''' + fields + '};\n' + '\n'.join(function(name) for name in [
    'void COutFFMPEGVideoStreamImpl::WriteEncoderLog(',
    'void COutFFMPEGVideoStreamImpl::Close()',
    'bool COutFFMPEGVideoStreamImpl::HandleOutAndErr(']) + r'''
int main(){
 auto init=[](COutFFMPEGVideoStreamImpl&s){
  s.m_Okay=TRUE;s.m_ProcessInfo.hProcess=reinterpret_cast<HANDLE>(1);s.m_ProcessInfo.hThread=reinterpret_cast<HANDLE>(2);
  s.m_hChildStd_ERR_Rd=reinterpret_cast<HANDLE>(3);s.m_EncoderLogSource="take/players";
 };
 COutFFMPEGVideoStreamImpl failed;init(failed);failed.Close();
 std::vector<advancedfx::RecordingLog::Entry> entries;uint64_t revision=0;
 auto readLog=[&]{logText.clear();assert(advancedfx::RecordingLog::Get().Read(revision,entries));for(const auto&e:entries){assert(e.Source=="take/players");logText+=e.Text;}};
 readLog();
 assert(logText.find("incompatible client key (21)")!=std::string::npos);
 assert(logText.find("FFmpeg exit code: 1")!=std::string::npos&&warnings.find("Recording > FFmpeg log")!=std::string::npos);
 assert(!failed.m_Okay);
 const auto count=closed.size();failed.Close();assert(closed.size()==count);
 logText.clear();warnings.clear();exited=false;injectFailure=false;exitStatus=0;
 advancedfx::RecordingLog::Get().Clear();
 COutFFMPEGVideoStreamImpl success;init(success);success.Close();readLog();
 assert(logText.find("FFmpeg exit code: 0")!=std::string::npos&&warnings.empty());
 advancedfx::RecordingLog::Get().Clear();
 std::vector<std::thread> workers;
 const std::string large(20000,'x'),longSource(2000,'s');
 for(int i=0;i<4;++i)workers.emplace_back([&]{for(int j=0;j<300;++j)advancedfx::RecordingLog::Get().Append(longSource,large.data(),large.size());});
 for(int i=0;i<100;++i){advancedfx::RecordingLog::Get().Read(revision,entries);if(i%10==0)advancedfx::RecordingLog::Get().Clear();}
 for(auto &t:workers)t.join();advancedfx::RecordingLog::Get().Read(revision,entries);
 size_t bytes=0;for(const auto&e:entries){assert(e.Source.size()<=1024&&e.Text.size()<=16384);bytes+=e.Source.size()+e.Text.size();}
 assert(bytes<=advancedfx::RecordingLog::MaxBytes&&entries.size()<=advancedfx::RecordingLog::MaxEntries);
 advancedfx::RecordingLog::Get().Clear();assert(advancedfx::RecordingLog::Get().Read(revision,entries)&&entries.empty());
 std::cout<<"Production FFmpeg shutdown preserves late stderr, output path and exit code; successful/duplicate close stays clean\n";
}
'''
cpp = a.build_dir / 'ffmpeg_log.cpp'; cpp.write_text(code)
cmd = ['g++', '-std=c++17', '-O1', '-g', '-pthread', '-I'+str(root), str(cpp), '-o', str(a.build_dir / 'ffmpeg_log')]
if a.sanitize: cmd[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
subprocess.run(cmd, check=True)
subprocess.run([str(a.build_dir / 'ffmpeg_log')], check=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
