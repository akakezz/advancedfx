// Exact-engine experimental surface history. Only ordinary immediate demo views.
// No historical native address is retained in a Frame.
#ifdef _WIN64
#include <intrin.h>
namespace {
using namespace advancedfx::SceneSurfaces;
using SurfaceAddress=std::uint64_t;
using ShadowHandle=std::uint16_t;
constexpr ShadowHandle ShadowInvalid=0xffff;
static bool SurfaceRead(SurfaceAddress a,void *out,std::size_t n){
    SIZE_T got=0;return a>=0x10000&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(a),out,n,&got)&&got==n;
}
template<class T> static bool SurfaceGet(SurfaceAddress a,T &v){return SurfaceRead(a,&v,sizeof(v));}
static bool SurfaceMatch(SurfaceAddress base,SurfaceAddress r,std::initializer_list<unsigned char> bytes){
    unsigned char data[32]{};return bytes.size()<=sizeof(data)&&SurfaceRead(base+r,data,bytes.size())&&std::equal(bytes.begin(),bytes.end(),data);
}
static SurfaceAddress SurfaceEngineModule(){
    const auto base=reinterpret_cast<SurfaceAddress>(GetModuleHandleA("engine.dll"));
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    if(!base||!SurfaceGet(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>0x1000||
       !SurfaceGet(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||
       nt.FileHeader.TimeDateStamp!=0x6ac41507||nt.OptionalHeader.SizeOfImage!=0x822000)return 0;
    return base;
}
static SurfaceAddress SurfaceEngine(){
    if(g_SourceSdkVer!=SourceSdkVer_TF2||!g_VEngineClient||!g_VEngineClient->IsPlayingDemo()||
       g_VEngineClient->IsSkippingDemo()||g_VEngineClient->IsDrawingLoadingImage()||MirvCampathPreview_IsSeeking())return 0;
    return SurfaceEngineModule();
}
static bool SurfaceMaterialName(SurfaceAddress material,std::string &name){
    SurfaceAddress table=0,fn=0;
    MEMORY_BASIC_INFORMATION mbi{};
    auto module=GetModuleHandleA("materialsystem.dll");
    if(!module||!SurfaceGet(material,table)||!SurfaceGet(table,fn)||!VirtualQuery(reinterpret_cast<void*>(fn),&mbi,sizeof(mbi))||
       mbi.AllocationBase!=module||!(mbi.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
    using Name=const char*(*)(void*);
    auto ptr=reinterpret_cast<Name>(fn)(reinterpret_cast<void*>(material));
    const auto address=reinterpret_cast<SurfaceAddress>(ptr);name.clear();
    // Read bounded chunks rather than one system call per character. If a
    // chunk crosses unreadable memory, fall back to a single byte so a
    // valid terminator just before that boundary remains accepted.
    for(unsigned i=0;i<260;){
        char bytes[32]{};
        auto n=(std::min)(unsigned(sizeof(bytes)),260-i);
        if(!SurfaceRead(address+i,bytes,n)){n=1;if(!SurfaceRead(address+i,bytes,n))return false;}
        for(unsigned j=0;j<n;++j){
            const auto ch=static_cast<unsigned char>(bytes[j]);
            if(!ch)return !name.empty();
            if(ch<32||ch>126)return false;
            name+=bytes[j];
        }
        i+=n;
    }
    return false;
}
struct NativeWorld {SurfaceAddress World=0,Surfaces=0;int Count=0;};
static bool SurfaceWorld(SurfaceAddress base,NativeWorld &w){
    return SurfaceGet(base+0x755738,w.World)&&w.World&&SurfaceGet(w.World+0xe8,w.Surfaces)&&w.Surfaces&&SurfaceGet(w.World+0xd8,w.Count)&&w.Count>0&&w.Count<=1048576;
}
static NativeWorld g_SurfaceWorld;
static std::uint64_t g_SurfaceGeneration=1;
static bool SurfaceSameWorld(const NativeWorld &a,const NativeWorld &b){return a.World==b.World&&a.Surfaces==b.Surfaces&&a.Count==b.Count;}
struct LiveDecal {Decal Value;SurfaceAddress Object=0;};
static bool ReadWorldDecals(SurfaceAddress base,const NativeWorld &world,std::vector<LiveDecal> &out,std::size_t &unsupported,DecalRead &status){
    status=DecalRead::Guard;
    if(!SurfaceMatch(base,0x117c42,{0x48,0x89,0x04,0xd1,0x66,0x89,0x58,0x66})||
       !SurfaceMatch(base,0x118700,{0x48,0x8b,0x80,0xe8,0x00,0x00,0x00,0x48,0x8d,0x49,0x40})||
       !SurfaceMatch(base,0x11677c,{0x48,0x8b,0x4a,0x18,0x48,0x8b,0x01,0xff,0x10})||
       !SurfaceMatch(base,0x11b326,{0x0f,0xb6,0x43,0x57,0x74,0x10}))return false;
    status=DecalRead::Table;
    SurfaceAddress table=0;int count=0,capacity=0;
    if(!SurfaceGet(base+0x6a4e60,table)||!SurfaceGet(base+0x6a4e70,count)||!SurfaceGet(base+0x6a4e68,capacity)||
       count<0||count>8192||capacity<count||(count&&!table))return false;
    std::vector<SurfaceAddress> slots(count);
    if(count&&!SurfaceRead(table,slots.data(),slots.size()*sizeof(SurfaceAddress)))return false;
    std::map<SurfaceAddress,std::string> materials;
    for(int i=0;i<count;++i){
        auto a=slots[i];if(!a)continue;
        std::array<unsigned char,0x6c> raw{};
        status=DecalRead::Record;
        if(!SurfaceGet(a,raw))return false;
        auto get=[&](std::size_t off,auto &v){std::memcpy(&v,raw.data()+off,sizeof(v));};
        LiveDecal live;auto &v=live.Value;live.Object=a;
        SurfaceAddress surface=0,material=0,attachment=0;
        get(0x10,surface);get(0x18,material);get(0x58,attachment);get(0x66,v.Slot);get(0x68,v.Flags);get(0x6a,v.NameIndex);
        if(v.Slot!=i){status=DecalRead::Slot;return false;}
        if(!surface||surface<world.Surfaces||(surface-world.Surfaces)%64||(surface-world.Surfaces)/64>=static_cast<unsigned>(world.Count)||
           attachment||(v.Flags&0x1100)){++unsupported;continue;}
        std::uint32_t surfaceFlags=0;
        if(!SurfaceGet(surface,surfaceFlags)){status=DecalRead::Surface;return false;}
        if(surfaceFlags&0x800){++unsupported;continue;}
        v.Surface=static_cast<std::uint32_t>((surface-world.Surfaces)/64);
        std::memcpy(v.Placement.data(),raw.data()+0x24,12);
        if(v.Flags&0x80)std::memcpy(v.Placement.data()+3,raw.data()+0x30,12);
        get(0x44,v.Placement[6]);get(0x54,v.Color);get(0x57,v.Alpha);
        v.Flags&=0x0081; // avoid transient draw/clip flags in historical identity
        auto found=materials.find(material);
        if(found==materials.end()){
            std::string name;if(!SurfaceMaterialName(material,name)){++unsupported;continue;}
            found=materials.emplace(material,std::move(name)).first;
        }
        v.Material=found->second;
        out.push_back(std::move(live));
    }
    SurfaceAddress after=0;int n=0;NativeWorld now;
    const bool stable=SurfaceGet(base+0x6a4e60,after)&&after==table&&SurfaceGet(base+0x6a4e70,n)&&n==count&&SurfaceWorld(base,now)&&SurfaceSameWorld(now,world);
    status=stable?DecalRead::Ready:DecalRead::Changed;return stable;
}
// Native engine shadow manager, SDK VEngineShadowMgr002; exact vtable and
// project/destroy/bias function bodies checked before installing observers.
using ProjectFn=void(*)(void*,ShadowHandle,const float*,const float*,const float*,const float*,int,const int*,float,float,float,const float*);
using DestroyFn=void(*)(void*,ShadowHandle);
using BiasFn=void(*)(void*,ShadowHandle,unsigned char);
using CreateFn=ShadowHandle(*)(void*,void*,void*,void*,int);
using TexFn=void(*)(void*,ShadowHandle,float,float,float,float);
static void *g_SurfaceShadowManager=nullptr;
static ProjectFn g_SurfaceProject=nullptr;
static DestroyFn g_SurfaceDestroy=nullptr;
static BiasFn g_SurfaceBias=nullptr;
static DWORD g_SurfaceThread=0;
static bool g_SurfaceHooks=false,g_SurfaceOwn=false,g_SurfaceReplayActive=false,g_SurfaceCaptureArmed=false;
static std::map<ShadowHandle,Shadow> g_SurfaceObserved;
static std::map<ShadowHandle,unsigned char> g_SurfaceSavedBias;
static std::vector<ShadowHandle> g_SurfaceOwned;
static std::string g_SurfaceReport;
// Return null from the native decal geometry lookup only for future-only
// surviving records in an active ordinary preview. No live record writes.
// Restrict to verified draw callsites; geometry callers outside drawing
// must retain their original behavior.
using DecalGeometryFn=void*(*)(void*,void*,void*,void*);
static DecalGeometryFn g_SurfaceDecalGeometry=nullptr;
static SurfaceAddress g_SurfaceDecalBase=0;
static DWORD g_SurfaceDecalThread=0;
static bool g_SurfaceDecalHookInstalled=false;
static thread_local bool g_SurfaceDecalGate=false;
struct DecalGate {SurfaceAddress Surface=0,Material=0;std::uint16_t Slot=0;};
static thread_local std::map<SurfaceAddress,DecalGate> g_SurfaceHiddenDecals;
static thread_local std::size_t g_SurfaceDecalDrawSkips=0;
static void *SurfaceDecalGeometryHook(void *scratch,void *decal,void *surface,void *material){
    if(g_SurfaceDecalGate&&GetCurrentThreadId()==g_SurfaceDecalThread){
        const auto caller=reinterpret_cast<SurfaceAddress>(_ReturnAddress())-g_SurfaceDecalBase;
        if(caller==0x116fe9||caller==0x11ae61||caller==0x11bd83||caller==0x11c96c||caller==0x11d708){
            const auto a=reinterpret_cast<SurfaceAddress>(decal);
            auto it=g_SurfaceHiddenDecals.find(a);
            std::uint16_t slot=0;
            if(it!=g_SurfaceHiddenDecals.end()&&it->second.Surface==reinterpret_cast<SurfaceAddress>(surface)&&
               it->second.Material==reinterpret_cast<SurfaceAddress>(material)&&SurfaceGet(a+0x66,slot)&&slot==it->second.Slot){
                ++g_SurfaceDecalDrawSkips;return nullptr;
            }
        }
    }
    return g_SurfaceDecalGeometry(scratch,decal,surface,material);
}
static bool SurfaceDecalHooks(SurfaceAddress base){
    if(g_SurfaceDecalHookInstalled)return base==g_SurfaceDecalBase&&GetCurrentThreadId()==g_SurfaceDecalThread;
    if(!SurfaceMatch(base,0x119490,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x56})||
       !SurfaceMatch(base,0x116fe4,{0xe8,0xa7,0x24,0,0,0x48,0x85,0xc0,0x74,0x27})||
       !SurfaceMatch(base,0x11ae5c,{0xe8,0x2f,0xe6,0xff,0xff,0x4c,0x8b,0xf8,0x48,0x85,0xc0,0x0f,0x84})||
       !SurfaceMatch(base,0x11bd7e,{0xe8,0x0d,0xd7,0xff,0xff,0x4c,0x8b,0xf8,0x48,0x85,0xc0,0x0f,0x84})||
       !SurfaceMatch(base,0x11c967,{0xe8,0x24,0xcb,0xff,0xff,0x4c,0x8b,0xf0,0x48,0x85,0xc0,0x0f,0x84})||
       !SurfaceMatch(base,0x11d703,{0xe8,0x88,0xbd,0xff,0xff,0x48,0x89,0x45,0xa0,0x48,0x85,0xc0,0x0f,0x84}))return false;
    g_SurfaceDecalGeometry=reinterpret_cast<DecalGeometryFn>(base+0x119490);
    if(DetourTransactionBegin()!=NO_ERROR)return false;
    LONG err=DetourUpdateThread(GetCurrentThread());
    if(err==NO_ERROR)err=DetourAttach(reinterpret_cast<PVOID*>(&g_SurfaceDecalGeometry),reinterpret_cast<PVOID>(SurfaceDecalGeometryHook));
    if(err!=NO_ERROR){DetourTransactionAbort();return false;}
    if(DetourTransactionCommit()!=NO_ERROR)return false;
    g_SurfaceDecalBase=base;g_SurfaceDecalThread=GetCurrentThreadId();g_SurfaceDecalHookInstalled=true;return true;
}
struct ShadowSlot {ShadowHandle Handle=ShadowInvalid;unsigned char Bias=0;std::uint16_t Flags=0;};
static bool ShadowSlots(void *manager,std::vector<ShadowSlot> &out){
    const auto a=reinterpret_cast<SurfaceAddress>(manager);
    SurfaceAddress data=0;int capacity=0,head=0,tail=0,count=0;
    if(!SurfaceGet(a+0x10,data)||!SurfaceGet(a+0x18,capacity)||!SurfaceGet(a+0x20,head)||!SurfaceGet(a+0x24,tail)||!SurfaceGet(a+0x2c,count)||
       capacity<0||capacity>65536||count<0||count>2048||count>capacity||(count&&!data))return false;
    std::set<int> visited;int prev=ShadowInvalid,i=head;
    while(i!=ShadowInvalid){
        if(i<0||i>=capacity||visited.size()>=static_cast<unsigned>(count)||!visited.insert(i).second)return false;
        const auto item=data+SurfaceAddress(i)*0x160;ShadowSlot s;s.Handle=static_cast<ShadowHandle>(i);
        std::uint16_t back=0,next=0;
        if(!SurfaceGet(item+0x150,back)||back!=prev||!SurfaceGet(item+0x152,next)||!SurfaceGet(item+0x88,s.Flags)||!SurfaceGet(item+0x5c,s.Bias))return false;
        out.push_back(s);prev=i;i=next;
    }
    return visited.size()==static_cast<unsigned>(count)&&prev==tail;
}
static void SurfaceProjectHook(void *self,ShadowHandle handle,const float *origin,const float *dir,const float *matrix,const float *size,int count,const int *leaves,float height,float offset,float amount,const float *caster){
    g_SurfaceProject(self,handle,origin,dir,matrix,size,count,leaves,height,offset,amount,caster);
    if(GetCurrentThreadId()!=g_SurfaceThread||g_SurfaceOwn||g_SurfaceReplayActive||!g_SurfaceCaptureArmed||self!=g_SurfaceShadowManager)return;
    auto base=SurfaceEngine();NativeWorld world;
    if(!base||!SurfaceWorld(base,world)){g_SurfaceObserved.clear();return;}
    if(!SurfaceSameWorld(world,g_SurfaceWorld)){g_SurfaceWorld=world;++g_SurfaceGeneration;g_SurfaceObserved.clear();}
    Shadow s;s.Height=height;s.FalloffOffset=offset;s.FalloffAmount=amount;
    if(count<=0||count>8192||!leaves){g_SurfaceObserved.erase(handle);return;}
    s.Leaves.resize(count);
    if(!SurfaceGet(reinterpret_cast<SurfaceAddress>(origin),s.Origin)||!SurfaceGet(reinterpret_cast<SurfaceAddress>(dir),s.Direction)||
       !SurfaceGet(reinterpret_cast<SurfaceAddress>(matrix),s.Matrix)||!SurfaceGet(reinterpret_cast<SurfaceAddress>(size),s.Size)||
       !SurfaceGet(reinterpret_cast<SurfaceAddress>(caster),s.Caster)||!SurfaceRead(reinterpret_cast<SurfaceAddress>(leaves),s.Leaves.data(),count*sizeof(int))||!s.Valid()){
        g_SurfaceObserved.erase(handle);return;
    }
    if(g_SurfaceObserved.size()>=2048&&!g_SurfaceObserved.count(handle))return;
    g_SurfaceObserved[handle]=std::move(s);
}
static void SurfaceDestroyHook(void *self,ShadowHandle handle){
    if(!g_SurfaceOwn&&GetCurrentThreadId()==g_SurfaceThread&&self==g_SurfaceShadowManager){g_SurfaceObserved.erase(handle);g_SurfaceSavedBias.erase(handle);g_SurfaceOwned.erase(std::remove(g_SurfaceOwned.begin(),g_SurfaceOwned.end(),handle),g_SurfaceOwned.end());}
    g_SurfaceDestroy(self,handle);
}
static void SurfaceBiasHook(void *self,ShadowHandle handle,unsigned char bias){
    if(!g_SurfaceOwn&&g_SurfaceReplayActive&&GetCurrentThreadId()==g_SurfaceThread&&self==g_SurfaceShadowManager){
        auto it=g_SurfaceSavedBias.find(handle);if(it!=g_SurfaceSavedBias.end()){it->second=bias;bias=255;}
    }
    g_SurfaceBias(self,handle,bias);
}
static bool SurfaceShadowHooks(SurfaceAddress base){
    if(g_SurfaceHooks)return GetCurrentThreadId()==g_SurfaceThread;
    if(!SurfaceMatch(base,0x127de0,{0x4c,0x8b,0xdc,0x41,0x54,0x41,0x57,0x48,0x81,0xec,0xc8,0,0,0})||
       !SurfaceMatch(base,0x124b00,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10})||
       !SurfaceMatch(base,0x12b7e0,{0x0f,0xb7,0xc2,0x48,0x69,0xd0,0x60,0x01,0,0})||
       !SurfaceMatch(base,0x12a8ff,{0x80,0x7c,0x01,0x5c,0xff}))return false;
    using Factory=void*(*)(const char*,int*);
    auto factory=reinterpret_cast<Factory>(GetProcAddress(reinterpret_cast<HMODULE>(base),"CreateInterface"));
    if(!factory)return false;
    void *manager=factory("VEngineShadowMgr002",nullptr);
    SurfaceAddress vt=0;std::array<SurfaceAddress,20> slots{};
    if(!manager||!SurfaceGet(reinterpret_cast<SurfaceAddress>(manager),vt)||vt!=base+0x3959f0||!SurfaceGet(vt,slots)||
       slots[0]!=base+0x124900||slots[1]!=base+0x124b00||slots[3]!=base+0x127de0||slots[9]!=base+0x12c1b0||slots[15]!=base+0x12b7e0)return false;
    g_SurfaceProject=reinterpret_cast<ProjectFn>(slots[3]);g_SurfaceDestroy=reinterpret_cast<DestroyFn>(slots[1]);g_SurfaceBias=reinterpret_cast<BiasFn>(slots[15]);
    if(DetourTransactionBegin()!=NO_ERROR)return false;
    LONG err=DetourUpdateThread(GetCurrentThread());
    if(err==NO_ERROR)err=DetourAttach(reinterpret_cast<PVOID*>(&g_SurfaceProject),reinterpret_cast<PVOID>(SurfaceProjectHook));
    if(err==NO_ERROR)err=DetourAttach(reinterpret_cast<PVOID*>(&g_SurfaceDestroy),reinterpret_cast<PVOID>(SurfaceDestroyHook));
    if(err==NO_ERROR)err=DetourAttach(reinterpret_cast<PVOID*>(&g_SurfaceBias),reinterpret_cast<PVOID>(SurfaceBiasHook));
    if(err!=NO_ERROR){DetourTransactionAbort();return false;}
    if(DetourTransactionCommit()!=NO_ERROR)return false;
    g_SurfaceShadowManager=manager;g_SurfaceThread=GetCurrentThreadId();g_SurfaceHooks=true;return true;
}
static void *SurfaceSimpleShadowMaterial(){
    if(!g_AppSystemFactory)return nullptr;
    auto system=g_AppSystemFactory("VMaterialSystem082",nullptr);
    SurfaceAddress vt=0,fn=0;MEMORY_BASIC_INFORMATION mbi{};
    if(!system||!SurfaceGet(reinterpret_cast<SurfaceAddress>(system),vt)||!SurfaceGet(vt+0x238,fn)||
       !VirtualQuery(reinterpret_cast<void*>(fn),&mbi,sizeof(mbi))||mbi.AllocationBase!=GetModuleHandleA("materialsystem.dll"))return nullptr;
    using Find=void*(*)(void*,const char*,const char*,bool,const char*);
    auto material=reinterpret_cast<Find>(fn)(system,"decals/simpleshadow","Decal textures",false,nullptr);
    std::string name;
    return material&&SurfaceMaterialName(reinterpret_cast<SurfaceAddress>(material),name)&&name=="decals/simpleshadow"?material:nullptr;
}
} // namespace
#endif
advancedfx::SceneSurfaces::Frame Mirv_Tf2_CaptureSurfaces(int tick){
    advancedfx::SceneSurfaces::Frame sample;sample.Tick=tick;
#ifdef _WIN64
    auto base=SurfaceEngine();NativeWorld world;
    if(!base||!MaterialSystem_IsImmediate()||!SurfaceWorld(base,world))return sample;
    if(!SurfaceSameWorld(world,g_SurfaceWorld)){g_SurfaceWorld=world;++g_SurfaceGeneration;g_SurfaceObserved.clear();}
    sample.World=g_SurfaceGeneration;
    std::vector<LiveDecal> decals;
    sample.DecalsReady=ReadWorldDecals(base,world,decals,sample.DecalsUnsupported,sample.DecalStatus);
    if(sample.DecalsReady){auto values=std::make_shared<std::vector<Decal>>();values->reserve(decals.size());for(auto &d:decals)values->push_back(std::move(d.Value));sample.Decals=values;}
    if(SurfaceShadowHooks(base)){
        std::vector<ShadowSlot> slots;
        if(ShadowSlots(g_SurfaceShadowManager,slots)){
            auto shadows=std::make_shared<std::vector<Shadow>>();
            for(const auto &s:slots)if(!(s.Flags&6)&&s.Bias!=255){
                auto it=g_SurfaceObserved.find(s.Handle);
                if(it==g_SurfaceObserved.end()||shadows->size()>=128){++sample.ShadowsUnobserved;continue;}
                auto projection=it->second;projection.Bias=s.Bias;shadows->push_back(std::move(projection));
            }
            sample.Shadows=shadows;sample.ShadowsReady=true;
        }
    }
#endif
    return sample;
}
void Mirv_Tf2_SetSurfaceObservation(bool armed){
#ifdef _WIN64
    if(!armed&&g_SurfaceCaptureArmed)g_SurfaceObserved.clear();
    g_SurfaceCaptureArmed=armed;
#endif
}
void Mirv_Tf2_ResetSurfaces(){
#ifdef _WIN64
    if(!g_SurfaceReplayActive){g_SurfaceObserved.clear();g_SurfaceWorld={};++g_SurfaceGeneration;}
#endif
}
unsigned Mirv_Tf2_BeginSurfaceReplay(const advancedfx::SceneSurfaces::Frame &sample,unsigned requested,std::string &status){
    status="Historical surfaces unavailable or not eligible.";
#ifdef _WIN64
    const auto base=SurfaceEngine();NativeWorld world;
    if(!requested||!base||!Mirv_Tf2_ParticleIsolationReady()||!SurfaceWorld(base,world)||!SurfaceSameWorld(world,g_SurfaceWorld)||sample.World!=g_SurfaceGeneration||g_SurfaceReplayActive)return 0;
    unsigned active=0;std::ostringstream report;
    if((requested&1)&&sample.ShadowsReady&&sample.Shadows&&g_SurfaceHooks&&GetCurrentThreadId()==g_SurfaceThread){
        float visible=0;std::vector<ShadowSlot> slots;
        auto material=Mirv_Tf2_SceneDrawVisibility("r_shadows",visible,false)&&visible!=0?SurfaceSimpleShadowMaterial():nullptr;
        if(material&&ShadowSlots(g_SurfaceShadowManager,slots)){
            auto vt=*reinterpret_cast<void***>(g_SurfaceShadowManager);
            auto create=reinterpret_cast<CreateFn>(vt[0]);auto tex=reinterpret_cast<TexFn>(vt[9]);
            g_SurfaceOwn=true;
            for(const auto &s:*sample.Shadows){
                if(!s.Valid())continue;
                auto handle=create(g_SurfaceShadowManager,material,material,reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xffff)),0);
                if(handle==ShadowInvalid)continue;
                g_SurfaceOwned.push_back(handle);
                tex(g_SurfaceShadowManager,handle,0,0,1,1);
                g_SurfaceProject(g_SurfaceShadowManager,handle,s.Origin.data(),s.Direction.data(),s.Matrix.data(),s.Size.data(),static_cast<int>(s.Leaves.size()),s.Leaves.data(),s.Height,s.FalloffOffset,s.FalloffAmount,s.Caster.data());
                g_SurfaceBias(g_SurfaceShadowManager,handle,s.Bias);
            }
            g_SurfaceOwn=false;
            // Fail open on incomplete observation or creation. Keep live
            // shadows if replacement coverage cannot be established.
            const bool replace=sample.ShadowsUnobserved==0&&g_SurfaceOwned.size()==sample.Shadows->size();
            if(replace){
                for(const auto &s:slots)if(!(s.Flags&6)&&s.Bias!=255){g_SurfaceSavedBias[s.Handle]=s.Bias;g_SurfaceBias(g_SurfaceShadowManager,s.Handle,255);}
            }
            if(replace||!g_SurfaceOwned.empty())active|=1;
            report<<"Historical projected blobs "<<g_SurfaceOwned.size()<<"/"<<sample.Shadows->size()<<"; unobserved "<<sample.ShadowsUnobserved<<"; live bias gates "<<g_SurfaceSavedBias.size()<<"; "<<(replace?"replacement":"partial overlay")<<". ";
        }else report<<"Shadow material, manager, or r_shadows unavailable. ";
    }
    if(requested&2){
        if(!sample.DecalsReady||!sample.Decals)report<<"Historical decal capture unavailable: "<<DecalReadLabel(sample.DecalStatus)<<". ";
        else if(!SurfaceDecalHooks(base))report<<"Decal geometry gate unavailable (ABI or installation). ";
        else {
            std::vector<LiveDecal> current;std::size_t unsupported=0;DecalRead status=DecalRead::NotRun;
            if(ReadWorldDecals(base,world,current,unsupported,status)){
                std::size_t matched=0,hidden=0,failed=0;
                g_SurfaceHiddenDecals.clear();g_SurfaceDecalDrawSkips=0;
                for(const auto &live:current){
                    const auto &v=live.Value;
                    auto it=std::lower_bound(sample.Decals->begin(),sample.Decals->end(),v.Slot,[](const Decal &a,std::uint16_t s){return a.Slot<s;});
                    const bool same=it!=sample.Decals->end()&&it->Same(v);matched+=same;
                    if(!same||it->Alpha==0){
                        DecalGate gate;gate.Slot=v.Slot;
                        if(SurfaceGet(live.Object+0x10,gate.Surface)&&SurfaceGet(live.Object+0x18,gate.Material)){
                            g_SurfaceHiddenDecals.emplace(live.Object,gate);++hidden;
                        }else ++failed;
                    }
                }
                g_SurfaceDecalGate=true;active|=2;
                report<<"Historical surviving decals "<<matched<<"/"<<sample.Decals->size()<<"; geometry gates "<<hidden<<"; missing "<<sample.Decals->size()-matched<<"; unsupported live "<<unsupported<<"; rejected gates "<<failed<<". ";
            }else report<<"Live decal traversal failed: "<<DecalReadLabel(status)<<". ";
        }
    }
    g_SurfaceReplayActive=active!=0||!g_SurfaceOwned.empty();
    g_SurfaceReport=report.str();if(!g_SurfaceReport.empty())status=g_SurfaceReport;return active;
#else
    return 0;
#endif
}
std::string Mirv_Tf2_EndSurfaceReplay(){
#ifdef _WIN64
    const bool gated=g_SurfaceDecalGate;
    const auto decalSkips=g_SurfaceDecalDrawSkips;
    g_SurfaceDecalGate=false;g_SurfaceHiddenDecals.clear();g_SurfaceDecalDrawSkips=0;
    if(!g_SurfaceReplayActive)return {};
    std::size_t failed=0;const auto base=SurfaceEngineModule();NativeWorld world;
    if(base&&SurfaceWorld(base,world)&&SurfaceSameWorld(world,g_SurfaceWorld)){
        if(!g_SurfaceOwned.empty()||!g_SurfaceSavedBias.empty()){
            std::vector<ShadowSlot> slots;
            if(ShadowSlots(g_SurfaceShadowManager,slots)){
                g_SurfaceOwn=true;
                for(const auto &s:slots){
                    auto it=g_SurfaceSavedBias.find(s.Handle);
                    if(it!=g_SurfaceSavedBias.end()&&s.Bias==255)g_SurfaceBias(g_SurfaceShadowManager,s.Handle,it->second);
                }
                for(auto handle:g_SurfaceOwned)if(std::any_of(slots.begin(),slots.end(),[&](const ShadowSlot &s){return s.Handle==handle;}))g_SurfaceDestroy(g_SurfaceShadowManager,handle);
                g_SurfaceOwn=false;
            }else failed+=g_SurfaceOwned.size()+g_SurfaceSavedBias.size();
        }
    }else failed+=g_SurfaceOwned.size()+g_SurfaceSavedBias.size();
    g_SurfaceOwned.clear();g_SurfaceSavedBias.clear();g_SurfaceReplayActive=false;
    return g_SurfaceReport+(gated?" Native decal geometry skips "+std::to_string(decalSkips)+".":"")+" Scope cleanup warnings "+std::to_string(failed)+".";
#else
    return {};
#endif
}
