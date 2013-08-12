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

class BluetoothProfileController
{
public:
  BluetoothProfileController(uint32_t aCod, BluetoothReplyRunnable* aRunnable);
  BluetoothProfileController(BluetoothServiceClass aClass, BluetoothReplyRunnable* aRunnable);

  void Connect(const nsAString& aDeviceAddress);
  void OnConnectCallback();

  void Disconnect(const nsAString& aDeviceAddress);
  void OnDisconnectCallback();

  void SetErrorString(const char* aErrorString);

private:
  void ConnectNext();
  void DisconnectNext();

  nsTArray<BluetoothProfileManagerBase*> mProfiles;
  BluetoothReplyRunnable* mRunnable;
  int8_t mProfilesIndex;
  nsString mDeviceAddress;
  nsString mErrorString;
};

END_BLUETOOTH_NAMESPACE

#endif
