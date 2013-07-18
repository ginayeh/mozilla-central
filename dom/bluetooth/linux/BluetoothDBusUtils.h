/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothdbusutils_h__
#define mozilla_dom_bluetooth_bluetoothdbusutils_h__

#include <dbus/dbus.h>

#include "BluetoothCommon.h"
#include "BluetoothDBusCommon.h"
#include "BluetoothDBusService.h"
#include "BluetoothService.h"
#include "BluetoothA2dpManager.h"
#include "BluetoothHfpManager.h"
#include "BluetoothOppManager.h"
#include "BluetoothUnixSocketConnector.h"
#include "BluetoothUtils.h"
#include "BluetoothUuid.h"

#include "mozilla/dom/bluetooth/BluetoothTypes.h"

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothValue;

typedef struct {
  const char* name;
  int type;
} Properties;

typedef void (*UnpackFunc)(DBusMessage*, DBusError*, BluetoothValue&, nsAString&);
typedef bool (*FilterFunc)(const InfallibleTArray<BluetoothNamedValue>&);

void
OnCreatePairedDeviceReply(DBusMessage* aMsg, void* aBluetoothReplyRunnable);

void
OnRemoveDeviceReply(DBusMessage* aMsg, void* aBluetoothReplyRunnable);

void
OnControlReply(DBusMessage* aMsg, void* aBluetoothReplyRunnable);

void
OnSetPropertyReply(DBusMessage* aMsg, void* aBluetoothReplyRunnable);

void
OnSendSinkConnectReply(DBusMessage* aMsg, void* aParam);

void
OnSendSinkDisconnectReply(DBusMessage* aMsg, void* aParam);

void
OnUpdatePlayStatusReply(DBusMessage* aMsg, void* aParam);

void
OnSendDiscoveryMessageReply(DBusMessage* aMsg, void* aBluetoothReplyRunnable);

void
OnDiscoverServicesReply(DBusMessage* aMsg, void* aData);

bool
GetConnectedDevicesFilter(const InfallibleTArray<BluetoothNamedValue>& aProp);

bool
GetPairedDevicesFilter(const InfallibleTArray<BluetoothNamedValue>& aProp);

bool
GetPropertiesInternal(const nsAString& aPath,
                      const char* aIface,
                      BluetoothValue& aValue);

bool
GetDefaultAdapterPath(BluetoothValue& aValue, nsString& aError);

bool
RegisterAgent();

int
GetDeviceServiceChannel(const nsAString& aObjectPath,
                        const nsAString& aPattern,
                        int aAttributeId);

void
AppendDeviceIcon(InfallibleTArray<BluetoothNamedValue>& aProperties);

DBusHandlerResult
EventFilter(DBusConnection* aConn, DBusMessage* aMsg, void* aData);

DBusHandlerResult
AgentEventFilter(DBusConnection *conn, DBusMessage *msg, void *data);

PLDHashOperator
UnrefDBusMessages(const nsAString& key, DBusMessage* value, void* arg);

class BluetoothValue;
class BluetoothProfileManagerBase;
class BluetoothSignal;

class PrepareProfileManagersRunnable : public nsRunnable
{
public:
  nsresult Run()
  {
//    LOG("[B] PrepareProfileManagersRunnable::Run");
    BluetoothHfpManager* hfp = BluetoothHfpManager::Get();
    if (!hfp || !hfp->Listen()) {
      NS_WARNING("Failed to start listening for BluetoothHfpManager!");
      return NS_ERROR_FAILURE;
    }

    BluetoothOppManager* opp = BluetoothOppManager::Get();
    if (!opp || !opp->Listen()) {
      NS_WARNING("Failed to start listening for BluetoothOppManager!");
      return NS_ERROR_FAILURE;
    }

    BluetoothA2dpManager* a2dp = BluetoothA2dpManager::Get();
    NS_ENSURE_TRUE(a2dp, NS_ERROR_FAILURE);
    a2dp->ResetA2dp();
    a2dp->ResetAvrcp();

    return NS_OK;
  }
};

class PrepareAdapterRunnable : public nsRunnable
{
public:
  nsresult Run()
  {
//    LOG("[B] PrepareAdapterRunnable::Run");
    MOZ_ASSERT(!NS_IsMainThread());
    nsTArray<uint32_t> uuids;

    uuids.AppendElement(BluetoothServiceClass::HANDSFREE_AG);
    uuids.AppendElement(BluetoothServiceClass::HEADSET_AG);
    uuids.AppendElement(BluetoothServiceClass::OBJECT_PUSH);

    // TODO/qdot: This needs to be held for the life of the bluetooth connection
    // so we could clean it up. For right now though, we can throw it away.
    nsTArray<uint32_t> handles;

    if (!BluetoothDBusService::AddReservedServicesInternal(uuids, handles)) {
      NS_WARNING("Failed to add reserved services");
#ifdef MOZ_WIDGET_GONK
      return NS_ERROR_FAILURE;
#endif
    }

    if(!RegisterAgent()) {
      NS_WARNING("Failed to register agent");
      return NS_ERROR_FAILURE;
    }

    NS_DispatchToMainThread(new PrepareProfileManagersRunnable());
    return NS_OK;
  }
};

class PrepareAdapterTask : public nsRunnable
{
public:
  PrepareAdapterTask(const nsAString& aPath)
    : mPath(aPath)
  {
  }

  nsresult Run()
  {
//    LOG("[B] PrepareAdapterTask::Run");
    MOZ_ASSERT(NS_IsMainThread());

    BluetoothService* bs = BluetoothService::Get();
    NS_ENSURE_TRUE(bs, NS_ERROR_FAILURE);
    BluetoothDBusService::SetAdapterPath(mPath);
    nsRefPtr<nsRunnable> func(new PrepareAdapterRunnable());
    bs->DispatchToCommandThread(func);
    return NS_OK;
  }

private:
  nsString mPath;
};

class OnGetServiceChannelRunnable : public nsRunnable
{
public:
  OnGetServiceChannelRunnable(const nsAString& aObjectPath,
      const nsAString& aServiceUuid,
      int aChannel,
      BluetoothProfileManagerBase* aManager)
    : mServiceUuid(aServiceUuid)
    , mChannel(aChannel)
    , mManager(aManager)
  {
    MOZ_ASSERT(!aObjectPath.IsEmpty());
    MOZ_ASSERT(!aServiceUuid.IsEmpty());
    MOZ_ASSERT(aManager);

    mDeviceAddress = GetAddressFromObjectPath(aObjectPath);
  }

  nsresult
  Run()
  {
//      LOG("[B] OnGetServiceChannelRunnable::Run");
    MOZ_ASSERT(NS_IsMainThread());

    mManager->OnGetServiceChannel(mDeviceAddress, mServiceUuid, mChannel);
    return NS_OK;
   }

private:
  nsString mDeviceAddress;
  nsString mServiceUuid;
  int mChannel;
  BluetoothProfileManagerBase* mManager;
};

class GetServiceChannelRunnable : public nsRunnable
{
public:
  GetServiceChannelRunnable(const nsAString& aObjectPath,
                            const nsAString& aServiceUuid,
                            BluetoothProfileManagerBase* aManager)
    : mObjectPath(aObjectPath),
      mServiceUuid(aServiceUuid),
      mManager(aManager)
  {
    MOZ_ASSERT(!aObjectPath.IsEmpty());
    MOZ_ASSERT(!aServiceUuid.IsEmpty());
    MOZ_ASSERT(aManager);
  }

  nsresult
  Run()
  {
//    LOG("[B] [ct] GetServiceChannelRunnable::Run");
    MOZ_ASSERT(!NS_IsMainThread());

    int channel = GetDeviceServiceChannel(mObjectPath, mServiceUuid, 0x0004);
    nsRefPtr<nsRunnable> r(new OnGetServiceChannelRunnable(mObjectPath,
                                                           mServiceUuid,
                                                           channel,
                                                           mManager));
    NS_DispatchToMainThread(r);
    return NS_OK;
  }

private:
  nsString mObjectPath;
  nsString mServiceUuid;
  BluetoothProfileManagerBase* mManager;
};

class BluetoothArrayOfDevicePropertiesRunnable : public nsRunnable
{
public:
  BluetoothArrayOfDevicePropertiesRunnable(
                                     const nsTArray<nsString>& aDeviceAddresses,
                                     BluetoothReplyRunnable* aRunnable,
                                     FilterFunc aFilterFunc)
    : mDeviceAddresses(aDeviceAddresses)
    , mRunnable(dont_AddRef(aRunnable))
    , mFilterFunc(aFilterFunc)
  {
  }

  nsresult Run()
  {
//    LOG("[B] BluetoothArrayOfDevicePropertiesRunnable::Run");
    MOZ_ASSERT(!NS_IsMainThread());
    DBusError err;
    dbus_error_init(&err);

    BluetoothValue values = InfallibleTArray<BluetoothNamedValue>();
    nsAutoString errorStr;

    for (uint32_t i = 0; i < mDeviceAddresses.Length(); i++) {
      BluetoothValue propValue;
      nsString adapterPath;
      BluetoothDBusService::GetAdapterPath(adapterPath);
      nsString objectPath =
        GetObjectPathFromAddress(adapterPath, mDeviceAddresses[i]);

      if (!GetPropertiesInternal(objectPath, DBUS_DEVICE_IFACE, propValue)) {
        errorStr.AssignLiteral("Getting properties failed!");
        break;
      }

      InfallibleTArray<BluetoothNamedValue>& properties =
        propValue.get_ArrayOfBluetoothNamedValue();
      AppendDeviceIcon(properties);

      // We have to manually attach the path to the rest of the elements
      properties.AppendElement(
        BluetoothNamedValue(NS_LITERAL_STRING("Path"), objectPath)
      );

      if (mFilterFunc(properties)) {
        values.get_ArrayOfBluetoothNamedValue().AppendElement(
          BluetoothNamedValue(mDeviceAddresses[i], properties)
        );
      }
    }

    DispatchBluetoothReply(mRunnable, values, errorStr);
    return NS_OK;
  }

private:
  nsTArray<nsString> mDeviceAddresses;
  nsRefPtr<BluetoothReplyRunnable> mRunnable;
  FilterFunc mFilterFunc;
};

class DefaultAdapterPropertiesRunnable : public nsRunnable
{
public:
  DefaultAdapterPropertiesRunnable(BluetoothReplyRunnable* aRunnable)
    : mRunnable(dont_AddRef(aRunnable))
  {
  }

  nsresult Run()
  {
//    LOG("[B] DefaultAdapterPropertiesRunnable::Run");
    MOZ_ASSERT(!NS_IsMainThread());

    BluetoothValue v;
    nsAutoString replyError;
    if (!GetDefaultAdapterPath(v, replyError)) {
      DispatchBluetoothReply(mRunnable, v, replyError);
      return NS_ERROR_FAILURE;
    }

    DBusError err;
    dbus_error_init(&err);

    nsString objectPath = v.get_nsString();
    v = InfallibleTArray<BluetoothNamedValue>();
    if (!GetPropertiesInternal(objectPath, DBUS_ADAPTER_IFACE, v)) {
      NS_WARNING("Getting properties failed!");
      return NS_ERROR_FAILURE;
    }

    // We have to manually attach the path to the rest of the elements
    v.get_ArrayOfBluetoothNamedValue().AppendElement(
      BluetoothNamedValue(NS_LITERAL_STRING("Path"), objectPath));

    DispatchBluetoothReply(mRunnable, v, replyError);

    return NS_OK;
  }

private:
  nsRefPtr<BluetoothReplyRunnable> mRunnable;
};

class OnUpdateSdpRecordsRunnable : public nsRunnable
{
public:
  OnUpdateSdpRecordsRunnable(const nsAString& aObjectPath,
                             BluetoothProfileManagerBase* aManager)
    : mManager(aManager)
  {
     MOZ_ASSERT(!aObjectPath.IsEmpty());
     MOZ_ASSERT(aManager);

     mDeviceAddress = GetAddressFromObjectPath(aObjectPath);
  }

  nsresult Run()
  {
//    LOG("[B] OnUpdateSdpRecordsRunnable::Run");
    MOZ_ASSERT(NS_IsMainThread());

    mManager->OnUpdateSdpRecords(mDeviceAddress);
    return NS_OK;
  }

private:
  nsString mDeviceAddress;
  BluetoothProfileManagerBase* mManager;
};

END_BLUETOOTH_NAMESPACE

#endif
