/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothprofilecontroller_h__
#define mozilla_dom_bluetooth_bluetoothprofilecontroller_h__

#include "BluetoothReplyRunnable.h"
#include "BluetoothUuid.h"

BEGIN_BLUETOOTH_NAMESPACE

/*
 * Class of Device(CoD): 32-bit unsigned integer
 *
 *  31   24  23    13 12     8 7      2 1 0
 * |       | Major   | Major  | Minor  |   |
 * |       | service | device | device |   |
 * |       | class   | class  | class  |   |
 * |       |<- 11  ->|<- 5  ->|<- 6  ->|   |
 *
 * https://www.bluetooth.org/en-us/specification/assigned-numbers/baseband
 */

// Bit 23 ~ Bit 13: Major service class
#define GetMajorServiceClass(cod) return ((cod & 0xffe000) >> 13);

// Bit 12 ~ Bit 8: Major device class
#define GetMajorDeviceClass(cod)  return ((cod & 0x1f00) >> 8);

// Bit 7 ~ Bit 2: Minor device class
#define GetMinorDeviceClass(cod)  return ((aCod & 0xfc) >> 2);

// Bit 21: Major service class = 0x100, Audio
#define HAS_AUDIO(cod)            return ((cod & 0x200000) >> 21);

// Bit 20: Major service class = 0x80, Object Transfer
#define HAS_OBJECT_TRANSFER(cod)  return ((cod & 0x100000) >> 20);

// Bit 18: Major service class = 0x20, Rendering
#define HAS_RENDING(cod)          return ((cod & 0x40000) >> 18);

// Bit 11 and Bit 9: Major device class = 0xA, Peripheral
#define IS_PERIPHERAL(cod)        return ((cod & 0x200) >> 9) && ((cod & 0x800) >> 11);

// Bit 10: Major device class = 0x4, Audio/Video
// Bit 2: Minor device class = 0x1, Wearable Headset Device 
#define IS_HEADSET(cod)           return ((cod & 0x400) >> 10) && ((cod & 0x4)  >> 2);

class BluetoothProfileManagerBase;
typedef void (*BluetoothProfileControllerCallback)();

class BluetoothProfileController
{
public:
  // Connect/Disconnect to a specific service UUID.
  BluetoothProfileController(const nsAString& aDeviceAddress,
                             BluetoothServiceClass aClass,
                             BluetoothReplyRunnable* aRunnable,
                             BluetoothProfileControllerCallback aCallback);

  // Based on the class of device(CoD), connect to multiple profiles
  // sequencely.
  BluetoothProfileController(const nsAString& aDeviceAddress,
                             uint32_t aCod,
                             BluetoothReplyRunnable* aRunnable,
                             BluetoothProfileControllerCallback aCallback);

  // Disconnect all connected profiles.
  BluetoothProfileController(const nsAString& aDeviceAddress,
                             BluetoothReplyRunnable* aRunnable,
                             BluetoothProfileControllerCallback aCallback);

  ~BluetoothProfileController();

  void Connect();
  void OnConnectReply();

  void Disconnect();
  void OnDisconnectReply();

  uint32_t GetCod();

private:
  void Init(const nsAString& aDeviceAddress,
            BluetoothReplyRunnable* aRunnable,
            BluetoothProfileControllerCallback aCallback);

  void ConnectNext();
  void DisconnectNext();

  int8_t mProfilesIndex;
  nsTArray<BluetoothProfileManagerBase*> mProfiles;

  uint32_t mCod;
  nsString mDeviceAddress;
  BluetoothReplyRunnable* mRunnable;
  BluetoothProfileControllerCallback mCallback;
};

END_BLUETOOTH_NAMESPACE

#endif
