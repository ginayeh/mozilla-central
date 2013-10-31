/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set ts=4 sw=4 sts=4 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GeckoTaskTracer.h"
#include "GeckoTaskTracerImpl.h"
#include "jsapi.h"
#include "SaveProfileTask.h"

#include "mozilla/ThreadLocal.h"
#include "nsThreadUtils.h"
#include "prenv.h"
#include "prthread.h"
#include "ProfileEntry.h"

#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>

static bool sDebugRunnable = true;
uint64_t sCounter = 0;

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
static mozilla::ThreadLocal<ThreadProfile *> sThreadProfile;
static pthread_mutex_t sTracedInfoLock = PTHREAD_MUTEX_INITIALIZER;

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

ThreadProfile *
GetThreadProfile()
{
    if (!sThreadProfile.initialized()) {
        sThreadProfile.init();
    }

    if (!sThreadProfile.get()) {
        // Get thread profile and keep it in sThreadProfile
        std::vector<ThreadInfo*> threads = Sampler::GetRegisteredThreads();
        for (uint32_t i = 0; i < threads.size(); i++) {
            ThreadInfo* info = threads[i];
            if (info->Profile() && (gettid() == info->ThreadId())) {
                sThreadProfile.set(info->Profile());
                LOG("sThreadProfile now is pointed to [%p]", sThreadProfile.get());
                break;
            }
        }
    }
    return sThreadProfile.get();
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

void
LogAction(ActionType aType, uint64_t aTid, uint64_t aOTid)
{
    TracedInfo *info = GetTracedInfo();

/*    if (sDebugRunnable && aOTid) {
        LOG("(tid: %d (%s)), task: %lld, orig: %lld", gettid(), GetCurrentThreadName(), aTid, aOTid);
    }*/

    TracedActivity *activity = info->activities + info->actNext;
    info->actNext = (info->actNext + 1) % TASK_TRACE_BUF_SIZE;
    activity->actionType = aType;
    activity->tm = (uint64_t)JS_Now();
    activity->taskId = aTid;
    activity->originTaskId = aOTid;

    ProfileEntry entry('n', (int)activity->originTaskId);
    ThreadProfile* profile = GetThreadProfile();
    if (profile) {
      sCounter++;
//      LOG("[%lld] add tag %d to tid: %d", sCounter, (int)activity->originTaskId, gettid());
      profile->addTag(entry);
    } else {
//      LOG("no profile, tid: %d", gettid());
    }
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
/*    if (uint64_t currTid = *GetCurrentThreadTaskIdPtr() && sDebugRunnable) {
        LOG("(tid: %d), task: %lld, >> %s", gettid(), *GetCurrentThreadTaskIdPtr(), aInfo);
    }*/
}

void
LogSamplerExit(const char *aInfo)
{
/*    if (uint64_t currTid = *GetCurrentThreadTaskIdPtr() && sDebugRunnable) {
        LOG("(tid: %d), task: %lld, << %s", gettid(), *GetCurrentThreadTaskIdPtr(), aInfo);
    }*/
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
