/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothdbuscommon_h__
#define mozilla_dom_bluetooth_bluetoothdbuscommon_h__

#define BLUEZ_DBUS_BASE_IFC                     "org.bluez"
#define DBUS_MANAGER_IFACE BLUEZ_DBUS_BASE_IFC  ".Manager"
#define DBUS_ADAPTER_IFACE BLUEZ_DBUS_BASE_IFC  ".Adapter"
#define DBUS_DEVICE_IFACE  BLUEZ_DBUS_BASE_IFC  ".Device"
#define DBUS_AGENT_IFACE   BLUEZ_DBUS_BASE_IFC  ".Agent"
#define DBUS_SINK_IFACE    BLUEZ_DBUS_BASE_IFC  ".AudioSink"
#define DBUS_CTL_IFACE     BLUEZ_DBUS_BASE_IFC  ".Control"

#define B2G_AGENT_CAPABILITIES "DisplayYesNo"
#define BLUEZ_DBUS_BASE_PATH      "/org/bluez"
#define BLUEZ_ERROR_IFC           "org.bluez.Error"

#define ERR_SERVICE_NOT_READY         "ServiceNotReadyError"
#define ERR_UNKNOWN_PROFILE           "UnknownProfileError"
#define ERR_PAIRING_REQUEST_RETRIEVAL "PairingRequestRetrievalError"
#define ERR_MEMORY_ALLOCATION         "MemoryAllocationError"
#define ERR_A2DP_IS_DISCONNECTED      "A2dpIsDisconnected"
#define ERR_AVRCP_IS_DISCONNECTED     "AvrcpIsDisconnected"

#endif // mozilla_dom_bluetooth_bluetoothdbuscommon_h__
