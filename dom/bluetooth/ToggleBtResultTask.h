/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_togglebtresulttask_h__
#define mozilla_dom_bluetooth_togglebtresulttask_h__

//#include "BluetoothCommon.h"
//#include "nsThreadUtils.h"
//#include "jsapi.h"
#include "BluetoothManager.h"

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "GonkDBus", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

BEGIN_BLUETOOTH_NAMESPACE

class ToggleBtResultTask : public nsRunnable
{
public:
  ToggleBtResultTask(BluetoothManager* aManager, bool aEnabled)
    : mManagerPtr(aManager),
      mEnabled(aEnabled),
      mResult(false)
  {
  }

  void SetReply(bool aResult)
  {
    LOG("### ToggleBtResultTask::SetReply(), %d", aResult);
    mResult = aResult;
  }

  NS_IMETHOD Run()
  {
    LOG("### ToggleBtResultTask::Run()");
    MOZ_ASSERT(NS_IsMainThread());

    mManagerPtr->SetEnabledInternal(mEnabled);
    mManagerPtr->FireEnabledDisabledEvent(mResult);

    // mManagerPtr must be null before returning to prevent the background
    // thread from racing to release it during the destruction of this runnable.
    mManagerPtr = nullptr;

    return NS_OK;
  }

private:
  nsRefPtr<BluetoothManager> mManagerPtr;
  bool mEnabled;
  bool mResult;
};
END_BLUETOOTH_NAMESPACE

#endif
