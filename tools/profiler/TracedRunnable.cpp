/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TracedRunnable.h"

namespace mozilla {
namespace tasktracer {

// Implementing TracedRunnable.h

NS_IMPL_ISUPPORTS1(TracedRunnable, nsIRunnable)

NS_IMETHODIMP
TracedRunnable::Run()
{
//    LogAction(ACTION_START, mTaskId, mOriginTaskId);

    SetupTracedInfo();
    nsresult rv = mFactualObj->Run();
    ClearTracedInfo();

    LogAction(ACTION_FINISHED, mTaskId, mOriginTaskId);
    return rv;
}

TracedRunnable::TracedRunnable(nsIRunnable *aFatualObj)
    : mOriginTaskId(0)
    , mFactualObj(aFatualObj)
{
    mTaskId = GenNewUniqueTaskId();
}

TracedRunnable::~TracedRunnable()
{
}

void
TracedRunnable::InitOriginTaskId()
{
    uint64_t origintaskid = *GetCurrentThreadTaskIdPtr();
    SetOriginTaskId(origintaskid);

//    LogAction(ACTION_DISPATCH, mTaskId, mOriginTaskId);
}

TracedTask::~TracedTask()
{
  if (mBackedObject) {
    delete mBackedObject;
    mBackedObject = nullptr;
  }
}

void
TracedTask::InitOriginTaskId()
{
  uint64_t originTaskId = *GetCurrentThreadTaskIdPtr();
//    if (originTaskId == 0) {
//        originTaskId = mTaskId;
//    }
  mOriginTaskId = originTaskId;

//  LogAction(ACTION_DISPATCH, mTaskId, mOriginTaskId);
}

void
TracedTask::Run()
{
//  LogAction(ACTION_START, mTaskId, mOriginTaskId);

  SetupTracedInfo();
  mBackedObject->Run();
  ClearTracedInfo();

  LogAction(ACTION_FINISHED, mTaskId, mOriginTaskId);
}

// Implementing GeckoTaskTracer.h

nsIRunnable *
CreateTracedRunnable(nsIRunnable *aRunnable)
{
  TracedRunnable *runnable = new TracedRunnable(aRunnable);
  runnable->InitOriginTaskId();
  return runnable;
}

Task *
CreateTracedTask(Task *aTask)
{
  TracedTask *task = new TracedTask(aTask);
  task->InitOriginTaskId();
  return task;
}

} // namespace tasktracer
} // namespace mozilla
