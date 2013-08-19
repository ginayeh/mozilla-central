/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BluetoothProfileController.h"

#include "BluetoothA2dpManager.h"
#include "BluetoothHfpManager.h"
#include "BluetoothHidManager.h"
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
                                   BluetoothServiceClass aClass,
                                   BluetoothReplyRunnable* aRunnable,
                                   BluetoothProfileControllerCallback aCallback)
{
  LOG("[C] %s", __FUNCTION__);

  // Put the specific profile into array
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
    case BluetoothServiceClass::HID:
      profile = BluetoothHidManager::Get();
      break;
  }

  NS_ENSURE_TRUE_VOID(profile);
  Init(aDeviceAddress, aRunnable, aCallback);
  mProfiles.AppendElement(profile);
}

BluetoothProfileController::BluetoothProfileController(
                                   const nsAString& aDeviceAddress,
                                   uint32_t aCod,
                                   BluetoothReplyRunnable* aRunnable,
                                   BluetoothProfileControllerCallback aCallback)
{
  LOG("[C] %s", __FUNCTION__);

  // Put multiple profiles into array and connect to all of them sequencely
  bool hasAudio = HAS_AUDIO(aCod);
  bool hasObjectTransfer = HAS_OBJECT_TRANSFER(aCod);
  bool hasRendering = HAS_RENDERING(aCod);
  bool isPeripheral = IS_PERIPHERAL(aCod);

  NS_ENSURE_FALSE_VOID(!hasAudio && !hasObjectTransfer &&
                       !hasRendering && !isPeripheral);

  mCod = aCod;
  Init(aDeviceAddress, aRunnable, aCallback);

  /**
   * Connect to HFP/HSP first. Then, connect A2DP if Rendering bit is set.
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
  if (isPeripheral) {
    mProfiles.AppendElement(BluetoothHidManager::Get());
  }
}

BluetoothProfileController::BluetoothProfileController(
                                   const nsAString& aDeviceAddress,
                                   BluetoothReplyRunnable* aRunnable,
                                   BluetoothProfileControllerCallback aCallback)
{
  LOG("[C] %s", __FUNCTION__);

  Init(aDeviceAddress, aRunnable, aCallback);

  // Put all connected profiles into array and disconnect all of them
  BluetoothProfileManagerBase* profile;
  profile = BluetoothHfpManager::Get();
  if (profile->IsConnected()) {
    mProfiles.AppendElement(BluetoothHfpManager::Get());
  }
  profile = BluetoothOppManager::Get();
  if (profile->IsConnected()) {
    mProfiles.AppendElement(BluetoothOppManager::Get());
  }
  profile = BluetoothHidManager::Get();
  if (profile->IsConnected()) {
    mProfiles.AppendElement(BluetoothHidManager::Get());
  }
  profile = BluetoothA2dpManager::Get();
  if (profile->IsConnected()) {
    mProfiles.AppendElement(BluetoothA2dpManager::Get());
  }
}

BluetoothProfileController::~BluetoothProfileController()
{
  LOG("[C] %s", __FUNCTION__);
  mProfilesIndex = -1;
  mProfiles.Clear();
  mRunnable = nullptr;
  mCallback = nullptr;
}

void
BluetoothProfileController::Init(const nsAString& aDeviceAddress,
                                 BluetoothReplyRunnable* aRunnable,
                                 BluetoothProfileControllerCallback aCallback)
{
  LOG("[C] %s", __FUNCTION__);
  MOZ_ASSERT(!aDeviceAddress.IsEmpty());
  MOZ_ASSERT(aRunnable);
  MOZ_ASSERT(aCallback);

  mDeviceAddress = aDeviceAddress;
  mRunnable = aRunnable;
  mCallback = aCallback;
  mProfiles.Clear();
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

  if (mProfilesIndex < mProfiles.Length()) {
    MOZ_ASSERT(!mDeviceAddress.IsEmpty());

    mProfiles[mProfilesIndex]->Connect(mDeviceAddress, this);
    return;
  }

  LOG("[C] all profiles connect complete.");
  MOZ_ASSERT(mRunnable && mCallback);
  // The action has been completed, so the dom request is replied and then
  // the callback is invoked
  DispatchBluetoothReply(mRunnable, BluetoothValue(true), EmptyString());
  mCallback();
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
    return;
  }

  LOG("[C] all profiles disconnect complete.");
  MOZ_ASSERT(mRunnable && mCallback);
  // The action has been completed, so the dom request is replied and then
  // the callback is invoked
  DispatchBluetoothReply(mRunnable, BluetoothValue(true), EmptyString());
  mCallback();
}

void
BluetoothProfileController::OnDisconnectReply()
{
  LOG("[C] %s", __FUNCTION__);
  mProfilesIndex++;
  DisconnectNext();
}

