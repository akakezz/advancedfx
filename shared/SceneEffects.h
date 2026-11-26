#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <deque>
#include <map>
#include <cstdint>

// Tool messages describe effect lifecycle/control points, NOT simulated particles.
// Copy their transient state synchronously; never retain engine pointers.
namespace advancedfx { namespace SceneEffects {
enum class Kind : unsigned char { Create, Destroy, Stop, Object, Position, Orientation, Count };
struct Event {
    int Tick = -1, Id = -1, Point = -1, Object = -1;
    float ClientTime = 0;
    Kind Type = Kind::Count;
    bool InfiniteOnly = false;
    std::array<float,4> Value{};
    std::array<char,128> Name{};
};
// Public Source SDK 2013 toolframework/itoolframework.h layouts.
// CNewParticleEffect::Construct/SetControlPoint/etc. publish these synchronously.
// Natural pointer alignment differs on x86/x64; all other payloads are fixed width.
namespace Wire {
struct Create { int Id; const char *Name; float Time; int Owner; };
struct Destroy { int Id; float Time; };
struct Stop { int Id; float Time; unsigned char InfiniteOnly; };
struct Object { int Id; float Time; int Point, Entity; };
struct Position { int Id; float Time; int Point; float Value[3]; };
struct Orientation { int Id; float Time; int Point; float Value[4]; };
static_assert(offsetof(Create,Name)==(sizeof(void*)==8?8:4) &&
              offsetof(Create,Time)==(sizeof(void*)==8?16:8) &&
              sizeof(Position)==24 && sizeof(Orientation)==28 &&
              sizeof(Destroy)==8 && sizeof(Stop)==12 && sizeof(Object)==16,
              "Unexpected Source particle tool message ABI");
template<class T> T Read(const void *p) { T v{}; std::memcpy(&v,p,sizeof(v)); return v; }
}
enum class DecodeResult { Ignore, Unknown, Invalid, Valid };
inline DecodeResult Decode(const char *name, const void *state, int tick, Event &out) {
    if (!name || std::strncmp(name,"ParticleSystem_",15)!=0) return DecodeResult::Ignore;
    Event e; e.Tick=tick;
    const char *op=name+15;
    if (!std::strcmp(op,"Create")) e.Type=Kind::Create;
    else if (!std::strcmp(op,"Destroy")) e.Type=Kind::Destroy;
    else if (!std::strcmp(op,"StopEmission")) e.Type=Kind::Stop;
    else if (!std::strcmp(op,"SetControlPointObject")) e.Type=Kind::Object;
    else if (!std::strcmp(op,"SetControlPointPosition")) e.Type=Kind::Position;
    else if (!std::strcmp(op,"SetControlPointOrientation")) e.Type=Kind::Orientation;
    else return DecodeResult::Unknown;
    if (!state || tick<0) return DecodeResult::Invalid;
    switch(e.Type) {
    case Kind::Create: {
        auto v=Wire::Read<Wire::Create>(state); e.Id=v.Id; e.ClientTime=v.Time; e.Object=v.Owner;
        if (!v.Name || !v.Name[0]) return DecodeResult::Invalid;
        for (std::size_t i=0;i+1<e.Name.size() && v.Name[i];++i) e.Name[i]=v.Name[i];
        break;
    }
    case Kind::Destroy: {auto v=Wire::Read<Wire::Destroy>(state); e.Id=v.Id; e.ClientTime=v.Time; break;}
    case Kind::Stop: {auto v=Wire::Read<Wire::Stop>(state); e.Id=v.Id; e.ClientTime=v.Time; e.InfiniteOnly=v.InfiniteOnly!=0; break;}
    case Kind::Object: {auto v=Wire::Read<Wire::Object>(state); e.Id=v.Id; e.ClientTime=v.Time; e.Point=v.Point; e.Object=v.Entity; break;}
    case Kind::Position: {auto v=Wire::Read<Wire::Position>(state); e.Id=v.Id; e.ClientTime=v.Time; e.Point=v.Point;
        for (int i=0;i<3;++i) e.Value[i]=v.Value[i]; break;}
    case Kind::Orientation: {auto v=Wire::Read<Wire::Orientation>(state); e.Id=v.Id; e.ClientTime=v.Time; e.Point=v.Point;
        for (int i=0;i<4;++i) e.Value[i]=v.Value[i]; break;}
    default: return DecodeResult::Invalid;
    }
    if (e.Id<0 || !std::isfinite(e.ClientTime)) return DecodeResult::Invalid;
    if (e.Type==Kind::Object || e.Type==Kind::Position || e.Type==Kind::Orientation) {
        if (e.Point<0 || e.Point>=64) return DecodeResult::Invalid;
        for (float v:e.Value) if (!std::isfinite(v)) return DecodeResult::Invalid;
    }
    out=e;
    return DecodeResult::Valid;
}
struct ControlPoint {
    int Object=-1;
    bool HasObject=false, HasPosition=false, HasOrientation=false;
    std::array<float,4> Position{}, Orientation{};
};
struct System {
    std::array<char,128> Name{};
    int Owner=-1, CreatedTick=-1;
    bool StopReported=false, InfiniteOnly=false;
    std::map<int,ControlPoint> Points;
};
struct Sample {
    std::map<int,System> Systems;
    std::size_t UnknownUpdates=0, UnmatchedDestroys=0, RecreatedIds=0;
    bool InWindow=false, Truncated=false, Interrupted=false;
};
class Journal {
public:
    static constexpr std::size_t DefaultLimit=131072;
    explicit Journal(std::size_t limit=DefaultLimit):Limit(limit){}
    void Clear() {
        Events.clear(); Counts.fill(0); Dropped=Invalid=Unknown=0;
        First=Last=FirstDrop=-1; Interrupted=false;
    }
    // A backward seek is a new history, never merge two different passes.
    void Observe(int tick) {
        if(tick<0) return;
        if(Last>=0 && tick<Last) Clear();
        if(First<0) First=tick;
        Last=tick;
    }
    void Interrupt() { if(First>=0) Interrupted=true; }
    void Push(const Event &e) {
        if(e.Tick<0 || e.Type==Kind::Count) return;
        Observe(e.Tick);
        if(Events.size()>=Limit) {if(FirstDrop<0) FirstDrop=e.Tick; ++Dropped; return;}
        Events.push_back(e); ++Counts[static_cast<std::size_t>(e.Type)];
    }
    void Note(DecodeResult result) {
        if(result==DecodeResult::Invalid) ++Invalid;
        else if(result==DecodeResult::Unknown) ++Unknown;
    }
    Sample At(int tick) const {
        Sample s; s.InWindow=First>=0 && tick>=First && tick<=Last;
        if(!s.InWindow) return s;
        s.Truncated=FirstDrop>=0 && tick>=FirstDrop;
        s.Interrupted=Interrupted;
        for(const auto &e:Events) {
            if(e.Tick>tick) break;
            auto found=s.Systems.find(e.Id);
            if(e.Type==Kind::Create) {
                if(found!=s.Systems.end()) ++s.RecreatedIds;
                System &system=s.Systems[e.Id]; system=System{};
                system.Name=e.Name; system.Owner=e.Object; system.CreatedTick=e.Tick;
            } else if(e.Type==Kind::Destroy) {
                if(found==s.Systems.end()) ++s.UnmatchedDestroys;
                else s.Systems.erase(found);
            } else if(found==s.Systems.end()) ++s.UnknownUpdates;
            else if(e.Type==Kind::Stop) {
                found->second.StopReported=true; found->second.InfiniteOnly=e.InfiniteOnly;
            } else {
                auto &p=found->second.Points[e.Point];
                if(e.Type==Kind::Object) {p.Object=e.Object; p.HasObject=true;}
                else if(e.Type==Kind::Position) {p.Position=e.Value; p.HasPosition=true;}
                else if(e.Type==Kind::Orientation) {p.Orientation=e.Value; p.HasOrientation=true;}
            }
        }
        return s;
    }
    std::deque<Event> Events;
    std::array<std::size_t,static_cast<std::size_t>(Kind::Count)> Counts{};
    std::size_t Dropped=0, Invalid=0, Unknown=0;
    int First=-1, Last=-1, FirstDrop=-1;
    bool Interrupted=false;
private:
    std::size_t Limit;
};
}} // namespace advancedfx::SceneEffects
