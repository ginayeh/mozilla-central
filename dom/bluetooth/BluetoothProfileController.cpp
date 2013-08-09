/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BluetoothProfileController.h"

#include "BluetoothA2dpManager.h"
#include "BluetoothHfpManager.h"
#include "BluetoothOppManager.h"

USING_BLUETOOTH_NAMESPACE

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "GonkDBus", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

BluetoothProfileController::BluetoothProfileController(
                                              uint32_t aCod,
                                              BluetoothReplyRunnable* aRunnable)
{

}

BluetoothProfileController::BluetoothProfileController(
                                              BluetoothServiceClass aClass,
                                              BluetoothReplyRunnable* aRunnable)
{
  MOZ_ASSERT(aRunnable);

  BluetoothProfileManagerBase* profile;
  switch (aClass) {
    case BluetoothServiceClass::HANDSFREE:
    case BluetoothServiceClass::HEADSET:
      profile = BluetoothHfpManager::Get();
      break;
    case BluetoothServiceClass::A2DP:
      profile = BluetoothA2dpManager::Get();
      break;
    case BluetoothServiceClass::OBJECT_PUSH:
      profile = BluetoothOppManager::Get();
      break;
  }

  if (profile) {
    mProfiles.AppendElement(profile);
    mRunnable = aRunnable;
    mProfilesIndex = 0;
  }
}

void
BluetoothProfileController::Connect()
{
  MOZ_ASSERT(mProfilesIndex < mProfiles.Length());

  ConnectNext();
}

void
BluetoothProfileController::ConnectNext()
{
  if (mProfilesIndex < mProfiles.Length()) {
    mProfiles[mProfilesIndex]->Connect();
  } else {
    // All profiles has been tried
  }
}

void
BluetoothProfileController::OnConnectCallback()
{
  mProfilesIndex++;
  ConnectNext();
}

void
BluetoothProfileController::Disconnect()
{

}

void
BluetoothProfileController::DisconnectNext()
{
  if (mProfilesIndex < mProfiles.Length()) {
    mProfiles[mProfilesIndex]->Disconnect();
  } else {
  
  }
}

void
BluetoothProfileController::OnDisconnectCallback()
  DisconnectNext();
}

void
BluetoothProfileController::SetErrorString(const char* aErrorString)
{
  mErrorString.AssignLiteral(aErrorString);
}
