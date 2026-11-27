#include "shared/NativeParticleSnapshot.h"
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <map>
using namespace advancedfx::NativeParticles;
static void check(bool b,const char *s){if(!b){std::cerr<<s<<"\n";std::exit(1);}}
int main(){
    using namespace Layout;
    constexpr Address base=0x10000000, root=0x20000000, child=0x30000000, xyz=0x40000000;
    std::map<Address,unsigned char> bytes;
    auto put=[&](Address a,const auto &v){const auto *p=reinterpret_cast<const unsigned char*>(&v);for(std::size_t i=0;i<sizeof(v);++i)bytes[a+i]=p[i];};
    auto read=[&](Address a,void *out,std::size_t n){auto *p=static_cast<unsigned char*>(out);for(std::size_t i=0;i<n;++i){auto it=bytes.find(a+i);if(it==bytes.end())return false;p[i]=it->second;}return true;};
    put(base+ManagerReady,std::uint32_t(1));put(base+Manager+RootHead,root);
    put(root,base+RootVtable);put(root+RootNext,Address(0));put(root+RootPrev,Address(0));
    const auto coll=root+RootCollection;
    put(coll+Active,5);put(coll+ChildHead,child);put(coll+Xyz,xyz);put(coll+XyzStride,Address(12));
    const float p[24]={1,2,3,4,10,20,30,40,100,200,300,400,5,6,7,8,50,60,70,80,500,600,700,800};
    put(xyz,p);
    put(child+Active,2);put(child+ChildHead,Address(0));put(child+ChildNext,Address(0));put(child+Xyz,xyz);put(child+XyzStride,Address(0));
    auto f=Capture(base,10,read);
    check(f.Complete&&f.Roots==1&&f.Children==1&&f.Active==7&&f.Points.size()==7,"root and child coverage");
    check(f.Points[4]==Point{5,50,500}&&f.Points[6]==Point{2,20,200},"four-wide xyz and constant stride");
    History h(4096);check(h.Append(f)&&h.At(10)&&!h.At(11),"exact tick history");
    put(xyz,0.0f);auto newer=Capture(base,11,read);check(h.Append(newer)&&h.At(10)->Points[0][0]==1&&h.At(11)->Points[0][0]==0,"owned history");
    put(child+ChildNext,child);auto cycle=Capture(base,12,read);check(!cycle.Complete&&cycle.Rejected,"child cycle bounded");
    put(child+ChildNext,Address(0));put(coll+XyzStride,Address(99));auto invalid=Capture(base,12,read);check(!invalid.Complete&&invalid.Points.size()==2,"unexpected stride rejected");
    put(root,Address(0));check(!Capture(base,13,read).Complete,"root vtable guard");
    const auto oldBytes=h.Bytes,oldRevision=h.Revision;
    auto oldLease=h.ShareForPreview(11);auto invalidTick=f;invalidTick.Tick=-1;
    check(!h.Append(invalidTick)&&h.Bytes==oldBytes&&h.Revision==oldRevision&&oldLease==h.ShareForPreview(11),"invalid tick preserves interval and lease");
    // The one-entry decode cache legitimately changes when accessing another
    // tick; verify lease identity before performing those lookups.
    check(h.At(10)&&h.At(11),"invalid tick preserves both recorded samples");
    History retain(HistoryStorage::Store(f).BytesUsed());check(retain.Append(f),"retention budget");
    auto oversized=f;oversized.Tick=9;oversized.Points.resize(2048);
    for(std::size_t i=0;i<oversized.Points.size();++i)oversized.Points[i]={float(i),float(i*i),float(i%11)};
    check(HistoryStorage::Store(oversized).BytesUsed()>retain.BudgetBytes(),"oversized fixture");
    auto retainedLease=retain.ShareForPreview(10);const auto retainedBytes=retain.Bytes;
    check(!retain.Append(oversized)&&retain.At(10)&&retain.Bytes==retainedBytes&&retain.ShareForPreview(10)==retainedLease,"oversized backward sample preserves interval");
    check(h.Append(f)&&!h.At(11),"backward tick resets history");
    History small(4096);check(small.Append(f),"initial budget sample");small.SetBudget((std::max)(small.Bytes,HistoryStorage::Store(newer).BytesUsed()));check(small.Append(newer)&&small.Frames.size()==1&&small.At(11),"rolling budget");
    // Restore root list and positions; synthesize the indexed attribute arrays.
    put(root,base+RootVtable);put(coll+XyzStride,Address(12));
    constexpr Address def=0x50000000,name=0x51000000,radius=0x52000000,tint=0x53000000,ids=0x54000000;
    char text[128]="test_effect";
    put(def+DefinitionName,name);put(name,text);
    put(coll+Definition,def);put(child+Definition,def);put(coll+Clock,2.5f);put(child+Clock,1.5f);
    std::array<Address,AttributeCount> pointers{},strides{};
    pointers[Position]=xyz;strides[Position]=12;
    pointers[Radius]=radius;strides[Radius]=4;
    pointers[Tint]=tint;strides[Tint]=0;
    pointers[ParticleId]=ids;strides[ParticleId]=4;
    const float radii[8]={1,2,3,4,5,6,7,8};
    const float colors[12]={.1f,.2f,.3f,.4f,.5f,.6f,.7f,.8f,.9f,1,.1f,.2f};
    const std::uint32_t idWords[8]={1,2,3,4,0x7fc00000u,6,7,8};
    put(radius,radii);put(tint,colors);put(ids,idWords);
    put(coll+Xyz,pointers);put(coll+XyzStride,strides);
    strides[Position]=0;put(child+Xyz,pointers);put(child+XyzStride,strides);
    auto app=Capture(base,20,read,true);
    check(app.Complete&&app.AppearanceComplete&&app.Collections.size()==2&&app.NamedCollections==2,"full appearance metadata");
    check(app.Collections[0].Parent==-1&&app.Collections[1].Parent==0&&app.Collections[1].PointStart==5,"frame-local hierarchy");
    check(std::string(app.Collections[0].Name.data())=="test_effect"&&app.Collections[0].HasTime&&app.Collections[0].Time==2.5f,"owned definition name and clock");
    float v=0;std::uint32_t bits=0;
    check(Float(app,app.Collections[0],Radius,4,0,v)&&v==5,"varying scalar second block");
    check(Float(app,app.Collections[0],Tint,4,2,v)&&v==.9f,"constant vec3 repeated across blocks");
    check(Word(app,app.Collections[0],ParticleId,4,0,bits)&&bits==0x7fc00000u,"integer bit pattern retained");
    v=17;check(!Float(app,app.Collections[0],ParticleId,4,0,v)&&v==17,"invalid float preserves caller fallback");
    check(!Word(app,app.Collections[0],Alpha,0,0,bits)&&!Word(app,app.Collections[0],Radius,5,0,bits),"unavailable and out-of-range fields");
    put(radius,99.0f);check(Float(app,app.Collections[0],Radius,0,0,v)&&v==1,"independent attribute storage");
    put(coll+XyzStride+8*Radius,Address(99));auto partial=Capture(base,21,read,true);
    check(partial.Complete&&!partial.AppearanceComplete&&partial.AppearanceRejected&&partial.Points.size()==7,"appearance failure preserves positions");
    auto old=Capture(base,22,read,false);check(old.Complete&&!old.Appearance&&old.Collections.empty()&&old.Words.empty(),"position-only fallback stays opt-in");
    check(app.Bytes()>old.Bytes(),"appearance included in rolling payload budget");
    check(Components(Rotation)==3&&Components(TraceNormal)==1,"native component mask");
    constexpr Address rot=0x55000000;
    pointers[Rotation]=rot;strides[Rotation]=12;put(rot,p);
    strides[Position]=12;strides[Radius]=4;put(coll+Xyz,pointers);put(coll+XyzStride,strides);
    auto rotation=Capture(base,23,read,true);
    check(rotation.AppearanceComplete&&Float(rotation,rotation.Collections[0],Rotation,4,2,v)&&v==500,"three-component rotation storage");
    History gaps(4096);auto g=f;g.Tick=30;check(gaps.Append(g),"first sample");g.Tick=33;check(gaps.Append(g),"second sample");
    check(gaps.ForPreview(31)->Tick==30&&gaps.ForPreview(32)->Tick==30&&gaps.ForPreview(33)->Tick==33,"causal short gap hold");
    check(!gaps.ForPreview(29)&&!gaps.ForPreview(34)&&!gaps.ForPreview(32,1),"never extrapolate or bridge long gap");
    // A single detached node with sequential owned varying allocations.
    constexpr Address detached=0x60000000,mem=0x61000000;
    auto simple=app;simple.Collections.resize(1);auto &c=simple.Collections[0];
    put(detached+Definition,def);put(detached+Active,0);put(detached+0x60,12);put(detached+0x64,8);
    put(detached+ChildHead,Address(0));put(detached+0x22e8,std::array<unsigned char,0x328>{});put(detached+0x22e8,mem);
    std::array<Address,32> dp{},ds{};dp[Position]=mem;ds[Position]=12;
    dp[Radius]=mem+12*3*4;ds[Radius]=4;
    dp[Tint]=tint;dp[ParticleId]=dp[Radius]+12*4;ds[ParticleId]=4;
    put(detached+Xyz,dp);put(detached+XyzStride,ds);
    ChildIndex topology;
    check(BuildChildIndex(simple,topology)&&topology.size()==simple.Collections.size(),"prepared recorded topology");
    std::size_t reads=0;
    auto counted=[&](Address a,void *out,std::size_t n){++reads;return read(a,out,n);};
    auto plan=PlanDetached(simple,0,detached,counted,&topology);
    check(reads==7,"detached header and attribute tables use bounded reads");
    auto narrow=[&](Address a,void *out,std::size_t n){if((a==detached+Definition&&n==16)||(a==detached+0x22e8&&n==0x328))return false;return read(a,out,n);};
    auto fallback=PlanDetached(simple,0,detached,narrow,&topology);
    check(fallback.Error.empty()&&fallback.Copies.size()==plan.Copies.size()&&fallback.Particles==plan.Particles,"narrow field fallback preserves restore plan");
    auto invalidTopology=simple;invalidTopology.Collections[0].Parent=0;
    check(!BuildChildIndex(invalidTopology,topology),"cyclic recorded hierarchy rejected");
    check(plan.Error.empty()&&plan.Nodes==1&&plan.Particles==5&&plan.Copies.size()==3&&plan.Constants==1,"validated detached copy plan");
    put(name,'X');check(!PlanDetached(simple,0,detached,read).Error.empty(),"batched name mismatch rejected");put(name,text);
    dp[Radius]+=4;put(detached+Xyz,dp);
    check(!PlanDetached(simple,0,detached,read).Error.empty()&&PlanDetached(simple,0,detached,read).Copies.empty(),"wrong allocation address fails closed");
    check(app.SharedConstantWords>=12,"identical constant tint shared between collections");
    check(app.Collections[0].Attributes[Tint].Start==app.Collections[1].Attributes[Tint].Start,"shared constant offsets remain readable");
    check(Float(app,app.Collections[1],Tint,1,1,v)&&v==.6f,"shared lane data unchanged");
    History resized(4096);check(resized.Append(f)&&resized.Append(newer),"budget fixture");
    resized.SetBudget((std::max)(HistoryStorage::Store(f).BytesUsed(),HistoryStorage::Store(newer).BytesUsed()));
    check(resized.Frames.size()==1&&resized.At(11)&&resized.Evicted==1,"budget shrink evicts oldest");
    resized.SetBudget(4096);check(resized.Frames.size()==1&&resized.Evicted==1,"budget expansion cannot restore evicted frames");
    resized.SetBudget(0);check(resized.Frames.empty()&&resized.Bytes==0&&resized.Evicted==2,"budget stays bounded");
    check(!resized.Append(f)&&resized.Rejected==1,"oversized frame counted");
    // Independent lossless storage, including non-finite integer bit patterns,
    // names, hierarchy, signed zero, constant sharing and incomplete positions.
    auto stored=HistoryStorage::Store(app);Frame restored;
    check(HistoryStorage::Load(stored,restored),"decode appearance snapshot");
    check(HistoryStorage::Pack(restored)==HistoryStorage::Pack(app)&&restored.Words==app.Words&&restored.Points==app.Points,"lossless appearance roundtrip");
    check(Float(restored,restored.Collections[1],Tint,1,1,v)&&v==.6f,"restored shared attributes");
    auto broken=partial;broken.Points[0][0]=-0.0f;
    check(HistoryStorage::Load(HistoryStorage::Store(broken),restored)&&std::signbit(restored.Points[0][0]),"fallback points preserve bits");
    History random(4096*4);check(random.Append(app)&&random.Append(rotation),"compressed appearance history");
    check(random.At(23)&&random.At(20)&&random.At(20)->Words==app.Words&&random.At(23)->Words==rotation.Words,"reverse and repeated random access");
    auto leased=random.ShareForPreview(23);
    check(leased&&leased.get()==random.ShareForPreview(23).get(),"stationary preview reuses immutable decoded storage");
    check(random.ShareForPreview(22)->Tick==20&&leased->Words==rotation.Words,"lease survives different tick decoding");
    auto changed=rotation;changed.Words[0]^=1;
    check(random.Append(changed)&&random.At(23)->Words==changed.Words,"same tick replacement invalidates decoded cache");
    check(random.RawBytes>=random.Bytes,"synthetic snapshots reduced");
    check(leased->Words==rotation.Words&&random.ShareForPreview(23).get()!=leased.get(),"lease survives replacement at same tick");
    random.Clear();
    check(!random.ShareForPreview(23)&&random.DecodedBytes()==0&&leased->Words==rotation.Words,"lease survives clear without stale lookup");
    HistoryCodec::Bytes repeat(100000,42),decoded;
    auto encoded=HistoryCodec::Encode(repeat);
    check(encoded.size()<3000&&HistoryCodec::Decode(encoded,repeat.size(),decoded)&&decoded==repeat,"repeated run and overlapping match");
    HistoryCodec::Bytes noise(20000);std::uint32_t rng=12345;
    for(auto &b:noise){rng=rng*1664525u+1013904223u;b=static_cast<std::uint8_t>(rng>>24);}
    check(HistoryCodec::Decode(HistoryCodec::Encode(noise),noise.size(),decoded)&&decoded==noise,"incompressible bytes roundtrip");
    check(!HistoryCodec::Decode({128,0,0},4,decoded)&&!HistoryCodec::Decode({127,1},128,decoded),"malformed codec input rejected");
    check(!HistoryCodec::Decode(encoded,repeat.size()-1,decoded),"decoded size bound");
    check(plan.Bindings.size()==1&&plan.Bindings[0].Native==detached&&plan.Bindings[0].Recorded==0&&plan.Bindings[0].Definition==def,"native activation binding");
    std::vector<CopyOp> copies;
    AppendCopy(copies,0,{0x10000,0,12});AppendCopy(copies,0,{0x10030,12,4});
    check(copies.size()==1&&copies[0].Count==16,"adjacent source/destination coalesce");
    AppendCopy(copies,0,{0x10044,16,4});check(copies.size()==2,"destination gap never overwritten");
    AppendCopy(copies,0,{0x10054,21,4});check(copies.size()==3,"source gap never overwritten");
    AppendCopy(copies,copies.size(),{0x10064,25,4});check(copies.size()==4,"no cross-collection merge");
    auto prefix=ActivationPrefix(1.25f,5);float clock=0;std::memcpy(&clock,&prefix[5],sizeof(clock));
    check(clock==1.25f&&prefix[0]==prefix[5]&&prefix[1]==prefix[5]&&prefix[2]==prefix[5]&&prefix[3]==prefix[5]&&prefix[4]==2,"activation prefix preserves all clock/count fields");
    Frame forest;forest.Collections.resize(7);
    forest.Collections[0].Parent=-1;forest.Collections[1].Parent=0;forest.Collections[2].Parent=1;
    forest.Collections[3].Parent=0;forest.Collections[4].Parent=-1;forest.Collections[5].Parent=4;forest.Collections[6].Parent=-1;
    forest.Collections[2].Active=4;forest.Collections[3].Active=8;forest.Collections[4].Active=3;
    std::vector<std::size_t> visited;
    ReplayForest(forest,[&](std::size_t i,std::size_t active){visited.push_back(i);check(active>0,"empty branches skipped");return i!=0;});
    check(visited==std::vector<std::size_t>{0,1,3,4},"root success avoids duplicates; root failure falls back to child subtrees");
    ChildIndex forestIndex;check(BuildChildIndex(forest,forestIndex),"forest topology");
    visited.clear();ReplayForest(forest,[&](std::size_t i,std::size_t){visited.push_back(i);return i!=0;},&forestIndex);
    check(visited==std::vector<std::size_t>{0,1,3,4},"prepared forest traversal identical");
    forestIndex[0].clear();visited.clear();ReplayForest(forest,[&](std::size_t i,std::size_t){visited.push_back(i);return true;},&forestIndex);
    check(visited.empty(),"missing prepared edge rejected before submission");
    // Recorded preflight preserves root-first fallback: an incomplete
    // child rejects the parent factory attempt, not a valid sibling.
    Frame full;full.Complete=full.Appearance=true;full.Collections.resize(3);
    for(std::size_t i=0;i<full.Collections.size();++i){
        auto &node=full.Collections[i];node.Parent=i?0:-1;node.Name[0]='x';node.HasTime=true;node.Time=1;
        node.Active=i?4:0;
        if(node.Active)for(unsigned a=0;a<Layout::AttributeCount;++a){
            node.Mask|=1u<<a;const auto width=4u*Components(a);
            node.Attributes[a]={static_cast<std::uint32_t>(full.Words.size()),width,0};full.Words.resize(full.Words.size()+width);
        }
    }
    std::vector<ReplayPrerequisite> eligible;
    check(BuildReplayPrerequisites(full,eligible)&&std::all_of(eligible.begin(),eligible.end(),[](auto why){return why==ReplayPrerequisite::Ready;}),"complete recorded tree eligible");
    auto damaged=full;damaged.Collections[1].Mask&=~1u;
    check(BuildReplayPrerequisites(damaged,eligible)&&eligible[0]==ReplayPrerequisite::Descendant&&eligible[1]==ReplayPrerequisite::Mask&&eligible[2]==ReplayPrerequisite::Ready,"invalid child blocks parent only");
    std::vector<std::size_t> factory;
    ReplayForest(damaged,[&](std::size_t i,std::size_t){if(eligible[i]!=ReplayPrerequisite::Ready)return false;factory.push_back(i);return true;});
    check(factory==std::vector<std::size_t>{2},"recorded-only rejection skips impossible factories, preserves valid sibling");
    damaged=full;damaged.Collections[1].Attributes[Position].Start=0xffffffffu;
    check(BuildReplayPrerequisites(damaged,eligible)&&eligible[1]==ReplayPrerequisite::Block,"recorded block bounds");
    damaged=full;damaged.Collections[1].Active=Layout::MaxDetachedActive+1;
    check(BuildReplayPrerequisites(damaged,eligible)&&eligible[1]==ReplayPrerequisite::Capacity,"recorded native capacity limit");
    damaged=full;damaged.Collections[1].Name.fill('x');
    check(BuildReplayPrerequisites(damaged,eligible)&&eligible[1]==ReplayPrerequisite::Name,"unterminated recorded definition rejected");
    damaged=full;damaged.Collections[0].HasTime=false;
    check(BuildReplayPrerequisites(damaged,eligible)&&eligible[0]==ReplayPrerequisite::Clock&&eligible[1]==ReplayPrerequisite::Ready,"empty node still requires clock, children remain eligible");
    damaged.Collections[1].Parent=1;check(!BuildReplayPrerequisites(damaged,eligible)&&eligible.empty(),"malformed recorded hierarchy rejected");
    Frame empty;empty.Complete=empty.Appearance=empty.AppearanceComplete=true;
    check(RecordedIsolationReady(empty),"complete empty capture may isolate live effects");
    full.AppearanceComplete=true;full.Active=8;full.Points.resize(8);
    check(RecordedIsolationReady(full),"compatible complete tree may isolate");
    auto immutable=std::make_shared<const Frame>(full);
    ReplayPreparation prep(immutable);
    check(prep.Matches(*immutable)&&prep.Valid()&&prep.IsolationReady()&&!prep.Matches(full),"preparation is bound to exact immutable lease");
    std::vector<std::size_t> planned,ordinary;
    prep.Walk(*immutable,[&](std::size_t i,std::size_t count){planned.push_back(i);check(count>0,"prepared traversal skips empty subtrees");return i!=0;});
    ReplayForest(*immutable,[&](std::size_t i,std::size_t){ordinary.push_back(i);return i!=0;});
    check(planned==ordinary&&planned==std::vector<std::size_t>{0,1,2},"prepared fallback order matches ordinary replay");
    planned.clear();prep.Walk(full,[&](std::size_t i,std::size_t){planned.push_back(i);return true;});
    check(planned.empty(),"mismatched frame cannot consume cached topology");
    std::weak_ptr<const Frame> lifetime=immutable;immutable.reset();
    check(lifetime.expired(),"preparation does not pin decoded appearance storage");
    auto malformed=std::make_shared<const Frame>(damaged);
    ReplayPreparation badPrep(malformed);
    check(!badPrep.Valid()&&!badPrep.IsolationReady(),"invalid topology cannot authorize prepared replay");
    auto incomplete=full;incomplete.Collections[1].Mask&=~1u;
    check(!RecordedIsolationReady(incomplete),"incompatible recorded tree cannot authorize blanket hiding");
    incomplete=full;incomplete.Active=7;check(!RecordedIsolationReady(incomplete),"recorded active total mismatch");
    incomplete=full;incomplete.Points.pop_back();check(!RecordedIsolationReady(incomplete),"missing recorded positions");
    incomplete=full;incomplete.AppearanceRejected=1;check(!RecordedIsolationReady(incomplete),"capture rejection prevents isolation");
    incomplete=empty;incomplete.Collections.resize(1);
    check(RecordedIsolationReady(incomplete),"unused empty root does not prevent isolation");
    std::cout<<"Native particle snapshot tests passed\n";
}
