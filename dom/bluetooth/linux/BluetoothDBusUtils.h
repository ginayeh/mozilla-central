/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothdbusutils_h__
#define mozilla_dom_bluetooth_bluetoothdbusutils_h__

#include <dbus/dbus.h>

#include "BluetoothCommon.h"

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothValue;

typedef struct {
  const char* name;
  int type;
} Properties;

void
UnpackIntMessage(DBusMessage* aMsg, DBusError* aErr,
                 BluetoothValue& aValue, nsAString& aErrorStr);

void
UnpackObjectPathMessage(DBusMessage* aMsg, DBusError* aErr,
                        BluetoothValue& aValue, nsAString& aErrorStr);

void
UnpackVoidMessage(DBusMessage* aMsg, DBusError* aErr,
                  BluetoothValue& aValue, nsAString& aErrorStr);

void
UnpackManagerPropertiesMessage(DBusMessage* aMsg, DBusError* aErr,
                               BluetoothValue& aValue, nsAString& aErrorStr);

void
UnpackAdapterPropertiesMessage(DBusMessage* aMsg, DBusError* aErr,
                               BluetoothValue& aValue, nsAString& aErrorStr);

void
UnpackDevicePropertiesMessage(DBusMessage* aMsg, DBusError* aErr,
                              BluetoothValue& aValue, nsAString& aErrorStr);

void
ParseProperties(DBusMessageIter* aIter, BluetoothValue& aValue,
                nsAString& aErrorStr, Properties* aPropertyTypes,
                const int aPropertyTypeLen);

void
ParseDeviceProperties(DBusMessageIter* aIter, BluetoothValue& aValue,
                      nsAString& aErrorStr);

void
ParseManagerPropertyChange(DBusMessage* aMsg, BluetoothValue& aValue,
                           nsAString& aErrorStr);

void
ParseAdapterPropertyChange(DBusMessage* aMsg, BluetoothValue& aValue,
                           nsAString& aErrorStr);

void
ParseDevicePropertyChange(DBusMessage* aMsg, BluetoothValue& aValue,
                          nsAString& aErrorStr);

void
ParseSinkPropertyChange(DBusMessage* aMsg, BluetoothValue& aValue,
                        nsAString& aErrorStr);

void
ParseControlPropertyChange(DBusMessage* aMsg, BluetoothValue& aValue,
                           nsAString& aErrorStr);

END_BLUETOOTH_NAMESPACE

#endif
