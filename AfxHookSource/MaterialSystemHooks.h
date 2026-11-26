#pragma once

class CMaterialSystemFunctor abstract {
public:
    virtual ~CMaterialSystemFunctor() {};
    virtual void operator()() = 0;
};

// Query before submitting engine work that needs an ordered cleanup callback.
bool MaterialSystem_CanExecuteOnRenderThread();
// True only when a verified material system has no deferred render queue.
bool MaterialSystem_IsImmediate();

bool MaterialSystem_ExecuteOnRenderThread(CMaterialSystemFunctor * functor);

bool Hook_MaterialSystem_GetRenderCallQueue(void);
bool Hook_MaterialSystem_SwapBuffers(void);
bool Hook_MaterialSystem(void);
