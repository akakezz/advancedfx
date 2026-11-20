"""Exercise production SceneModelResources and render-queue wrappers without the game.

The test extracts the actual C++ implementations from the repository and
compiles fake Source interfaces, so changes to the production lifetime logic
are exercised rather than a separate reimplementation.
Run: python tests/cinematic/test_scene_render_queue.py [--sanitize]
"""
from pathlib import Path
import argparse
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--build-dir', type=Path, default=Path(tempfile.gettempdir()) / 'hlae-scene-retirement-tests')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = args.build_dir.resolve()
out.mkdir(parents=True, exist_ok=True)

def extract(content, begin, end):
    start = content.index(begin)
    finish = content.index(end, start + len(begin))
    return content[start:finish]

calcs = (root / 'AfxHookSource/MirvCalcs.cpp').read_text(encoding='utf-8-sig')
hooks = (root / 'AfxHookSource/MaterialSystemHooks.cpp').read_text(encoding='utf-8-sig')
retirement = extract(calcs, '// The render queue only signals completion.', '// Convert once per observed pose')
frame = extract(calcs, 'void Mirv_Tf2_SceneRenderFrame()', 'void Mirv_Tf2_SceneRenderStatus(')
queue = extract(hooks, 'class IMaterialSystemRefCounted', 'bool Hook_MaterialSystem_GetRenderCallQueue(')

preamble = r'''
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <iostream>
#include <utility>

class CMaterialSystemFunctor {
public:
  virtual ~CMaterialSystemFunctor() = default;
  virtual void operator()() = 0;
};
template<typename R, typename... Args>
R SceneInterfaceCall(void *object, size_t slot, Args... args) {
  using Fn = R(*)(void*,Args...);
  return reinterpret_cast<Fn>((*static_cast<void***>(object))[slot])(object,args...);
}
enum SourceSdkVer {
  SourceSdkVer_CSSV34, SourceSdkVer_CSS, SourceSdkVer_L4D2,
  SourceSdkVer_TF2, SourceSdkVer_TF2Classified, SourceSdkVer_HL2MP
};
SourceSdkVer g_SourceSdkVer = SourceSdkVer_HL2MP;
void *g_pMaterialSystem = reinterpret_cast<void*>(1);
using CMaterialSystem_GetRenderCallQueue_t = void*(*)(void*,void*);
class IMaterialSystemRefCounted;
std::vector<IMaterialSystemRefCounted*> queued;
bool emptyRenderQueue = false;
std::array<unsigned char,512> fakeQueueStorage{};
void* FakeGetQueue(void*,void*) {return emptyRenderQueue ? nullptr : fakeQueueStorage.data();}
CMaterialSystem_GetRenderCallQueue_t g_Old_CMaterialSystem_GetRenderCallQueue = FakeGetQueue;
using CMatCallQueue_QueueFunctor_t = void(*)(void*,void*,void*);
void FakeQueue(void*,void*,void*);
std::uintptr_t materialsystem_GetRenderCallQueue = 1;
int materialsystem_CFunctor_vtable_size = 4;
std::uintptr_t materialsystem_CMatCallQueue_QueueFunctor = reinterpret_cast<std::uintptr_t>(&FakeQueue);
#define AFXADDR_GET(name) (name)
'''

postamble = r'''
struct FakeInterface { void **Vtable; unsigned Unlocks=0,Releases=0; };
std::array<void*,42> cacheSlots{};
std::array<void*,14> materialSlots{};
FakeInterface cache{cacheSlots.data()}, material{materialSlots.data()};
unsigned queuedCalls=0, completedCalls=0;
void UnlockStudioHdr(void *object, unsigned short handle) {
  assert(object==&cache && handle==42); ++cache.Unlocks;
}
int ReleaseHandle(void *object, unsigned short handle) {
  assert(object==&cache && handle==42); ++cache.Releases; return 0;
}
void ReleaseMaterial(void *object) {assert(object==&material); ++material.Releases;}

void FakeQueue(void *queueThis,void*,void *ptr) {
  assert(queueThis == fakeQueueStorage.data()+(materialsystem_CFunctor_vtable_size==4 ? 0xa4 : 0));
  auto *callback = static_cast<IMaterialSystemRefCounted*>(ptr);
  // The 3-slot wrapper needs a queue-owned reference. The 4-slot HL2MP
  // submission already carries an additional reference for queue lifetime.
  if (materialsystem_CFunctor_vtable_size == 3) callback->AddRef();
  queued.push_back(callback);
  ++queuedCalls;
}

void Process(bool call=true) {
  std::vector<IMaterialSystemRefCounted*> work;
  work.swap(queued);
  for (auto *callback : work) {
    if (call) {
      if (materialsystem_CFunctor_vtable_size == 4)
        (*static_cast<CMaterialSystemFunctor4*>(callback))();
      else (*static_cast<CMaterialSystemFunctor3*>(callback))();
      ++completedCalls;
    }
    callback->Release();
  }
}
void Enqueue(unsigned n,bool pinMaterial=true) {
  for (unsigned i=0;i<n;++i) {
    assert(g_SceneOutstanding.load() < SceneMaxOutstanding);
    auto *functor = new SceneModelResources(&cache,42);
    functor->State().HeaderLocked=true;
    if (pinMaterial) functor->State().Material=&material;
    functor->Submit();
    assert(MaterialSystem_ExecuteOnRenderThread(functor));
  }
}
int main() {
  cacheSlots[8]=reinterpret_cast<void*>(&ReleaseHandle);
  cacheSlots[41]=reinterpret_cast<void*>(&UnlockStudioHdr);
  materialSlots[13]=reinterpret_cast<void*>(&ReleaseMaterial);
  assert(MaterialSystem_CanExecuteOnRenderThread());

  // The actual HL2MP wrapper delivers the callback; the engine thread retires it.
  Enqueue(2);
  assert(queued.size()==2 && g_SceneOutstanding.load()==2);
  Process(true);
  assert(queued.empty() && g_SceneOutstanding.load()==2);
  Mirv_Tf2_SceneRenderFrame();
  assert(g_SceneOutstanding.load()==0 && cache.Releases==2 && cache.Unlocks==2 && material.Releases==2);

  // Source may discard a queued callback without invoking operator().
  // Its destructor must still signal completion exactly once.
  Enqueue(3);
  Process(false);
  Mirv_Tf2_SceneRenderFrame();
  assert(g_SceneOutstanding.load()==0 && cache.Releases==5 && material.Releases==5);

  // Completion on a worker can overlap retirement on the engine thread.
  Enqueue(128);
  std::thread render([] {Process(true);});
  for (int i=0;i<1000;++i) Mirv_Tf2_SceneRenderFrame();
  render.join();
  Mirv_Tf2_SceneRenderFrame();
  assert(g_SceneOutstanding.load()==0 && cache.Releases==133 && material.Releases==133);

  // If queued objects are not called or destroyed, accounting saturates at
  // 512 and cannot drain. This simulates the symptom, not its OF cause.
  Enqueue(SceneMaxOutstanding);
  assert(queued.size()==SceneMaxOutstanding && g_SceneOutstanding.load()==SceneMaxOutstanding);
  assert(g_SceneOutstandingPeak.load()==SceneMaxOutstanding);
  Mirv_Tf2_SceneRenderFrame();
  assert(g_SceneOutstanding.load()==SceneMaxOutstanding);
  Process(false);
  Mirv_Tf2_SceneRenderFrame();
  assert(g_SceneOutstanding.load()==0 && cache.Releases==645);

  // An empty queue executes and retires the functor synchronously.
  emptyRenderQueue=true;
  Enqueue(1);
  Mirv_Tf2_SceneRenderFrame();
  assert(g_SceneOutstanding.load()==0 && cache.Releases==646);
  emptyRenderQueue=false;

  // The queue helper's failure path destroys the submitted functor, which
  // must still be retired at the next engine-thread drain.
  g_pMaterialSystem=nullptr;
  auto *failed = new SceneModelResources(&cache,42);
  failed->State().HeaderLocked=true;
  failed->Submit();
  assert(!MaterialSystem_ExecuteOnRenderThread(failed));
  assert(g_SceneOutstanding.load()==1);
  Mirv_Tf2_SceneRenderFrame();
  assert(g_SceneOutstanding.load()==0 && cache.Releases==647);
  g_pMaterialSystem=reinterpret_cast<void*>(1);

  // Also verify the independent 3-slot callback wrapper's refcount contract.
  materialsystem_CFunctor_vtable_size=3;
  Enqueue(1);
  Process(true);
  Mirv_Tf2_SceneRenderFrame();
  assert(g_SceneOutstanding.load()==0 && cache.Releases==648);
  std::cout << "Scene replay completion and queue-lifetime regression checks passed: "
            << "processed=" << completedCalls << " queued=" << queuedCalls
            << " retired=" << cache.Releases << " peak=" << g_SceneOutstandingPeak.load() << "\n";
}
'''
source = out / 'scene_render_queue.cpp'
source.write_text(preamble + '\n' + retirement + '\n' + queue + '\n' + frame + '\n' + postamble, encoding='utf-8')
command = ['g++', '-std=c++17', '-pthread', '-O1', '-g', str(source), '-o', str(out/'scene_render_queue')]
if args.sanitize:
    command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
result = subprocess.run(command, capture_output=True, text=True)
(out/'build.log').write_text(result.stdout + result.stderr)
if result.returncode:
    print(result.stderr[-10000:])
    raise SystemExit(result.returncode)
result = subprocess.run([str(out/'scene_render_queue')],capture_output=True,text=True)
print(result.stdout + result.stderr)
if result.returncode:
    raise SystemExit(result.returncode)
