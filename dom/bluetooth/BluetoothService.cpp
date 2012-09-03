/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"

#include "BluetoothService.h"
#include "BluetoothTypes.h"
#include "BluetoothReplyRunnable.h"
#include "GeneratedEvents.h"

#include "nsIDOMDOMRequest.h"
#include "nsDOMEventTargetHelper.h"
#include "nsThreadUtils.h"
#include "nsXPCOMCIDInternal.h"
#include "nsObserverService.h"
#include "nsIDOMBluetoothResultEvent.h"
#include "mozilla/Services.h"
#include "mozilla/LazyIdleThread.h"
#include "mozilla/Util.h"

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "GonkDBus", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

using namespace mozilla;

USING_BLUETOOTH_NAMESPACE

static nsRefPtr<BluetoothService> gBluetoothService;
static bool gInShutdown = false;

NS_IMPL_ISUPPORTS1(BluetoothService, nsIObserver)

class ToggleBtAck : public nsRunnable
{
public:
  ToggleBtAck(bool aEnabled) : mEnabled(aEnabled)
  {
  }
  
  NS_IMETHOD Run()
  {
    LOG("### ToggleBtAck::Run()");
    MOZ_ASSERT(NS_IsMainThread());

    if (!mEnabled || gInShutdown) {
      if (gBluetoothService->mBluetoothCommandThread) {
        nsCOMPtr<nsIThread> t;
        gBluetoothService->mBluetoothCommandThread.swap(t);
        t->Shutdown();
      }
    }
    
    if (gInShutdown) {
      gBluetoothService = nullptr;
    }

    return NS_OK;
  }

private:
  bool mEnabled;
};

class ToggleBtTask : public nsRunnable
{
public:
  ToggleBtTask(bool aEnabled,
               nsIRunnable* aRunnable,
               bool* aResult)
    : mEnabled(aEnabled),
      mRunnable(aRunnable),
      mResult(aResult)
  {
    MOZ_ASSERT(NS_IsMainThread());
    LOG("### ToggleBtTask created, mResult = %d[%p]", *aResult, aResult);
  }

  NS_IMETHOD Run() 
  {
    LOG("### ToggleBtTask::Run(), mResult = %d[%p]", *mResult, mResult);
    MOZ_ASSERT(!NS_IsMainThread());

    nsString replyError;
    if (mEnabled) {
      if (NS_FAILED(gBluetoothService->StartInternal())) {
        replyError.AssignLiteral("Bluetooth service not available - We should never reach this point!");
        *mResult = false;
      } else {
        *mResult = true;
      }
    }
    else {
      if (NS_FAILED(gBluetoothService->StopInternal())) {        
        replyError.AssignLiteral("Bluetooth service not available - We should never reach this point!");
        *mResult = false;
      } else {
        *mResult = true;
      }
    }
    LOG("### replyError, mResult = %d[%p]", *mResult, mResult);

    // Always has to be called since this is where we take care of our reference
    // count for runnables. If there's an error, replyError won't be empty, so
    // consider our status flipped.
    nsCOMPtr<nsIRunnable> ackTask = new ToggleBtAck(mEnabled);
    if (NS_FAILED(NS_DispatchToMainThread(ackTask))) {
      NS_WARNING("Failed to dispatch to main thread!");
    }
    
    if (!mRunnable) {
      return NS_OK;
    }

    if (NS_FAILED(NS_DispatchToMainThread(mRunnable))) {
      NS_WARNING("Failed to dispatch to main thread!");
    }
    
    return NS_OK;
  }

private:
  bool mEnabled;
  nsCOMPtr<nsIRunnable> mRunnable;
  bool* mResult;
};

nsresult
BluetoothService::RegisterBluetoothSignalHandler(const nsAString& aNodeName,
                                                 BluetoothSignalObserver* aHandler)
{
  MOZ_ASSERT(NS_IsMainThread());
  BluetoothSignalObserverList* ol;
  if (!mBluetoothSignalObserverTable.Get(aNodeName, &ol)) {
    ol = new BluetoothSignalObserverList();
    mBluetoothSignalObserverTable.Put(aNodeName, ol);
  }
  ol->AddObserver(aHandler);
  return NS_OK;
}

nsresult
BluetoothService::UnregisterBluetoothSignalHandler(const nsAString& aNodeName,
                                                   BluetoothSignalObserver* aHandler)
{
  MOZ_ASSERT(NS_IsMainThread());
  BluetoothSignalObserverList* ol;
  if (!mBluetoothSignalObserverTable.Get(aNodeName, &ol)) {
    NS_WARNING("Node does not exist to remove BluetoothSignalListener from!");
    return NS_OK;
  }
  ol->RemoveObserver(aHandler);
  if (ol->Length() == 0) {
    mBluetoothSignalObserverTable.Remove(aNodeName);
  }
  return NS_OK;
}

nsresult
BluetoothService::DistributeSignal(const BluetoothSignal& signal)
{
  MOZ_ASSERT(NS_IsMainThread());
  // Notify observers that a message has been sent
  BluetoothSignalObserverList* ol;
  if (!mBluetoothSignalObserverTable.Get(signal.path(), &ol)) {
    return NS_OK;
  }
  ol->Broadcast(signal);
  return NS_OK;
}

nsresult
BluetoothService::StartStopBluetooth(nsIRunnable* aResultRunnable, bool aStart, bool* aResult)
{
  LOG("### StartStopBluetooth, aResult = %d[%p]", *aResult, aResult);
  MOZ_ASSERT(NS_IsMainThread());

  // If we're shutting down, bail early.
  if (gInShutdown && aStart) {
    NS_ERROR("Start called while in shutdown!");
    return NS_ERROR_FAILURE;
  }
  if (!mBluetoothCommandThread) {
    nsresult rv = NS_NewNamedThread("BluetoothCmd",
                                    getter_AddRefs(mBluetoothCommandThread));
    NS_ENSURE_SUCCESS(rv, rv);
  }
  nsCOMPtr<nsIRunnable> r = new ToggleBtTask(aStart, aResultRunnable, aResult);
  if (NS_FAILED(mBluetoothCommandThread->Dispatch(r, NS_DISPATCH_NORMAL))) {
    NS_WARNING("Cannot dispatch firmware loading task!");
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

nsresult
BluetoothService::Start(nsIRunnable* aResultRunnable, bool* aResult)
{
  return StartStopBluetooth(aResultRunnable, true, aResult);
}

nsresult
BluetoothService::Stop(nsIRunnable* aResultRunnable, bool* aResult)
{
  return StartStopBluetooth(aResultRunnable, false, aResult);
}

// static
BluetoothService*
BluetoothService::Get()
{
  MOZ_ASSERT(NS_IsMainThread());

  // If we already exist, exit early
  if (gBluetoothService) {
    return gBluetoothService;
  }
  
  // If we're in shutdown, don't create a new instance
  if (gInShutdown) {
    NS_WARNING("BluetoothService returns null during shutdown");
    return nullptr;
  }

  // Create new instance, register, return
  gBluetoothService = BluetoothService::Create();
  if (!gBluetoothService) {
    NS_WARNING("Cannot create bluetooth service!");
    return nullptr;
  }
  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  MOZ_ASSERT(obs);

  if (NS_FAILED(obs->AddObserver(gBluetoothService, "xpcom-shutdown", false))) {
    NS_ERROR("AddObserver failed!");
  }
  return gBluetoothService;
}

nsresult
BluetoothService::Observe(nsISupports* aSubject, const char* aTopic,
                          const PRUnichar* aData)
{
  NS_ASSERTION(!strcmp(aTopic, "xpcom-shutdown"),
               "BluetoothService got unexpected topic!");
  gInShutdown = true;
  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (obs && NS_FAILED(obs->RemoveObserver(this, "xpcom-shutdown"))) {
    NS_WARNING("Can't unregister bluetooth service with xpcom shutdown!");
  }

  // XXX
  bool result = false;
  return Stop(nullptr, &result);
}
