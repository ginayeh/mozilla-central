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
  MOZ_ASSERT(!aRunnable);

  /**
   * Class of Device(CoD): 32-bit
   * bit 2 - bit 7: minor device class
   * bit 8 - bit 12: major device class
   * bit 13 - bit 23: major service class
   */

  // Extract major service class
  uint16_t serviceClass = ((aCod & 0xffe000) >> 12);
  bool hasAudio = false;
  bool hasObjectTransfer = false;
  bool hasRendering = false;

  // bit 21: Audio
  // bit 20: Object Transfer
  // bit 18: Rendering
  hasAudio = ((serviceClass & 0x100) >> 8);
  hasObjectTransfer = ((serviceClass & 0x80) >> 7);
  hasRendering = ((serviceClass & 0x20) >> 5);

  mProfilesIndex = 0;
  if (!hasAudio && !hasObjectTransfer && !hasRendering) {
    return;
  }

  mProfilesIndex = 0;
  mRunnable = aRunnable;
  mCod = aCod;

  /**
   * We connect HFP/HSP first. Then, connect A2DP if Rendering bit is set.
   * It's almost impossible to send file to a remote device which is a Audio
   * device or a Rendering device, so we won't connect OPP in that case.
   */
  if (hasAudio) {
    // bit 10: Audio/Video
    uint8_t deviceClass = ((aCod & 0x1ffc) >> 2);
    if (((deviceClass & 0x80) >> 7) && (deviceClass & 0x1)) {
      // HSP
      mProfiles.AppendElement(BluetoothHfpManager::Get());
    } else {
      // HFP
      mProfiles.AppendElement(BluetoothHfpManager::Get());
    }
  }
  if (hasRendering) {
    mProfiles.AppendElement(BluetoothA2dpManager::Get());
  }
  if (hasObjectTransfer && !hasAudio && !hasRendering) {
    mProfiles.AppendElement(BluetoothOppManager::Get());
  }
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
    mProfilesIndex = 0;
    mRunnable = aRunnable;
    mProfiles.AppendElement(profile);
  }
}

void
BluetoothProfileController::Connect(const nsAString& aDeviceAddress)
{
  MOZ_ASSERT(mProfilesIndex < mProfiles.Length());
  MOZ_ASSERT(!aDeviceAddress.IsEmpty());

  mDeviceAddress = aDeviceAddress;

  ConnectNext();
}

void
BluetoothProfileController::ConnectNext()
{
  if (mProfilesIndex < mProfiles.Length()) {
    mProfiles[mProfilesIndex]->Connect(mDeviceAddress, this);
  } else {
    // All profiles has been tried
    mDeviceAddress.Truncate();
    mProfilesIndex = -1;
    mProfiles.Clear();
  }
}

void
BluetoothProfileController::OnConnectReply()
{
  mProfilesIndex++;
  ConnectNext();
}

void
BluetoothProfileController::Disconnect(const nsAString& aDeviceAddress)
{
  MOZ_ASSERT(!aDeviceAddress.IsEmpty());

  mDeviceAddress = aDeviceAddress;

  DisconnectNext();
}

void
BluetoothProfileController::DisconnectNext()
{
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
  mProfilesIndex++;
  DisconnectNext();
}

