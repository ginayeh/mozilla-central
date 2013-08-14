/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BluetoothProfileController.h"

#include "BluetoothA2dpManager.h"
#include "BluetoothHfpManager.h"
#include "BluetoothOppManager.h"

#include "BluetoothUtils.h"
#include "mozilla/dom/bluetooth/BluetoothTypes.h"

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
                                              const nsAString& aDeviceAddress,
                                              uint32_t aCod,
                                              BluetoothReplyRunnable* aRunnable)
{
  LOG("[C] %s", __FUNCTION__);
  MOZ_ASSERT(aRunnable);

  mProfilesIndex = 0;
  bool hasAudio = BluetoothCodHelper::HasAudio(aCod);
  bool hasObjectTransfer = BluetoothCodHelper::HasObjectTransfer(aCod);
  bool hasRendering = BluetoothCodHelper::HasRendering(aCod);

  if (!hasAudio && !hasObjectTransfer && !hasRendering) {
    return;
  }

  mProfilesIndex = 0;
  mRunnable = aRunnable;
  mCod = aCod;
  mDeviceAddress = aDeviceAddress;

  /**
   * We connect HFP/HSP first. Then, connect A2DP if Rendering bit is set.
   * It's almost impossible to send file to a remote device which is an Audio
   * device or a Rendering device, so we won't connect OPP in that case.
   */
  if (hasAudio) {
    mProfiles.AppendElement(BluetoothHfpManager::Get());
  }
  if (hasRendering) {
    mProfiles.AppendElement(BluetoothA2dpManager::Get());
  }
  if (hasObjectTransfer && !hasAudio && !hasRendering) {
    mProfiles.AppendElement(BluetoothOppManager::Get());
  }
}

BluetoothProfileController::BluetoothProfileController(
                                              const nsAString& aDeviceAddress,
                                              BluetoothServiceClass aClass,
                                              BluetoothReplyRunnable* aRunnable)
{
  LOG("[C] %s", __FUNCTION__);

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
    mProfilesIndex = 0;
    mRunnable = aRunnable;
    mProfiles.AppendElement(profile);
    mDeviceAddress = aDeviceAddress;
  }
}

void
BluetoothProfileController::Connect()
{
  LOG("[C] %s", __FUNCTION__);
  mProfilesIndex = 0;
  ConnectNext();
}

void
BluetoothProfileController::ConnectNext()
{
  LOG("[C] %s", __FUNCTION__);
  MOZ_ASSERT(!mDeviceAddress.IsEmpty());

  if (mProfilesIndex < mProfiles.Length()) {
    mProfiles[mProfilesIndex]->Connect(mDeviceAddress, this);
  } else {
    LOG("[C] all profiles connect complete.");
    // All profiles has been tried
    mDeviceAddress.Truncate();
    mProfilesIndex = -1;
    mProfiles.Clear();

    DispatchBluetoothReply(mRunnable, BluetoothValue(true), EmptyString());
  }
}

void
BluetoothProfileController::OnConnectReply()
{
  LOG("[C] %s", __FUNCTION__);
  mProfilesIndex++;
  ConnectNext();
}

void
BluetoothProfileController::Disconnect()
{
  LOG("[C] %s", __FUNCTION__);
  mProfilesIndex = 0;
  DisconnectNext();
}

void
BluetoothProfileController::DisconnectNext()
{
  LOG("[C] %s", __FUNCTION__);
  if (mProfilesIndex < mProfiles.Length()) {
    mProfiles[mProfilesIndex]->Disconnect(this);
  } else {
    mDeviceAddress.Truncate();
    mProfilesIndex = -1;
    mProfiles.Clear();
  }
}

void
BluetoothProfileController::OnDisconnectReply()
{
  LOG("[C] %s", __FUNCTION__);
  mProfilesIndex++;
  DisconnectNext();
}

uint32_t
BluetoothProfileController::GetCod()
{
  LOG("[C] %s", __FUNCTION__);
  return mCod;
}
