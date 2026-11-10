"""Exercise the actual recorder mute against a fake IEngineSoundClient003 ABI."""
from pathlib import Path
import argparse
import json
import struct
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--build-dir', type=Path)
parser.add_argument('--engine-dir', type=Path, help='Also validate the resolver against installed TF2 Windows engine binaries')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
native = (root / 'AfxHookSource/MirvCommands.cpp').read_text()
code = native[native.index('// TF2 POV recorder sound mute.'):native.index('// End TF2 POV recorder sound mute.')]
platform_a=code.index('// Platform-specific resolver')
platform_b=code.index('// End platform-specific recorder sound resolver.')
resolver=code[code.index('void *Tf2FindStartSound('):code.index('bool Tf2InstallRecorderSoundHook(')]
code=code[:platform_a]+'bool Tf2InstallRecorderSoundHook(void *);\n'+code[platform_b:]
command = native[native.index('// TF2 recorder sound command.'):native.index('// End TF2 recorder sound command.')]
preamble = r'''
#include <algorithm>
#include <atomic>
#include <memory>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <stdexcept>
#include <strings.h>
#define __cdecl
using DWORD=unsigned long;DWORD soundThread=1;DWORD GetCurrentThreadId(){return soundThread;}
#define __thiscall
#define _stricmp strcasecmp
#define Tier0_Msg printf
#define Tier0_Warning printf
struct Alloc { int Frees=0; void Free(void*p){++Frees;free(p);} } alloc;
namespace SOURCESDK { namespace L4D2 { Alloc *g_pMemAlloc=&alloc; } }
enum {SourceSdkVer_TF2=1,SourceSdkVer_TF2Classified=2};
int g_SourceSdkVer=SourceSdkVer_TF2;
struct Engine {
 bool Playing=true,Hltv=false,Loading=false;int Local=1;
 bool IsPlayingDemo(){return Playing;}bool IsHLTV(){return Hltv;}
 bool IsDrawingLoadingImage(){return Loading;}int GetLocalPlayer(){return Local;}
} engine, *g_VEngineClient=&engine;
struct Entity {int Index;Entity*Owner=nullptr;};
Entity player{1},other{2},weapon{3,&player},viewmodel{4,&weapon},nonNetwork{0,&player};
std::vector<Entity*> entities{&player,&other,&weapon,&viewmodel,&nonNetwork};
struct Tools {
 Entity*FirstEntity(){return entities.empty()?nullptr:entities.front();}
 Entity*NextEntity(Entity*e){auto i=std::find(entities.begin(),entities.end(),e);return i!=entities.end()&&++i!=entities.end()?*i:nullptr;}
 int GetEntIndex(Entity*e){return e->Index;}Entity*GetOwnerEntity(Entity*e){return e->Owner;}
} tools;
bool toolsAvailable=true;
struct CClientToolsTf2 {
 static CClientToolsTf2*Instance(){static CClientToolsTf2 t;return toolsAvailable?&t:nullptr;}
 Tools*GetClientTools(){return &tools;}
};
void*SoundFactory(const char*,int*);
void*(*g_AppSystemFactory)(const char*,int*)=SoundFactory;
void MirvFix_RecorderSoundUpdate();
'''
resolver_test = r"""
std::map<uintptr_t,std::vector<unsigned char>> soundCode;
bool Tf2SoundCodeRead(const void*address,void*buffer,size_t length){
 auto a=reinterpret_cast<uintptr_t>(address);
 for(auto&segment:soundCode)if(a>=segment.first&&a-segment.first<=segment.second.size()&&length<=segment.second.size()-(a-segment.first)){
  memcpy(buffer,segment.second.data()+a-segment.first,length);return true;
 }
 return false;
}
"""+resolver+r"""
void ResolverCheck(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
void RunEngineCase(const char*file,uintptr_t base,uintptr_t ambient,bool is64){
 std::ifstream input(file,std::ios::binary);std::vector<unsigned char> data((std::istreambuf_iterator<char>(input)),{});
 ResolverCheck(!data.empty(),"engine test image exists");soundCode.clear();soundCode.emplace(base,std::move(data));
 auto result=Tf2FindStartSound(reinterpret_cast<void*>(ambient),is64);
 ResolverCheck(result!=nullptr,"versioned ambient emitter resolves one verified native sound start in the installed engine");
}
void ResolverTests(){
 for(bool is64:{false,true}){
  soundCode.clear();auto&code=soundCode[0x1000];code.assign(256,0x90);
  std::vector<unsigned char> prefix=is64?std::vector<unsigned char>{0xc6,0x44,0x24,0x20,1,0xc7,0x44,0x24,0x28,255,255,255,255,0xc7,0x44,0x24,0x2c,6,0,0,0}:std::vector<unsigned char>{0xc6,0x45,0xa4,1,0xc7,0x45,0xac,255,255,255,255,0xc7,0x45,0xb0,6,0,0,0};
  std::copy(prefix.begin(),prefix.end(),code.begin()+16);
  std::vector<unsigned char> flag=is64?std::vector<unsigned char>{0x89,0x44,0x24,0x5c}:std::vector<unsigned char>{0x89,0x45,0xdc};std::copy(flag.begin(),flag.end(),code.begin()+48);
  code[64]=0xe8;int relative=0x2000-(0x1000+69);memcpy(code.data()+65,&relative,4);
  auto&entry=soundCode[0x2000];entry=is64?std::vector<unsigned char>{0x40,0x57,0x48,0x81,0xec,0x60,2,0,0,0x48,0x83,0x79,0x10,0,0x48,0x8b,0xf9,0x75,0xb,0x33,0xc0}:std::vector<unsigned char>{0x55,0x8b,0xec,0x81,0xec,0xc4,1,0,0,0x56,0x8b,0x75,8,0x83,0x7e,0x10,0,0x75,7,0x33,0xc0};
  ResolverCheck(Tf2FindStartSound((void*)0x1000,is64)==(void*)0x2000,"native start resolves through validated relative call on both ABIs");
  auto saved=code[48];code[48]=0x90;ResolverCheck(!Tf2FindStartSound((void*)0x1000,is64),"unknown flags layout is rejected");code[48]=saved;
  entry[0]=0x90;ResolverCheck(!Tf2FindStartSound((void*)0x1000,is64),"unknown start entry is rejected");entry[0]=is64?0x40:0x55;
  soundCode[0x3000]=entry;code[80]=0xe8;relative=0x3000-(0x1000+85);memcpy(code.data()+81,&relative,4);
  ResolverCheck(!Tf2FindStartSound((void*)0x1000,is64),"ambiguous sound start discovery is rejected");
 }
 soundCode.clear();
}
"""

test = r'''
std::vector<Tf2ActiveSound> sounds;
void*table[18]={};
struct SoundInterface{void**Vtable=table;}soundInterface;
bool interfaceAvailable=true,badQuery=false,hookAvailable=true;int queries=0,writes=0,starts=0;
int Start(void*){++starts;return 77;}
namespace { bool Tf2InstallRecorderSoundHook(void*){if(!hookAvailable)return false;g_Tf2StartSound=Start;return true;} }
// Native emitter layout: flags precede pitch. Populate both, including normal pitch 100.
int Emit(int source,int channel=2,int flags=0,bool ambient=false,int pitch=100){
 char p[96]{};p[0]=ambient;memcpy(p+8,&source,4);memcpy(p+12,&channel,4);
 void*sfx=(void*)0x100;float volume=.8f;int level=75;
 memcpy(p+16,&sfx,sizeof(sfx));p[sizeof(void*)==8?48:44]=1;
 memcpy(p+(sizeof(void*)==8?52:48),&volume,4);memcpy(p+(sizeof(void*)==8?56:52),&level,4);
 memcpy(p+(sizeof(void*)==8?60:56),&flags,4);memcpy(p+(sizeof(void*)==8?64:60),&pitch,4);
 return Tf2RecorderStartSound(p);
}
void Query(void*,Tf2ActiveSoundList&list){
 ++queries;
 if(badQuery){list.Size=-1;return;}
 int n=list.Size+(int)sounds.size();
 if(n>list.Capacity){list.Memory=(Tf2ActiveSound*)realloc(list.Memory,n*sizeof(Tf2ActiveSound));list.Capacity=n;list.Elements=list.Memory;}
 for(auto s:sounds)list.Memory[list.Size++]=s;
}
void Volume(void*,int guid,float v){++writes;for(auto&s:sounds)if(s.Guid==guid)s.Volume=v;}
void*SoundFactory(const char*name,int*){return interfaceAvailable&&!strcmp(name,"IEngineSoundClient003")?&soundInterface:nullptr;}
Tf2ActiveSound Make(int guid,int source,float volume=.8f){Tf2ActiveSound s{};s.Guid=guid;s.Source=source;s.Volume=volume;s.Filename=(void*)size_t(guid);return s;}
void Check(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
void Call(std::initializer_list<const char*> words){struct Args{std::vector<const char*>V;int ArgC(){return V.size();}const char*ArgV(int i){return V.at(i);}}data;data.V=words;auto*args=&data;int argc=args->ArgC();const char*cmd1=args->ArgV(1);
'''+command+r'''
}
int main(){try{ResolverTests();ENGINE_CASES
 table[16]=(void*)Volume;table[17]=(void*)Query;
 Check(!MirvFix_GetRecorderSoundMute(),"default off");
 MirvFix_RecorderSoundUpdate();Check(queries==0,"disabled has no queries");
 engine.Hltv=true;Check(!MirvFix_SetRecorderSoundMute(true),"STV rejected");engine.Hltv=false;
 engine.Playing=false;Check(!MirvFix_SetRecorderSoundMute(true),"live play rejected");engine.Playing=true;
 g_SourceSdkVer=0;Check(!MirvFix_SetRecorderSoundMute(true),"other games rejected");g_SourceSdkVer=SourceSdkVer_TF2;
 toolsAvailable=false;Check(!MirvFix_SetRecorderSoundMute(true),"missing tools rejected");toolsAvailable=true;
 interfaceAvailable=false;Check(!MirvFix_SetRecorderSoundMute(true),"missing native sound interface rejected");interfaceAvailable=true;
 hookAvailable=false;Check(!MirvFix_SetRecorderSoundMute(true),"no leaky fallback when pre-playback hook is unavailable");hookAvailable=true;
 sounds={Make(10,1),Make(11,-1),Make(12,3),Make(13,4),Make(14,2),Make(15,0),Make(16,-2)};
 sounds.push_back(Make(17,1));sounds.back().Channel=8;
 sounds.push_back(Make(18,1));sounds.back().Channel=2;
 sounds.push_back(Make(19,1));sounds.back().Channel=7;
 sounds.push_back(Make(20,-1));sounds.back().Channel=6;
 Call({"mirv_fix","recorderSounds","0"});
 Check(MirvFix_GetRecorderSoundMute()&&g_Tf2MutedSounds.size()==6,"command enables recorder and owned emitters");
 for(int i=0;i<7;++i)Check(sounds[i].Volume==(i<4?0:.8f),"other player, world and UI untouched");
 Check(sounds[7].Volume==.8f&&sounds[8].Volume==0&&sounds[9].Volume==0&&sounds[10].Volume==.8f,"microphone and anonymous ambient audio retained; character speech muted");
 Check(Emit(1)==0&&Emit(-1)==0&&Emit(3)==0&&Emit(4)==0&&starts==0,"recorder speech, local sounds, weapons and viewmodels never reach native playback");
 for(int pitch=1;pitch<=255;++pitch)Check(Emit(1,2,0,false,pitch)==0,"recorder starts are blocked regardless of pitch bits");
 for(int flags:{1,2,8,16,64,128,256,512,1024})Check(Emit(1,2,flags)==0,"non-stop sound flags cannot bypass recorder mute");
 for(int pitch:{1,64,100,128,255})Check(Emit(1,2,4,false,pitch)==77&&Emit(1,2,32,false,pitch)==77,"stop requests reach native playback regardless of pitch");

 Check(Emit(2)==77&&Emit(0)==77&&Emit(-2)==77&&Emit(-1,6,0,true)==77&&Emit(1,8)==77&&Emit(1,2,4)==77&&Emit(1,2,32)==77,"other emitters, anonymous ambient, microphone voice and sound stops pass through");
 soundThread=2;Check(Emit(3)==0&&Emit(0)==77,"worker uses immutable weapon ownership snapshot");soundThread=1;
 Entity newWeapon{5,&player};entities.push_back(&newWeapon);Check(Emit(5)==0,"new weapon is filtered before the next frame publishes ownership");entities.pop_back();
 engine.Hltv=true;Check(Emit(1)==77,"STV sound starts bypass cached recorder state");engine.Hltv=false;
 int firstWrites=writes;MirvFix_RecorderSoundUpdate();
 Check(writes==firstWrites&&g_Tf2RecorderSoundList.Size==11,"repeated query does not append stale snapshots or rewrite zero volumes");
 Call({"mirv_fix","recorderSounds","nan"});Check(MirvFix_GetRecorderSoundMute(),"invalid command preserves state");
 sounds[0].Volume=.6f;MirvFix_RecorderSoundUpdate();Check(sounds[0].Volume==0,"engine volume update re-muted");
 // Changing weapon ownership restores its live sound and any child emitters.
 weapon.Owner=&other;MirvFix_RecorderSoundUpdate();Check(sounds[2].Volume==.8f&&sounds[3].Volume==.8f,"ownership change restores survivors");weapon.Owner=&player;
 engine.Loading=true;engine.Playing=false;int before=queries;MirvFix_RecorderSoundUpdate();
 Check(MirvFix_GetRecorderSoundMute()&&queries==before,"backward reload suspends queries");engine.Loading=false;engine.Playing=true;
 Call({"mirv_fix","recorderSounds","1"});
 Check(!MirvFix_GetRecorderSoundMute()&&sounds[0].Volume==.6f&&sounds[1].Volume==.8f,"disable restores latest native volume");
 Check(Emit(1)==77,"disabled hook forwards sounds immediately");
 Check(!g_Tf2RecorderSoundList.Memory&&alloc.Frees>0,"engine allocation released");
 Check(MirvFix_SetRecorderSoundMute(true),"re-enable");
 sounds[0].Filename=(void*)999;sounds[0].Source=2;sounds[0].Volume=.3f;
 sounds.erase(sounds.begin()+1);MirvFix_RecorderSoundUpdate();
 Check(sounds[0].Volume==.3f&&!g_Tf2MutedSounds.count(10)&&!g_Tf2MutedSounds.count(11),"finished sounds and reused GUID identity do not receive stale restore volumes");
 // A later, nonzero external volume edit must survive an explicit disable.
 sounds[1].Volume=.4f;MirvFix_SetRecorderSoundMute(false);Check(sounds[1].Volume==.4f,"disable respects an external volume edit");
 g_SourceSdkVer=SourceSdkVer_TF2Classified;Check(MirvFix_SetRecorderSoundMute(true),"TF2C uses the same versioned interface");
 engine.Hltv=true;MirvFix_RecorderSoundUpdate();Check(!MirvFix_GetRecorderSoundMute(),"STV transition disables mute");engine.Hltv=false;
 Check(MirvFix_SetRecorderSoundMute(true),"enable before playback end");engine.Playing=false;MirvFix_RecorderSoundUpdate();
 Check(!MirvFix_GetRecorderSoundMute()&&g_Tf2MutedSounds.empty(),"playback end restores and clears state");
 engine.Playing=true;badQuery=true;Check(!MirvFix_SetRecorderSoundMute(true)&&!MirvFix_GetRecorderSoundMute(),"failed query cannot report an enabled mute");
 puts("Recorder sound mute tests passed");return 0;
}catch(const std::exception&e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
'''
with tempfile.TemporaryDirectory(prefix='hlae-recorder-sound-') as temp:
    out = args.build_dir or Path(temp)
    out.mkdir(parents=True, exist_ok=True)
    source = out / 'recorder_sound.cpp'
    engine_cases = []
    if args.engine_dir:
        for arch, relative_file in [(64, 'bin/x64/engine.dll'), (32, 'bin/engine.dll')]:
            raw = (args.engine_dir / relative_file).read_bytes()
            pe = struct.unpack_from('<I', raw, 60)[0]
            count = struct.unpack_from('<H', raw, pe + 6)[0]
            opt = struct.unpack_from('<H', raw, pe + 20)[0]
            base = struct.unpack_from('<Q' if arch == 64 else '<I', raw, pe + (48 if arch == 64 else 52))[0]
            image = bytearray(struct.unpack_from('<I', raw, pe + 80)[0])
            for i in range(count):
                header = pe + 24 + opt + i * 40
                virtual_size, rva, size, offset = struct.unpack_from('<IIII', raw, header + 8)
                image[rva:rva + size] = raw[offset:offset + size]
            name = base + image.index(b'IEngineSoundClient003\0')
            factories = []
            if arch == 64:
                for i in range(len(image) - 14):
                    if image[i:i+3] == bytes([0x4c, 0x8d, 0x05]) and image[i+7:i+10] == bytes([0x48, 0x8d, 0x15]):
                        if base+i+7+struct.unpack_from('<i',image,i+3)[0] == name:
                            factories.append(base+i+14+struct.unpack_from('<i',image,i+10)[0])
            else:
                pattern = bytes([0x68]) + struct.pack('<I', name) + bytes([0x68])
                at = image.find(pattern)
                if at >= 0:
                    factories.append(struct.unpack_from('<I',image,at+6)[0])
            assert len(factories) == 1, 'Unambiguous versioned sound-interface registration'
            at = factories[0]-base
            if arch == 64:
                assert image[at:at+3] == bytes([0x48,0x8d,0x05]) and image[at+7] == 0xc3
                obj = factories[0]+7+struct.unpack_from('<i',image,at+3)[0]
            else:
                assert image[at] == 0xb8 and image[at+5] == 0xc3
                obj = struct.unpack_from('<I',image,at+1)[0]
            fmt = '<Q' if arch == 64 else '<I'
            vtable = struct.unpack_from(fmt,image,obj-base)[0]
            ambient = struct.unpack_from(fmt,image,vtable-base+11*(arch//8))[0]
            image_file = out/f'engine{arch}.image'
            image_file.write_bytes(image)
            engine_cases.append(f'RunEngineCase({json.dumps(str(image_file))}, {base}ull, {ambient}ull, {str(arch==64).lower()});')
    source.write_text(preamble + code + resolver_test + test.replace('ENGINE_CASES', ''.join(engine_cases)))
    subprocess.run(['g++', '-std=c++17', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(source), '-o', str(out/'recorder_sound')], check=True)
    subprocess.run([str(out/'recorder_sound')], check=True)
