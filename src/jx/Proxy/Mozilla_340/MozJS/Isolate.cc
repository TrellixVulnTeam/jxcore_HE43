// Copyright & License details are available under JXCORE_LICENSE file

#include "Isolate.h"
#include "assert.h"
#include "../EngineHelper.h"
#include "../../../commons.h"

namespace MozJS {

void GCOnMozJS(JSRuntime* rt, JSGCStatus status, void* data) {
#ifdef JXCORE_PRINT_NATIVE_CALLS
  // TODO(obastemur) GC is happening so?
  if (status == JSGC_BEGIN) {
    ENGINE_LOG_THIS("MozJS", "GC_BEGIN");
  } else if (status == JSGC_END) {
    ENGINE_LOG_THIS("MozJS", "GC_END");
  } else {
    error_console("Unknown GC flag. Did you forget updating _GCOnMozJS ?\n");
    abort();
  }
#endif
}

bool JSEngineInterrupt(JSContext* ctx) {
  int tid = JS_GetThreadId(ctx);
  node::commons* com = node::commons::getInstanceByThreadId(tid);
  if (com != NULL && com->should_interrupt_) {
    ENGINE_LOG_THIS("MozJS", "JSEngineInterrupt - API Call");
    com->should_interrupt_ = false;
    return false;
  }

  ENGINE_LOG_THIS("MozJS", "JSEngineInterrupt - 100% CPU");

  // TODO(obastemur) This also triggers when CPU hits 100%. do anything ?
  return true;
}

// Dummy ShellPrincipals from SM Shell
uint32_t ShellPrincipals::getBits(JSPrincipals* p) {
  if (!p) return 0xffff;
  return static_cast<ShellPrincipals*>(p)->bits;
}

void ShellPrincipals::destroy(JSPrincipals* principals) {
  js_free(static_cast<ShellPrincipals*>(principals));
}

bool ShellPrincipals::subsumes(JSPrincipals* first,
                                      JSPrincipals* second) {
  uint32_t firstBits = getBits(first);
  uint32_t secondBits = getBits(second);
  return (firstBits | secondBits) == firstBits;
}

JSSecurityCallbacks ShellPrincipals::securityCallbacks = {
    nullptr,  // contentSecurityPolicyAllows
    subsumes};

// The fully-trusted principal subsumes all other principals.
ShellPrincipals ShellPrincipals::fullyTrusted(-1, 1);

#define JXCORE_MAX_THREAD_COUNT 65  // +1 for main thread
Isolate* Isolates[JXCORE_MAX_THREAD_COUNT] = {NULL};
JSRuntime* runtimes[JXCORE_MAX_THREAD_COUNT] = {NULL};

static int threadIds[65] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                            13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
                            26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
                            39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,
                            52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64};

int* getThreadIdsMemRef() { return threadIds; }

Isolate::Isolate() {
  ctx_ = NULL;
  threadId_ = -1;
  disposable_ = false;
}

Isolate::Isolate(JSContext* ctx, int threadId, bool disposable) {
  ctx_ = ctx;
  threadId_ = threadId;
  disposable_ = disposable;
}

int GetThreadId() { return EngineHelper::GetThreadId(); }

Isolate* Isolate::GetCurrent() { return Isolates[GetThreadId()]; }

Isolate* Isolate::New(int threadId) {
  const bool for_thread = threadId != -1;
  JSRuntime* rt;

  if (for_thread) {
    if (runtimes[threadId] != NULL) {
      // Something is wrong! return NULL
      return NULL;
    }

    if (threadId == 0)
      runtimes[threadId] =
          JS_NewRuntime(JS::DefaultHeapMaxBytes, JS::DefaultNurseryBytes);
    else
      runtimes[threadId] = JS_NewRuntime(JS::DefaultHeapMaxBytes,
                                         2L * 1024L * 1024L, runtimes[0]);

    rt = runtimes[threadId];
    assert(rt != NULL && "couldn't initialize a new runtime. Out of memory?");

    JS_SetRuntimePrivate(rt, &threadIds[threadId]);

    JS_SetGCParameter(rt, JSGC_MODE, JSGC_MODE_INCREMENTAL);
    JS_SetGCParameter(rt, JSGC_MAX_BYTES, 0xffffffff);
    // 256 * ... value seems like only applicable for DEBUG builds
    JS_SetNativeStackQuota(rt, 128 * sizeof(size_t) * 1024);
    JS_SetDefaultLocale(rt, "UTF-8");

    JS_SetTrustedPrincipals(rt, &ShellPrincipals::fullyTrusted);
    JS_SetSecurityCallbacks(rt, &ShellPrincipals::securityCallbacks);
    JS_InitDestroyPrincipalsCallback(rt, ShellPrincipals::destroy);

#if defined(DEBUG) && !defined(__POSIX__)
// _WIN32
// TODO(obastemur) investigate how to debug JIT SM on Win
#else
    JS::RuntimeOptionsRef(rt)
        .setBaseline(true)
        .setIon(true)
        .setAsmJS(true)
        .setNativeRegExp(true);
#endif

    JS_SetInterruptCallback(rt, JSEngineInterrupt);
    JS_SetGCCallback(rt, GCOnMozJS, NULL);
  } else {
    threadId = GetThreadId();
    rt = runtimes[threadId];
  }

  JSContext* ctx = JS_NewContext(rt, 32 * 1024);

  assert(ctx != NULL && "couldn't initialize a new context. Out of memory?");
  JS_SetThreadId(ctx, threadId);

  if (for_thread) {
    Isolates[threadId] = new Isolate(ctx, threadId, true);
    return Isolates[threadId];
  } else {
    // TODO(obastemur) replace this to auto_ptr kind of implementation
    // to prevent mistakes(leak)
    return new Isolate(ctx, threadId, false);
  }
}

Isolate* Isolate::GetByThreadId(const int threadId) {
  assert(threadId > -1 && threadId < JXCORE_MAX_THREAD_COUNT);
  return Isolates[threadId];
}

void* Isolate::GetData() { return JS_GetContextPrivate(ctx_); }

void Isolate::SetData(void* data) { return JS_SetContextPrivate(ctx_, data); }

void Isolate::Dispose() {
  int tid = threadId_;
  if (disposable_) {
    if (Isolates[tid] != NULL) {
      delete Isolates[tid];
    }
    Isolates[tid] = NULL;
    runtimes[tid] = NULL;
  }
}
}  // namespace MozJS
