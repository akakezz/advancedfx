"""Check production conversion, packing and FFmpeg write path using a mock pipe."""
from pathlib import Path
import argparse, subprocess, tempfile, os
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--build-dir',type=Path,default=Path(tempfile.gettempdir())/'hlae-recording-fastpath');a=p.parse_args();r=Path(__file__).resolve().parents[2];a.build_dir.mkdir(parents=True,exist_ok=True)
flags=['-std=c++17','-O2','-I'+str(r)]
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-g']
def run(src,name):
 exe=a.build_dir/name;subprocess.run(['g++',*flags,str(src),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
run(r/'tests/cinematic/test_recording_fastpath.cpp','conversion')
s=(r/'shared/AfxOutStreams.cpp').read_text();body=s[s.index('bool COutFFMPEGVideoStreamImpl::WritePackedFrame('):s.index('COutFFMPEGVideoStreamImpl::~COutFFMPEGVideoStreamImpl()')]
f=a.build_dir/'pipe.cpp';f.write_text(r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#include "shared/PackedVideoFrame.h"
#include "shared/RecordingTiming.h"
using namespace advancedfx;
using DWORD=uint32_t;constexpr int TRUE=1,ERROR_IO_PENDING=997,WAIT_OBJECT_0=0,WAIT_TIMEOUT=258;
struct OVERLAPPED{void*hEvent=nullptr;};
std::vector<unsigned char> wire;DWORD lastWritten=0;int writes=0,mode=0;
int WriteFile(void*,const unsigned char*p,DWORD n,void*,OVERLAPPED*){++writes;lastWritten=mode==2&&n>3?3:n;if(mode==3){lastWritten=0;return TRUE;}wire.insert(wire.end(),p,p+lastWritten);return mode==1?0:TRUE;}
int GetLastError(){return ERROR_IO_PENDING;}int WaitForSingleObject(void*,int){return WAIT_OBJECT_0;}
int GetOverlappedResult(void*,OVERLAPPED*,DWORD*out,int){*out=lastWritten;return TRUE;}
#define FALSE 0
struct COutFFMPEGVideoStreamImpl{
 int m_Okay=TRUE;CImageFormat m_ImageFormat{ImageFormat::BGR,5,3,19};std::vector<unsigned char>m_PackedFrame;
 RecordingTiming::Counter m_EncoderPackTime;void*m_hChildStd_IN_Wr=nullptr;OVERLAPPED m_OverlappedStdin;
 bool HandleOutAndErr(){return true;}void Close(){m_Okay=FALSE;}bool WritePackedFrame(const unsigned char*);
};
'''+body+r'''
int main(){std::vector<unsigned char>src(57),expected;for(size_t i=0;i<src.size();++i)src[i]=i;for(int y=0;y<3;++y)expected.insert(expected.end(),src.begin()+19*y,src.begin()+19*y+15);
 for(mode=0;mode<3;++mode){COutFFMPEGVideoStreamImpl s;wire.clear();writes=0;assert(s.WritePackedFrame(src.data()));assert(wire==expected);assert(writes==(mode==2?15:1));}
 mode=3;COutFFMPEGVideoStreamImpl s;assert(!s.WritePackedFrame(src.data())&&!s.m_Okay);
 std::cout<<"Production FFmpeg write path: immediate, pending, partial, zero-byte failure passed\n";
}
''');run(f,'pipe')
