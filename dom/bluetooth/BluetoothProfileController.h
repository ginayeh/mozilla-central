/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothprofilecontroller_h__
#define mozilla_dom_bluetooth_bluetoothprofilecontroller_h__

//#include "BluetoothCommon.h"
//#include "BluetoothProfileManagerBase.h"
#include "BluetoothReplyRunnable.h"
#include "BluetoothUuid.h"

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothProfileManagerBase;

class BluetoothCodHelper
{
public:
  /*
   * Class of Device(CoD): 32-bit unsigned integer
   *
   *  31   24  23    13 12     8 7      2 1 0
   * |       | major   | major  | minor  |   |
   * |       | service | device | device |   |
   * |       | class   | class  | class  |   |
   * |       |<- 11  ->|<- 5  ->|<- 6  ->|   |
   *
   * bit 23 - bit 13: major service class
   * bit 12 - bit  8: major device class
   * bit  7 - bit  2: minor device class
   *
   * https://www.bluetooth.org/en-us/specification/assigned-numbers/baseband
   */

  static uint16_t GetMajorServiceClass(uint32_t aCod)
  {
    return ((aCod & 0xffe000) >> 13);
  }

  static uint8_t GetMajorDeviceClass(uint32_t aCod)
  {
    return ((aCod & 0x1f00) >> 8);
  }

  static uint8_t GetMinorDeviceClass(uint32_t aCod)
  {
    return ((aCod & 0xfc) >> 2);
  }

  static bool HasAudio(uint32_t aCod)
  {
    // Extract bit 21
    return ((aCod & 0x200000) >> 21);
  }

  static bool HasObjectTransfer(uint32_t aCod)
  {
    // Extract bit 20
    return ((aCod & 0x100000) >> 20);
  }

  static bool HasRendering(uint32_t aCod)
  {
    // Extract bit 18
    return ((aCod & 0x40000) >> 18);
  }

  static bool IsHeadset(uint32_t aCod)
  {
    // Extract bit 10
    bool isAudioVideo = (aCod & 0x400) >> 10;

    // Extract bit 2
    bool isHeadset = (aCod & 0x4)  >> 2;

    return (isAudioVideo && isHeadset);
  }
};

class BluetoothProfileController
{
public:
  BluetoothProfileController(const nsAString& aDeviceAddress,
                             uint32_t aCod, BluetoothReplyRunnable* aRunnable);
  BluetoothProfileController(
                  const nsAString& aDeviceAddress,
                  BluetoothServiceClass aClass = BluetoothServiceClass::UNKNOWN,
                  BluetoothReplyRunnable* aRunnable = nullptr);

  void Connect();
  void OnConnectReply();

  void Disconnect();
  void OnDisconnectReply();

  uint32_t GetCod();

private:
  void ConnectNext();
  void DisconnectNext();

  nsTArray<BluetoothProfileManagerBase*> mProfiles;
  BluetoothReplyRunnable* mRunnable;
  int8_t mProfilesIndex;
  uint32_t mCod;
  nsString mDeviceAddress;
  nsString mErrorString;
};

END_BLUETOOTH_NAMESPACE

#endif
