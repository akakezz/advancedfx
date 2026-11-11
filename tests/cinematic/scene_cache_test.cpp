#include "shared/SceneCache.h"
#include <cassert>
#include <limits>
int main()
{
 using Cache=advancedfx::SceneCache;
 Cache cache; Cache::Frame a; a.DemoTime=10;a.ClientTime=110;
 Cache::Entity entity;entity.Identity=101;entity.Angles[1]=179;a.Entities.push_back(entity);
 assert(cache.Append(a));assert(!cache.Append(a));
 auto b=a;b.DemoTime=10.05;b.ClientTime=110.05;b.Entities[0].Origin[0]=10;b.Entities[0].Angles[1]=-179;
 assert(cache.Append(b));Cache::Frame out;
 assert(cache.Sample(10.025,out));assert(std::abs(out.ClientTime-110.025)<1e-8);
 assert(std::abs(out.Entities[0].Origin[0]-5)<.001);assert(std::abs(out.Entities[0].Angles[1]-180)<.001);
 assert(!cache.Sample(9,out));assert(!cache.Sample(11,out));
 b.DemoTime=10.1;b.ClientTime=110.1;b.Entities[0].Identity=102;assert(cache.Append(b));
 assert(cache.Sample(10.075,out)&&out.Entities.empty());
 b.DemoTime=11;assert(cache.Append(b));assert(!cache.Sample(10.5,out));
 b.DemoTime=12;b.Entities[0].Origin[0]=1000;assert(cache.Append(b));
 assert(cache.Sample(11.5,out,2)&&out.Entities.empty());
 b.DemoTime=1;assert(cache.Append(b)&&cache.Count()==1);
 b.DemoTime=2;b.Entities[0].Origin[0]=std::numeric_limits<float>::quiet_NaN();assert(!cache.Append(b));
 cache.SetLimit(0);assert(cache.Count()==0&&cache.Bytes()==0);
}
