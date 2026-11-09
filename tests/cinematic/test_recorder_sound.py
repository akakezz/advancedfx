"""Exercise the actual recorder mute against a fake IEngineSoundClient003 ABI."""
from pathlib import Path
import argparse
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--build-dir', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
native = (root / 'AfxHookSource/MirvCommands.cpp').read_text()
code = native[native.index('// TF2 POV recorder sound mute.'):native.index('// End TF2 POV recorder sound mute.')]
command = native[native.index('// TF2 recorder sound command.'):native.index('// End TF2 recorder sound command.')]
preamble = r'''
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <stdexcept>
#include <strings.h>
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
Entity player{1},other{2},weapon{3,&player},viewmodel{4,&weapon};
std::vector<Entity*> entities{&player,&other,&weapon,&viewmodel};
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
test = r'''
std::vector<Tf2ActiveSound> sounds;
void*table[18]={};
struct SoundInterface{void**Vtable=table;}soundInterface;
bool interfaceAvailable=true,badQuery=false;int queries=0,writes=0;
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
int main(){try{
 table[16]=(void*)Volume;table[17]=(void*)Query;
 Check(!MirvFix_GetRecorderSoundMute(),"default off");
 MirvFix_RecorderSoundUpdate();Check(queries==0,"disabled has no queries");
 engine.Hltv=true;Check(!MirvFix_SetRecorderSoundMute(true),"STV rejected");engine.Hltv=false;
 engine.Playing=false;Check(!MirvFix_SetRecorderSoundMute(true),"live play rejected");engine.Playing=true;
 g_SourceSdkVer=0;Check(!MirvFix_SetRecorderSoundMute(true),"other games rejected");g_SourceSdkVer=SourceSdkVer_TF2;
 toolsAvailable=false;Check(!MirvFix_SetRecorderSoundMute(true),"missing tools rejected");toolsAvailable=true;
 interfaceAvailable=false;Check(!MirvFix_SetRecorderSoundMute(true),"missing native sound interface rejected");interfaceAvailable=true;
 sounds={Make(10,1),Make(11,-1),Make(12,3),Make(13,4),Make(14,2),Make(15,0),Make(16,-2)};
 sounds.push_back(Make(17,1));sounds.back().Channel=8;
 sounds.push_back(Make(18,1));sounds.back().Channel=2;
 sounds.push_back(Make(19,1));sounds.back().Channel=7;
 Call({"mirv_fix","recorderSounds","0"});
 Check(MirvFix_GetRecorderSoundMute()&&g_Tf2MutedSounds.size()==6,"command enables recorder and owned emitters");
 for(int i=0;i<7;++i)Check(sounds[i].Volume==(i<4?0:.8f),"other player, world and UI untouched");
 Check(sounds[7].Volume==.8f&&sounds[8].Volume==0&&sounds[9].Volume==0,"microphone voice chat retained; character speech muted");
 int firstWrites=writes;MirvFix_RecorderSoundUpdate();
 Check(writes==firstWrites&&g_Tf2RecorderSoundList.Size==10,"repeated query does not append stale snapshots or rewrite zero volumes");
 Call({"mirv_fix","recorderSounds","nan"});Check(MirvFix_GetRecorderSoundMute(),"invalid command preserves state");
 sounds[0].Volume=.6f;MirvFix_RecorderSoundUpdate();Check(sounds[0].Volume==0,"engine volume update re-muted");
 // Changing weapon ownership restores its live sound and any child emitters.
 weapon.Owner=&other;MirvFix_RecorderSoundUpdate();Check(sounds[2].Volume==.8f&&sounds[3].Volume==.8f,"ownership change restores survivors");weapon.Owner=&player;
 engine.Loading=true;engine.Playing=false;int before=queries;MirvFix_RecorderSoundUpdate();
 Check(MirvFix_GetRecorderSoundMute()&&queries==before,"backward reload suspends queries");engine.Loading=false;engine.Playing=true;
 Call({"mirv_fix","recorderSounds","1"});
 Check(!MirvFix_GetRecorderSoundMute()&&sounds[0].Volume==.6f&&sounds[1].Volume==.8f,"disable restores latest native volume");
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
    source.write_text(preamble + code + test)
    subprocess.run(['g++', '-std=c++17', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(source), '-o', str(out/'recorder_sound')], check=True)
    subprocess.run([str(out/'recorder_sound')], check=True)
