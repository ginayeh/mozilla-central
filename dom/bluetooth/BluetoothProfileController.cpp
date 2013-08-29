/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BluetoothProfileController.h"
#include "BluetoothReplyRunnable.h"

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
                                   BluetoothReplyRunnable* aRunnable,
                                   BluetoothProfileControllerCallback aCallback)
  : mDeviceAddress(aDeviceAddress)
  , mRunnable(aRunnable)
  , mCallback(aCallback)
{
  LOG("[C] %s", __FUNCTION__);

  MOZ_ASSERT(!aDeviceAddress.IsEmpty());
  MOZ_ASSERT(aRunnable);
  MOZ_ASSERT(aCallback);

  mProfilesIndex = -1;
  mProfiles.Clear();
}

BluetoothProfileController::~BluetoothProfileController()
{
  LOG("[C] %s", __FUNCTION__);
  mProfilesIndex = -1;
  mProfiles.Clear();
  mRunnable = nullptr;
  mCallback = nullptr;
}

bool
BluetoothProfileController::SetProfileArrayWithServiceClass(
                                                   BluetoothServiceClass aClass)
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

  NS_ENSURE_TRUE(profile, false);

  mProfiles.AppendElement(profile);
  return true;
}

void
BluetoothProfileController::Connect(BluetoothServiceClass aClass)
{
  LOG("[C] %s", __FUNCTION__);

  if (!SetProfileArrayWithServiceClass(aClass)) {
    DispatchBluetoothReply(mRunnable, BluetoothValue(),
                           NS_LITERAL_STRING(ERR_UNKNOWN_PROFILE));
    mCallback();
    return;
  }

  ConnectNext();
}

void
BluetoothProfileController::Connect(uint32_t aCod)
{
  LOG("[C] %s", __FUNCTION__);

  // Put multiple profiles into array and connect to all of them sequencely
  bool hasAudio = HAS_AUDIO(aCod);
  bool hasObjectTransfer = HAS_OBJECT_TRANSFER(aCod);
  bool hasRendering = HAS_RENDERING(aCod);
  bool isPeripheral = IS_PERIPHERAL(aCod);

  NS_ENSURE_TRUE_VOID(hasAudio || hasObjectTransfer ||
                      hasRendering || isPeripheral);

  LOG("[C] hasAudio: %d, hasObjectTransfer: %d, hasRendering: %d, isPeripheral: %d", hasAudio, hasObjectTransfer, hasRendering, isPeripheral);

  mCod = aCod;

  /**
   * Connect to HFP/HSP first. Then, connect A2DP if Rendering bit is set.
   * It's almost impossible to send file to a remote device which is an Audio
   * device or a Rendering device, so we won't connect OPP in that case.
   */
  BluetoothProfileManagerBase* profile;
  if (hasAudio) {
    profile = BluetoothHfpManager::Get();
    NS_ENSURE_TRUE_VOID(profile);
    mProfiles.AppendElement(profile);
  }
  if (hasRendering) {
    profile = BluetoothA2dpManager::Get();
    NS_ENSURE_TRUE_VOID(profile);
    mProfiles.AppendElement(profile);
  }
  if (hasObjectTransfer && !hasAudio && !hasRendering) {
    profile = BluetoothOppManager::Get();
    NS_ENSURE_TRUE_VOID(profile);
    mProfiles.AppendElement(profile);
  }
  if (isPeripheral) {
    profile = BluetoothHidManager::Get();
    NS_ENSURE_TRUE_VOID(profile);
    mProfiles.AppendElement(profile);
  }

  ConnectNext();
}

void
BluetoothProfileController::ConnectNext()
{
  LOG("[C] %s", __FUNCTION__);

  if (++mProfilesIndex < mProfiles.Length()) {
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
BluetoothProfileController::OnConnect(const nsAString& aErrorStr)
{
  LOG("[C] %s", __FUNCTION__);
  if (!aErrorStr.IsEmpty()) {
    BT_WARNING(NS_ConvertUTF16toUTF8(aErrorStr).get());
  }

  ConnectNext();
}

void
BluetoothProfileController::Disconnect(BluetoothServiceClass aClass)
{
  LOG("[C] %s", __FUNCTION__);

  if (aClass != BluetoothServiceClass::UNKNOWN) {
    if (!SetProfileArrayWithServiceClass(aClass)) {
      DispatchBluetoothReply(mRunnable, BluetoothValue(),
                             NS_LITERAL_STRING(ERR_UNKNOWN_PROFILE));
      mCallback();
      return;
    }

    DisconnectNext();
    return;
  }

  // Put all connected profiles into array and disconnect all of them
  BluetoothProfileManagerBase* profile;
  profile = BluetoothHfpManager::Get();
  NS_ENSURE_TRUE_VOID(profile);
  if (profile->IsConnected()) {
    mProfiles.AppendElement(profile);
  }
  profile = BluetoothOppManager::Get();
  NS_ENSURE_TRUE_VOID(profile);
  if (profile->IsConnected()) {
    mProfiles.AppendElement(profile);
  }
  profile = BluetoothHidManager::Get();
  NS_ENSURE_TRUE_VOID(profile);
  if (profile->IsConnected()) {
    mProfiles.AppendElement(profile);
  }
  profile = BluetoothA2dpManager::Get();
  NS_ENSURE_TRUE_VOID(profile);
  if (profile->IsConnected()) {
    mProfiles.AppendElement(profile);
  }

  DisconnectNext();
}

void
BluetoothProfileController::DisconnectNext()
{
  LOG("[C] %s", __FUNCTION__);
  if (++mProfilesIndex < mProfiles.Length()) {
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
BluetoothProfileController::OnDisconnect(const nsAString& aErrorStr)
{
  LOG("[C] %s", __FUNCTION__);
  if (!aErrorStr.IsEmpty()) {
    BT_WARNING(NS_ConvertUTF16toUTF8(aErrorStr).get());
  }

  DisconnectNext();
}

