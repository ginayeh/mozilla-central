/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothdbuscallback_h__
#define mozilla_dom_bluetooth_bluetoothdbuscallback_h__

#include <dbus/dbus.h>

#include "BluetoothCommon.h"
#include "BluetoothService.h"
#include "BluetoothUtils.h"

#include "BluetoothA2dpManager.h"
#include "BluetoothHfpManager.h"
#include "BluetoothOppManager.h"
#include "BluetoothUnixSocketConnector.h"

#include "mozilla/dom/bluetooth/BluetoothTypes.h"

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothValue;

typedef struct {
  const char* name;
  int type;
} Properties;

typedef void (*UnpackFunc)(DBusMessage*, DBusError*, BluetoothValue&, nsAString&);

void
UnpackObjectPathMessage(DBusMessage* aMsg, DBusError* aErr,
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

void
GetObjectPathCallback(DBusMessage* aMsg, void* aBluetoothReplyRunnable);

void
GetVoidCallback(DBusMessage* aMsg, void* aBluetoothReplyRunnable);

void
GetIntPathCallback(DBusMessage* aMsg, void* aBluetoothReplyRunnable);

void
GetManagerPropertiesCallback(DBusMessage* aMsg, void* aBluetoothReplyRunnable);

void
GetAdapterPropertiesCallback(DBusMessage* aMsg, void* aBluetoothReplyRunnable);

void
GetDevicePropertiesCallback(DBusMessage* aMsg, void* aBluetoothReplyRunnable);

void
SinkConnectCallback(DBusMessage* aMsg, void* aParam);

void
SinkDisconnectCallback(DBusMessage* aMsg, void* aParam);

void
ControlCallback(DBusMessage* aMsg, void* aParam);

void
OnSendDiscoveryMessageReply(DBusMessage *aReply, void *aData);

void
DiscoverServicesCallback(DBusMessage* aMsg, void* aData);

class BluetoothValue;
class BluetoothProfileManagerBase;
class BluetoothSignal;

class DistributeBluetoothSignalTask : public nsRunnable {
public:
  DistributeBluetoothSignalTask(const BluetoothSignal& aSignal)
    : mSignal(aSignal)
  {
  }

  nsresult Run()
  {
//  LOG("[B] DistributeBluetoothSignalTask::Run()");
    MOZ_ASSERT(NS_IsMainThread());

    BluetoothService* bs = BluetoothService::Get();
    NS_ENSURE_TRUE(bs, NS_ERROR_FAILURE);
    bs->DistributeSignal(mSignal);
    return NS_OK;
  }

private:
  BluetoothSignal mSignal;
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

class ControlPropertyChangedHandler : public nsRunnable
{
public:
  ControlPropertyChangedHandler(const BluetoothSignal& aSignal)
    : mSignal(aSignal)
  {
  }

  nsresult Run()
  {
//    LOG("[B] ControlPropertyChangedHandler::Run");
    MOZ_ASSERT(NS_IsMainThread());
    if (mSignal.value().type() != BluetoothValue::TArrayOfBluetoothNamedValue) {
      BT_WARNING("Wrong value type for ControlPropertyChangedHandler");
      return NS_ERROR_FAILURE;
    }

    InfallibleTArray<BluetoothNamedValue>& arr =
      mSignal.value().get_ArrayOfBluetoothNamedValue();
    MOZ_ASSERT(arr[0].name().EqualsLiteral("Connected"));
    MOZ_ASSERT(arr[0].value().type() == BluetoothValue::Tbool);
    bool connected = arr[0].value().get_bool();

    BluetoothA2dpManager* a2dp = BluetoothA2dpManager::Get();
    NS_ENSURE_TRUE(a2dp, NS_ERROR_FAILURE);
    a2dp->SetAvrcpConnected(connected);
    return NS_OK;
  }

private:
  BluetoothSignal mSignal;
};

class SinkPropertyChangedHandler : public nsRunnable
{
public:
  SinkPropertyChangedHandler(const BluetoothSignal& aSignal)
    : mSignal(aSignal)
  {
  }

  nsresult Run()
  {
//    LOG("[B] SinkPropertyChangedHandler::Run");
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mSignal.name().EqualsLiteral("PropertyChanged"));
    MOZ_ASSERT(mSignal.value().type() ==
      BluetoothValue::TArrayOfBluetoothNamedValue);

    // Replace object path with device address
    nsString address =
      GetAddressFromObjectPath(mSignal.path());
    mSignal.path() = address;

    BluetoothA2dpManager* a2dp = BluetoothA2dpManager::Get();
    NS_ENSURE_TRUE(a2dp, NS_ERROR_FAILURE);
    a2dp->HandleSinkPropertyChanged(mSignal);
    return NS_OK;
  }

private:
  BluetoothSignal mSignal;
};

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

class SendPlayStatusTask : public nsRunnable
{
public:
  SendPlayStatusTask()
  {
    MOZ_ASSERT(!NS_IsMainThread());
  }

  nsresult Run()
  {
    MOZ_ASSERT(NS_IsMainThread());
//    LOG("[B] SendPlayStatusTask::Run()");

    BluetoothA2dpManager* a2dp = BluetoothA2dpManager::Get();
    NS_ENSURE_TRUE(a2dp, NS_ERROR_FAILURE);

    uint32_t duration, position;
    ControlPlayStatus playStatus;
    a2dp->GetDuration(&duration);
    a2dp->GetPosition(&position);
    a2dp->GetPlayStatus(&playStatus);
//    LOG("[B] duration: %d, position: %d, playstatus: %d", duration, position, playStatus);

    BluetoothService* bs = BluetoothService::Get();
    NS_ENSURE_TRUE(bs, NS_ERROR_FAILURE);

    bs->UpdatePlayStatus(duration, position, playStatus);
    return NS_OK;
  }
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

class ConnectBluetoothSocketRunnable : public nsRunnable
{
public:
  ConnectBluetoothSocketRunnable(BluetoothReplyRunnable* aRunnable,
    UnixSocketConsumer* aConsumer,
    const nsAString& aObjectPath,
    const nsAString& aServiceUUID,
    BluetoothSocketType aType,
    bool aAuth,
    bool aEncrypt,
    int aChannel)
    : mRunnable(dont_AddRef(aRunnable))
    , mConsumer(aConsumer)
    , mObjectPath(aObjectPath)
    , mServiceUUID(aServiceUUID)
    , mType(aType)
    , mAuth(aAuth)
    , mEncrypt(aEncrypt)
    , mChannel(aChannel)
  {
  }

  nsresult Run()
  {
//        LOG("[B] [mt] ConnectBluetoothSocketRunnable::Run");
    MOZ_ASSERT(NS_IsMainThread());

    nsString address = GetAddressFromObjectPath(mObjectPath);
    BluetoothUnixSocketConnector* c =
      new BluetoothUnixSocketConnector(mType, mChannel, mAuth, mEncrypt);
    if (!mConsumer->ConnectSocket(c, NS_ConvertUTF16toUTF8(address).get())) {
      NS_NAMED_LITERAL_STRING(errorStr, "SocketConnectionError");
      DispatchBluetoothReply(mRunnable, BluetoothValue(), errorStr);
      return NS_ERROR_FAILURE;
    }
    return NS_OK;
  }

private:
  nsRefPtr<BluetoothReplyRunnable> mRunnable;
  nsRefPtr<UnixSocketConsumer> mConsumer;
  nsString mObjectPath;
  nsString mServiceUUID;
  BluetoothSocketType mType;
  bool mAuth;
  bool mEncrypt;
  int mChannel;
};

END_BLUETOOTH_NAMESPACE

#endif
