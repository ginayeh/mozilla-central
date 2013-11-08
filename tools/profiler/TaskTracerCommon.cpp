/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set ts=4 sw=4 sts=4 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GeckoTaskTracer.h"
#include "GeckoTaskTracerImpl.h"
#include "jsapi.h"

#include "mozilla/ThreadLocal.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#include "prenv.h"
#include "prthread.h"
#include "ProfileEntry.h"

#include "nsIJSRuntimeService.h"
#include "nsServiceManagerUtils.h"
#include "JSObjectBuilder.h"
#include "nsDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"

#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <ostream>
#include <fstream>

static bool sDebugRunnable = true;
static nsTArray<uint64_t> sLogArray;
static uint32_t sCounterRun = 0;
static uint32_t sCounterSampler = 0;

#ifdef MOZ_WIDGET_GONK
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "Task", args)
#else
#define LOG(args...) do {} while (0)
#endif

#if defined(__GLIBC__)
// glibc doesn't implement gettid(2).
#include <sys/syscall.h>
static pid_t gettid()
{
    return (pid_t) syscall(SYS_gettid);
}
#endif

#define MAX_THREAD_NUM 64

namespace mozilla {
namespace tasktracer {

static TracedInfo sAllTracedInfo[MAX_THREAD_NUM];
static mozilla::ThreadLocal<TracedInfo *> sTracedInfo;
static pthread_mutex_t sTracedInfoLock = PTHREAD_MUTEX_INITIALIZER;
static mozilla::TimeStamp sLogTimer = mozilla::TimeStamp::Now();

static TracedInfo *
AllocTraceInfo(int aTid)
{
    pthread_mutex_lock(&sTracedInfoLock);
    for (int i = 0; i < MAX_THREAD_NUM; i++) {
        if (sAllTracedInfo[i].threadId == 0) {
            TracedInfo *info = sAllTracedInfo + i;
            info->threadId = aTid;
            PRThread *thread = PR_GetCurrentThread();
            pthread_mutex_unlock(&sTracedInfoLock);
            return info;
        }
    }
    NS_ABORT();
    return NULL;
}

static void
_FreeTraceInfo(uint64_t aTid)
{
    pthread_mutex_lock(&sTracedInfoLock);
    for (int i = 0; i < MAX_THREAD_NUM; i++) {
        if (sAllTracedInfo[i].threadId == aTid) {
            TracedInfo *info = sAllTracedInfo + i;
            memset(info, 0, sizeof(TracedInfo));
            break;
        }
    }
    pthread_mutex_unlock(&sTracedInfoLock);
}

void
FreeTracedInfo()
{
    _FreeTraceInfo(gettid());
}

TracedInfo *
GetTracedInfo()
{
    if (!sTracedInfo.get()) {
        sTracedInfo.set(AllocTraceInfo(gettid()));
    }
    return sTracedInfo.get();
}

static const char*
GetCurrentThreadName()
{
    if (gettid() == getpid()) {
        return "main";
    } else if (const char *threadName = PR_GetThreadName(PR_GetCurrentThread())) {
        return threadName;
    } else {
        return "unknown";
    }
}

static bool
WriteCallback(const jschar *buf, uint32_t len, void *data)
{
    std::ofstream& stream = *static_cast<std::ofstream*>(data);
    nsAutoCString profile = NS_ConvertUTF16toUTF8(buf, len);
    stream << profile.Data();
    return true;
}

class SaveTracedInfoTask : public nsRunnable
{
public:
    SaveTracedInfoTask() {}

    NS_IMETHOD
    Run() {
        MOZ_ASSERT(NS_IsMainThread());

        mozilla::TimeStamp now = mozilla::TimeStamp::Now();
        mozilla::TimeDuration diff = now - sLogTimer;
        sLogTimer = now;
//        nsCString tmpPath;
//        tmpPath.AppendPrintf("/sdcard/tast_tracer_profile.txt");

        nsCOMPtr<nsIFile> tmpFile;
        nsAutoCString tmpPath;
        if (NS_FAILED(NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(tmpFile)))) {
            LOG("Failed to find temporary directory.");
            return NS_ERROR_FAILURE;
        }
        tmpPath.AppendPrintf("task_tracer_data.txt");

        nsresult rv = tmpFile->AppendNative(tmpPath);
        if (NS_FAILED(rv))
            return rv;

        rv = tmpFile->GetNativePath(tmpPath);
        if (NS_FAILED(rv))
            return rv;

        JSRuntime *rt;
        JSContext *cx;
        nsCOMPtr<nsIJSRuntimeService> rtsvc
          = do_GetService("@mozilla.org/js/xpc/RuntimeService;1");
        if (!rtsvc || NS_FAILED(rtsvc->GetRuntime(&rt)) || !rt) {
            LOG("failed to get RuntimeService");
            return NS_ERROR_FAILURE;;
        }

        cx = JS_NewContext(rt, 8192);
        if (!cx) {
            LOG("Failed to get context");
            return NS_ERROR_FAILURE;
        }

        {
            JSAutoRequest ar(cx);
            static const JSClass c = {
                "global", JSCLASS_GLOBAL_FLAGS,
                JS_PropertyStub, JS_DeletePropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
                JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub
            };
            JSObject *obj = JS_NewGlobalObject(cx, &c, NULL, JS::FireOnNewGlobalHook);

            std::ofstream stream;
            stream.open(tmpPath.get());
            if (stream.is_open()) {
                JSAutoCompartment autoComp(cx, obj);

                JSObjectBuilder b(cx);
                JS::RootedObject profile(cx, b.CreateObject());

                LOG("%d, %d, %d, %f", sLogArray.Length(), sCounterRun, sCounterSampler, diff.ToSeconds());
                b.DefineProperty(profile, "number", (int)sLogArray.Length());

                JSObjectBuilder::RootedArray data(b.context(), b.CreateArray());
                b.DefineProperty(profile, "data", data);

                for (uint32_t i = 0; i < sLogArray.Length(); i++) {
                    JSObjectBuilder::RootedObject runnable(b.context(), b.CreateObject());
                    b.DefineProperty(runnable, "originTaskId", (int)sLogArray[i]);
                    b.ArrayPush(data, runnable);
                }

                JS::Rooted<JS::Value> val(cx, OBJECT_TO_JSVAL(profile));
                JS_Stringify(cx, &val, JS::NullPtr(), JS::NullHandleValue, WriteCallback, &stream);
                stream.close();
//                LOGF("Saved to %s", tmpPath.get());

                sLogArray.Clear();
                sCounterRun = 0;
                sCounterSampler = 0;
            } else {
//                LOG("Fail to open profile log file.");
            }
        }
        JS_DestroyContext(cx);

        return NS_OK;
    }
};

void
LogAction(ActionType aType, uint64_t aTid, uint64_t aOTid)
{
    TracedInfo *info = GetTracedInfo();

//    if (sDebugRunnable && aOTid) {
    if (sDebugRunnable) {
//        LOG("(tid: [%d] %d (%s)), task: %lld, orig: %lld", sLogArray.Length(), gettid(), GetCurrentThreadName(), aTid, aOTid);
        sCounterRun++;
        sLogArray.AppendElement(aOTid);
        if (sLogArray.Length() == 2000) {
            nsCOMPtr<nsIRunnable> runnable = new SaveTracedInfoTask();
            NS_DispatchToMainThread(runnable);
        }
    }

    TracedActivity *activity = info->activities + info->actNext;
    info->actNext = (info->actNext + 1) % TASK_TRACE_BUF_SIZE;
    activity->actionType = aType;
    activity->tm = (uint64_t)JS_Now();
    activity->taskId = aTid;
    activity->originTaskId = aOTid;

#if 0 // Not fully implemented yet.
    // Fill TracedActivity to ProfileEntry
    ProfileEntry entry('a', activity);
    ThreadProfile *threadProfile = nullptr; // TODO get thread profile.
    threadProfile->addTag(entry);
#endif
}

void
InitRunnableTrace()
{
    // This will be called during startup.
    //MOZ_ASSERT(NS_IsMainThread());
    if (!sTracedInfo.initialized()) {
        sTracedInfo.init();
    }

    if (PR_GetEnv("MOZ_DEBUG_RUNNABLE")) {
        sDebugRunnable = true;
    }
}

void
LogSamplerEnter(const char *aInfo)
{
    if (uint64_t currTid = *GetCurrentThreadTaskIdPtr() && sDebugRunnable) {
        // FIXME
        sLogArray.AppendElement(0);
        sCounterSampler++;
        if (sLogArray.Length() == 2000) {
            nsCOMPtr<nsIRunnable> runnable = new SaveTracedInfoTask();
            NS_DispatchToMainThread(runnable);
        }
//        LOG("(tid: %d), task: %lld, >> %s", gettid(), currTid, aInfo);
    }
}

void
LogSamplerExit(const char *aInfo)
{
    if (uint64_t currTid = *GetCurrentThreadTaskIdPtr() && sDebugRunnable) {
//        LOG("(tid: %d), task: %lld, << %s", gettid(), currTid, aInfo);
        // FIXME
        sLogArray.AppendElement(0);
        if (sLogArray.Length() == 2000) {
            nsCOMPtr<nsIRunnable> runnable = new SaveTracedInfoTask();
            NS_DispatchToMainThread(runnable);
        }
    }
}

uint64_t *
GetCurrentThreadTaskIdPtr()
{
    TracedInfo *info = GetTracedInfo();
    return &info->currentTracedTaskId;
}

uint64_t
GenNewUniqueTaskId()
{
    pid_t tid = gettid();
    uint64_t taskid =
        ((uint64_t)tid << 32) | ++GetTracedInfo()->lastUniqueTaskId;
    return taskid;
}

void ClearTracedInfo() {
  *GetCurrentThreadTaskIdPtr() = 0;
}

} // namespace tasktracer
} // namespace mozilla
