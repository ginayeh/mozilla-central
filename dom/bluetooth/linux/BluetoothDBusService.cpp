/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/*
 ** Copyright 2006, The Android Open Source Project
 **
 ** Licensed under the Apache License, Version 2.0 (the "License");
 ** you may not use this file except in compliance with the License.
 ** You may obtain a copy of the License at
 **
 **     http://www.apache.org/licenses/LICENSE-2.0
 **
 ** Unless required by applicable law or agreed to in writing, software
 ** distributed under the License is distributed on an "AS IS" BASIS,
 ** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 ** See the License for the specific language governing permissions and
 ** limitations under the License.
 */

#include "base/basictypes.h"
#include "BluetoothDBusService.h"
#include "BluetoothA2dpManager.h"
#include "BluetoothHfpManager.h"
#include "BluetoothOppManager.h"
#include "BluetoothReplyRunnable.h"
#include "BluetoothUnixSocketConnector.h"
#include "BluetoothUtils.h"
#include "BluetoothUuid.h"
#include "BluetoothDBusUtils.h"

#include <cstdio>
#include <dbus/dbus.h>

#include "nsAutoPtr.h"
#include "nsThreadUtils.h"
#include "nsDebug.h"
#include "nsDataHashtable.h"
#include "mozilla/Atomics.h"
#include "mozilla/Hal.h"
#include "mozilla/ipc/UnixSocket.h"
#include "mozilla/ipc/DBusThread.h"
#include "mozilla/ipc/RawDBusConnection.h"
#include "mozilla/Util.h"
#include "mozilla/NullPtr.h"
#include "mozilla/dom/bluetooth/BluetoothTypes.h"
#if defined(MOZ_WIDGET_GONK)
#include "cutils/properties.h"
#endif

/**
 * Some rules for dealing with memory in DBus:
 * - A DBusError only needs to be deleted if it's been set, not just
 *   initialized. This is why LOG_AND_FREE... is called only when an error is
 *   set, and the macro cleans up the error itself.
 * - A DBusMessage needs to be unrefed when it is newed explicitly. DBusMessages
 *   from signals do not need to be unrefed, as they will be cleaned by DBus
 *   after DBUS_HANDLER_RESULT_HANDLED is returned from the filter.
 */

using namespace mozilla;
using namespace mozilla::ipc;
USING_BLUETOOTH_NAMESPACE

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "GonkDBus", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

#undef LOGV
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOGV(args...)  __android_log_print(ANDROID_LOG_INFO, "GonkDBusV", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

#define CHECK_SERVICE_STATUS(aRunnable, aErrorReturnValue)           \
  if (!IsReady()) {                                                  \
    DispatchBluetoothReply(aRunnable, BluetoothValue(),              \
                           NS_LITERAL_STRING(ERR_SERVICE_NOT_READY));\
    return aErrorReturnValue;                                        \
  }

#define CHECK_SERVICE_STATUS_VOID(aRunnable)                         \
  if (!IsReady()) {                                                  \
    DispatchBluetoothReply(aRunnable, BluetoothValue(),              \
                           NS_LITERAL_STRING(ERR_SERVICE_NOT_READY));\
    return;                                                          \
  }

static const char* sBluetoothDBusIfaces[] =
{
  DBUS_MANAGER_IFACE,
  DBUS_ADAPTER_IFACE,
  DBUS_DEVICE_IFACE
};

static const char* sBluetoothDBusSignals[] =
{
  "type='signal',interface='org.freedesktop.DBus'",
  "type='signal',interface='org.bluez.Adapter'",
  "type='signal',interface='org.bluez.Manager'",
  "type='signal',interface='org.bluez.Device'",
  "type='signal',interface='org.bluez.Input'",
  "type='signal',interface='org.bluez.Network'",
  "type='signal',interface='org.bluez.NetworkServer'",
  "type='signal',interface='org.bluez.HealthDevice'",
  "type='signal',interface='org.bluez.AudioSink'",
  "type='signal',interface='org.bluez.Control'"
};

Atomic<int32_t> BluetoothDBusService::mIsPairing;
nsString BluetoothDBusService::mAdapterPath;
nsDataHashtable<nsStringHashKey, DBusMessage* > BluetoothDBusService::mPairingReqTable;
nsDataHashtable<nsStringHashKey, DBusMessage* > BluetoothDBusService::mAuthorizeReqTable;
nsAutoPtr<RawDBusConnection> BluetoothDBusService::mCommandThreadConnection;

static void
ExtractHandles(DBusMessage *aReply, nsTArray<uint32_t>& aOutHandles)
{
  LOG("[B] %s", __FUNCTION__);
  uint32_t* handles = nullptr;
  int len;

  DBusError err;
  dbus_error_init(&err);

  if (dbus_message_get_args(aReply, &err,
                            DBUS_TYPE_ARRAY, DBUS_TYPE_UINT32, &handles, &len,
                            DBUS_TYPE_INVALID)) {
    if (!handles) {
      BT_WARNING("Null array in extract_handles");
    } else {
      for (int i = 0; i < len; ++i) {
        aOutHandles.AppendElement(handles[i]);
      }
    }
  } else {
    LOG_AND_FREE_DBUS_ERROR(&err);
  }
}

// static
bool
BluetoothDBusService::AddServiceRecords(const char* serviceName,
                                        unsigned long long uuidMsb,
                                        unsigned long long uuidLsb,
                                        int channel)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(!NS_IsMainThread());
//  NS_ENSURE_TRUE(this->IsReady(), false);

  DBusMessage* reply =
    dbus_func_args(mCommandThreadConnection->GetConnection(),
                   NS_ConvertUTF16toUTF8(mAdapterPath).get(),
                   DBUS_ADAPTER_IFACE, "AddRfcommServiceRecord",
                   DBUS_TYPE_STRING, &serviceName,
                   DBUS_TYPE_UINT64, &uuidMsb,
                   DBUS_TYPE_UINT64, &uuidLsb,
                   DBUS_TYPE_UINT16, &channel,
                   DBUS_TYPE_INVALID);

  return reply ? dbus_returns_uint32(reply) : -1;
}

// static
bool
BluetoothDBusService::AddReservedServicesInternal(
                                   const nsTArray<uint32_t>& aServices,
                                   nsTArray<uint32_t>& aServiceHandlesContainer)
{
  MOZ_ASSERT(!NS_IsMainThread());
//  NS_ENSURE_TRUE(this->IsReady(), false);

  int length = aServices.Length();
  if (length == 0) return false;

  const uint32_t* services = aServices.Elements();
  DBusMessage* reply =
    dbus_func_args(mCommandThreadConnection->GetConnection(),
                   NS_ConvertUTF16toUTF8(mAdapterPath).get(),
                   DBUS_ADAPTER_IFACE, "AddReservedServiceRecords",
                   DBUS_TYPE_ARRAY, DBUS_TYPE_UINT32,
                   &services, length, DBUS_TYPE_INVALID);

  if (!reply) {
    BT_WARNING("Null DBus message. Couldn't extract handles.");
    return false;
  }

  ExtractHandles(reply, aServiceHandlesContainer);
  return true;
}

void
BluetoothDBusService::DisconnectAllAcls(const nsAString& aAdapterPath)
{
  MOZ_ASSERT(!NS_IsMainThread());
//  NS_ENSURE_TRUE(this->IsReady(), false);
  LOG("[B] %s", __FUNCTION__);

  DBusMessage* reply =
    dbus_func_args(mCommandThreadConnection->GetConnection(),
                   NS_ConvertUTF16toUTF8(aAdapterPath).get(),
                   DBUS_ADAPTER_IFACE, "DisconnectAllConnections",
                   DBUS_TYPE_INVALID);

  if (reply) {
    dbus_message_unref(reply);
  }
}

bool
BluetoothDBusService::IsReady()
{
  if (!IsEnabled() || !mConnection ||
      !mCommandThreadConnection || IsToggling()) {
    BT_WARNING(ERR_SERVICE_NOT_READY);
    return false;
  }
  return true;
}

nsresult
BluetoothDBusService::StartInternal()
{
  LOG("[B] %s", __FUNCTION__);
  // This could block. It should never be run on the main thread.
  MOZ_ASSERT(!NS_IsMainThread());

  if (!StartDBus()) {
    NS_WARNING("Cannot start DBus thread!");
    return NS_ERROR_FAILURE;
  }

  if (mConnection) {
    return NS_OK;
  }

  if (NS_FAILED(EstablishDBusConnection())) {
    NS_WARNING("Cannot start Main Thread DBus connection!");
    StopDBus();
    return NS_ERROR_FAILURE;
  }

  mCommandThreadConnection = new RawDBusConnection();

  if (NS_FAILED(mCommandThreadConnection->EstablishDBusConnection())) {
    NS_WARNING("Cannot start Sync Thread DBus connection!");
    StopDBus();
    return NS_ERROR_FAILURE;
  }

  DBusError err;
  dbus_error_init(&err);

  // Set which messages will be processed by this dbus connection.
  // Since we are maintaining a single thread for all the DBus bluez
  // signals we want, register all of them in this thread at startup.
  // The event handler will sort the destinations out as needed.
  for (uint32_t i = 0; i < ArrayLength(sBluetoothDBusSignals); ++i) {
    dbus_bus_add_match(mConnection,
                       sBluetoothDBusSignals[i],
                       &err);
    if (dbus_error_is_set(&err)) {
      LOG_AND_FREE_DBUS_ERROR(&err);
    }
  }

  // Add a filter for all incoming messages_base
  if (!dbus_connection_add_filter(mConnection, EventFilter,
                                  NULL, NULL)) {
    NS_WARNING("Cannot create DBus Event Filter for DBus Thread!");
    return NS_ERROR_FAILURE;
  }

  if (!mPairingReqTable.IsInitialized()) {
    mPairingReqTable.Init();
  }

  if (!mAuthorizeReqTable.IsInitialized()) {
    mAuthorizeReqTable.Init();
  }

  BluetoothValue v;
  nsAutoString replyError;
  if (!GetDefaultAdapterPath(v, replyError)) {
    // Adapter path is not ready yet
    // Let's do PrepareAdapterTask when we receive signal 'AdapterAdded'
  } else {
    // Adapter path has been ready. let's do PrepareAdapterTask now
    nsRefPtr<PrepareAdapterTask> b = new PrepareAdapterTask(v.get_nsString());
    if (NS_FAILED(NS_DispatchToMainThread(b))) {
      NS_WARNING("Failed to dispatch to main thread!");
    }
  }

  return NS_OK;
}

nsresult
BluetoothDBusService::StopInternal()
{
  LOG("[B] %s", __FUNCTION__);
  // This could block. It should never be run on the main thread.
  MOZ_ASSERT(!NS_IsMainThread());

  // If Bluetooth is turned off while connections exist, in order not to only
  // disconnect with profile connections with low level ACL connections alive,
  // we disconnect ACLs directly instead of closing each socket.
  if (!mAdapterPath.IsEmpty()) {
    LOG("[B] mAdapterPath is not empty");
    DisconnectAllAcls(mAdapterPath);
  }

  if (!mConnection) {
    StopDBus();
    return NS_OK;
  }

  DBusError err;
  dbus_error_init(&err);
  for (uint32_t i = 0; i < ArrayLength(sBluetoothDBusSignals); ++i) {
    dbus_bus_remove_match(mConnection,
                          sBluetoothDBusSignals[i],
                          &err);
    if (dbus_error_is_set(&err)) {
      LOG_AND_FREE_DBUS_ERROR(&err);
    }
  }

  dbus_connection_remove_filter(mConnection, EventFilter, nullptr);

  if (!dbus_connection_unregister_object_path(
        mCommandThreadConnection->GetConnection(), KEY_LOCAL_AGENT)) {
    BT_WARNING("%s: Can't unregister object path %s for agent!",
        __FUNCTION__, KEY_LOCAL_AGENT);
  }

  if (!dbus_connection_unregister_object_path(
        mCommandThreadConnection->GetConnection(), KEY_REMOTE_AGENT)) {
    BT_WARNING("%s: Can't unregister object path %s for agent!",
        __FUNCTION__, KEY_REMOTE_AGENT);
  }

  mConnection = nullptr;
  mCommandThreadConnection = nullptr;

  // unref stored DBusMessages before clear the hashtable
  mPairingReqTable.EnumerateRead(UnrefDBusMessages, nullptr);
  mPairingReqTable.Clear();

  mAuthorizeReqTable.EnumerateRead(UnrefDBusMessages, nullptr);
  mAuthorizeReqTable.Clear();

  mIsPairing = 0;

  StopDBus();
  return NS_OK;
}

bool
BluetoothDBusService::IsEnabledInternal()
{
  return mEnabled;
}

nsresult
BluetoothDBusService::GetDefaultAdapterPathInternal(
                                              BluetoothReplyRunnable* aRunnable)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  CHECK_SERVICE_STATUS(aRunnable, NS_ERROR_FAILURE);

  nsRefPtr<BluetoothReplyRunnable> runnable = aRunnable;
  nsRefPtr<nsRunnable> func(new DefaultAdapterPropertiesRunnable(runnable));
  if (NS_FAILED(mBluetoothCommandThread->Dispatch(func, NS_DISPATCH_NORMAL))) {
    NS_WARNING("Cannot dispatch firmware loading task!");
    return NS_ERROR_FAILURE;
  }

  runnable.forget();
  return NS_OK;
}

nsresult
BluetoothDBusService::SendDiscoveryMessage(const char* aMessageName,
                                           BluetoothReplyRunnable* aRunnable)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  CHECK_SERVICE_STATUS(aRunnable, NS_ERROR_FAILURE);

  nsRefPtr<BluetoothReplyRunnable> runnable(aRunnable);

  bool success = dbus_func_args_async(mConnection, -1,
                                      OnSendDiscoveryMessageReply,
                                      static_cast<void*>(aRunnable),
                                      NS_ConvertUTF16toUTF8(mAdapterPath).get(),
                                      DBUS_ADAPTER_IFACE, aMessageName,
                                      DBUS_TYPE_INVALID);
  NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);

  runnable.forget();

  return NS_OK;
}

nsresult
BluetoothDBusService::SendSinkMessage(const nsAString& aDeviceAddress,
                                      const nsAString& aMessage)
{
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_TRUE(this->IsReady(), NS_ERROR_FAILURE);

  DBusCallback callback;
  if (aMessage.EqualsLiteral("Connect")) {
    callback = OnSendSinkConnectReply;
  } else if (aMessage.EqualsLiteral("Disconnect")) {
    callback = OnSendSinkDisconnectReply;
  } else {
    BT_WARNING("Unknown sink message");
    return NS_ERROR_FAILURE;
  }

  nsString objectPath = GetObjectPathFromAddress(mAdapterPath, aDeviceAddress);
  bool ret = dbus_func_args_async(mConnection,
                                  -1,
                                  callback,
                                  nullptr,
                                  NS_ConvertUTF16toUTF8(objectPath).get(),
                                  DBUS_SINK_IFACE,
                                  NS_ConvertUTF16toUTF8(aMessage).get(),
                                  DBUS_TYPE_INVALID);

  NS_ENSURE_TRUE(ret, NS_ERROR_FAILURE);
  return NS_OK;
}

nsresult
BluetoothDBusService::StopDiscoveryInternal(BluetoothReplyRunnable* aRunnable)
{
  return SendDiscoveryMessage("StopDiscovery", aRunnable);
}

nsresult
BluetoothDBusService::StartDiscoveryInternal(BluetoothReplyRunnable* aRunnable)
{
  return SendDiscoveryMessage("StartDiscovery", aRunnable);
}

nsresult
BluetoothDBusService::GetConnectedDevicePropertiesInternal(uint16_t aProfileId,
                                              BluetoothReplyRunnable* aRunnable)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  CHECK_SERVICE_STATUS(aRunnable, NS_ERROR_FAILURE);

  nsTArray<nsString> deviceAddresses;
  BluetoothProfileManagerBase* profile;
  nsAutoString errorStr;
  BluetoothValue values = InfallibleTArray<BluetoothNamedValue>();
  if (aProfileId == BluetoothServiceClass::HANDSFREE ||
      aProfileId == BluetoothServiceClass::HEADSET) {
    profile = BluetoothHfpManager::Get();
  } else if (aProfileId == BluetoothServiceClass::OBJECT_PUSH) {
    profile = BluetoothOppManager::Get();
  } else {
    DispatchBluetoothReply(aRunnable, values,
                           NS_LITERAL_STRING(ERR_UNKNOWN_PROFILE));
    return NS_OK;
  }

  if (profile->IsConnected()) {
    nsString address;
    profile->GetAddress(address);
    deviceAddresses.AppendElement(address);
  }

  nsRefPtr<BluetoothReplyRunnable> runnable = aRunnable;
  nsRefPtr<nsRunnable> func(
    new BluetoothArrayOfDevicePropertiesRunnable(deviceAddresses,
                                                 runnable,
                                                 GetConnectedDevicesFilter));
  mBluetoothCommandThread->Dispatch(func, NS_DISPATCH_NORMAL);

  runnable.forget();
  return NS_OK;
}

nsresult
BluetoothDBusService::GetPairedDevicePropertiesInternal(
                                     const nsTArray<nsString>& aDeviceAddresses,
                                     BluetoothReplyRunnable* aRunnable)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  CHECK_SERVICE_STATUS(aRunnable, NS_ERROR_FAILURE);

  nsRefPtr<BluetoothReplyRunnable> runnable = aRunnable;
  nsRefPtr<nsRunnable> func(
    new BluetoothArrayOfDevicePropertiesRunnable(aDeviceAddresses,
                                                 runnable,
                                                 GetPairedDevicesFilter));
  if (NS_FAILED(mBluetoothCommandThread->Dispatch(func, NS_DISPATCH_NORMAL))) {
    NS_WARNING("Cannot dispatch task!");
    return NS_ERROR_FAILURE;
  }

  runnable.forget();
  return NS_OK;
}

nsresult
BluetoothDBusService::SetProperty(BluetoothObjectType aType,
                                  const BluetoothNamedValue& aValue,
                                  BluetoothReplyRunnable* aRunnable)
{
  LOGV("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  CHECK_SERVICE_STATUS(aRunnable, NS_ERROR_FAILURE);

  MOZ_ASSERT(aType < ArrayLength(sBluetoothDBusIfaces));
  const char* interface = sBluetoothDBusIfaces[aType];

  /* Compose the command */
  DBusMessage* msg = dbus_message_new_method_call(
                                      "org.bluez",
                                      NS_ConvertUTF16toUTF8(mAdapterPath).get(),
                                      interface,
                                      "SetProperty");

  if (!msg) {
    NS_WARNING("Could not allocate D-Bus message object!");
    return NS_ERROR_FAILURE;
  }

  nsCString intermediatePropName(NS_ConvertUTF16toUTF8(aValue.name()));
  const char* propName = intermediatePropName.get();
  if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &propName,
                                DBUS_TYPE_INVALID)) {
    NS_WARNING("Couldn't append arguments to dbus message!");
    return NS_ERROR_FAILURE;
  }

  int type;
  int tmp_int;
  void* val;
  nsCString str;
  if (aValue.value().type() == BluetoothValue::Tuint32_t) {
    tmp_int = aValue.value().get_uint32_t();
    val = &tmp_int;
    type = DBUS_TYPE_UINT32;
  } else if (aValue.value().type() == BluetoothValue::TnsString) {
    str = NS_ConvertUTF16toUTF8(aValue.value().get_nsString());
    const char* tempStr = str.get();
    val = &tempStr;
    type = DBUS_TYPE_STRING;
  } else if (aValue.value().type() == BluetoothValue::Tbool) {
    tmp_int = aValue.value().get_bool() ? 1 : 0;
    val = &(tmp_int);
    type = DBUS_TYPE_BOOLEAN;
  } else {
    NS_WARNING("Property type not handled!");
    dbus_message_unref(msg);
    return NS_ERROR_FAILURE;
  }

  DBusMessageIter value_iter, iter;
  dbus_message_iter_init_append(msg, &iter);
  char var_type[2] = {(char)type, '\0'};
  if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, 
                                        var_type, &value_iter) ||
      !dbus_message_iter_append_basic(&value_iter, type, val) ||
      !dbus_message_iter_close_container(&iter, &value_iter)) {
    NS_WARNING("Could not append argument to method call!");
    dbus_message_unref(msg);
    return NS_ERROR_FAILURE;
  }

  nsRefPtr<BluetoothReplyRunnable> runnable = aRunnable;

  // msg is unref'd as part of dbus_func_send_async
  if (!dbus_func_send_async(mConnection,
                            msg,
                            1000,
                            OnSetPropertyReply,
                            (void*)aRunnable)) {
    NS_WARNING("Could not start async function!");
    return NS_ERROR_FAILURE;
  }
  runnable.forget();
  return NS_OK;
}

// static
bool
BluetoothDBusService::RemoveReservedServicesInternal(
                                      const nsTArray<uint32_t>& aServiceHandles)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(!NS_IsMainThread());
//  NS_ENSURE_TRUE(this->IsReady(), false);

  int length = aServiceHandles.Length();
  if (length == 0) return false;

  const uint32_t* services = aServiceHandles.Elements();

  DBusMessage* reply =
    dbus_func_args(mCommandThreadConnection->GetConnection(),
                   NS_ConvertUTF16toUTF8(mAdapterPath).get(),
                   DBUS_ADAPTER_IFACE, "RemoveReservedServiceRecords",
                   DBUS_TYPE_ARRAY, DBUS_TYPE_UINT32,
                   &services, length, DBUS_TYPE_INVALID);

  if (!reply) return false;

  dbus_message_unref(reply);
  return true;
}

nsresult
BluetoothDBusService::CreatePairedDeviceInternal(
                                              const nsAString& aDeviceAddress,
                                              int aTimeout,
                                              BluetoothReplyRunnable* aRunnable)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  CHECK_SERVICE_STATUS(aRunnable, NS_ERROR_FAILURE);

  const char *capabilities = B2G_AGENT_CAPABILITIES;
  const char *deviceAgentPath = KEY_REMOTE_AGENT;

  nsCString tempDeviceAddress = NS_ConvertUTF16toUTF8(aDeviceAddress);
  const char *deviceAddress = tempDeviceAddress.get();

  /**
   * FIXME: Bug 820274
   *
   * If the user turns off Bluetooth in the middle of pairing process, the
   * callback function GetObjectPathCallback (see the third argument of the
   * function call above) may still be called while enabling next time by
   * dbus daemon. To prevent this from happening, added a flag to distinguish
   * if Bluetooth has been turned off. Nevertheless, we need a check if there
   * is a better solution.
   *
   * Please see Bug 818696 for more information.
   */
  mIsPairing++;

  nsRefPtr<BluetoothReplyRunnable> runnable = aRunnable;
  // Then send CreatePairedDevice, it will register a temp device agent then
  // unregister it after pairing process is over
  bool ret = dbus_func_args_async(mConnection,
                                  aTimeout,
                                  OnCreatePairedDeviceReply,
                                  (void*)runnable,
                                  NS_ConvertUTF16toUTF8(mAdapterPath).get(),
                                  DBUS_ADAPTER_IFACE,
                                  "CreatePairedDevice",
                                  DBUS_TYPE_STRING, &deviceAddress,
                                  DBUS_TYPE_OBJECT_PATH, &deviceAgentPath,
                                  DBUS_TYPE_STRING, &capabilities,
                                  DBUS_TYPE_INVALID);
  if (!ret) {
    NS_WARNING("Could not start async function!");
    return NS_ERROR_FAILURE;
  }

  runnable.forget();
  return NS_OK;
}

nsresult
BluetoothDBusService::RemoveDeviceInternal(const nsAString& aDeviceAddress,
                                           BluetoothReplyRunnable* aRunnable)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  CHECK_SERVICE_STATUS(aRunnable, NS_ERROR_FAILURE);

  nsCString deviceObjectPath =
    NS_ConvertUTF16toUTF8(GetObjectPathFromAddress(mAdapterPath,
                                                   aDeviceAddress));
  const char* cstrDeviceObjectPath = deviceObjectPath.get();

  nsRefPtr<BluetoothReplyRunnable> runnable(aRunnable);

  bool success = dbus_func_args_async(mConnection, -1,
                                      OnRemoveDeviceReply,
                                      static_cast<void*>(runnable.get()),
                                      NS_ConvertUTF16toUTF8(mAdapterPath).get(),
                                      DBUS_ADAPTER_IFACE, "RemoveDevice",
                                      DBUS_TYPE_OBJECT_PATH, &cstrDeviceObjectPath,
                                      DBUS_TYPE_INVALID);
  NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);

  runnable.forget();

  return NS_OK;
}

bool
BluetoothDBusService::SetPinCodeInternal(const nsAString& aDeviceAddress,
                                         const nsAString& aPinCode,
                                         BluetoothReplyRunnable* aRunnable)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  CHECK_SERVICE_STATUS(aRunnable, false);

  DBusMessage *msg;
  if (!mPairingReqTable.Get(aDeviceAddress, &msg)) {
    BT_WARNING("%s: %s", __FUNCTION__, ERR_PAIRING_REQUEST_RETRIEVAL);
    DispatchBluetoothReply(aRunnable, BluetoothValue(),
                           NS_LITERAL_STRING(ERR_PAIRING_REQUEST_RETRIEVAL));
    return false;
  }

  DBusMessage *reply = dbus_message_new_method_return(msg);

  if (!reply) {
    BT_WARNING("%s: %s", __FUNCTION__, ERR_MEMORY_ALLOCATION);
    dbus_message_unref(msg);
    DispatchBluetoothReply(aRunnable, BluetoothValue(),
                           NS_LITERAL_STRING(ERR_MEMORY_ALLOCATION));
    return false;
  }

  bool result;

  nsCString tempPinCode = NS_ConvertUTF16toUTF8(aPinCode);
  const char* pinCode = tempPinCode.get();

  nsAutoString errorStr;
  if (!dbus_message_append_args(reply,
                                DBUS_TYPE_STRING, &pinCode,
                                DBUS_TYPE_INVALID)) {
    BT_WARNING("%s: Couldn't append arguments to dbus message.", __FUNCTION__);
    errorStr.AssignLiteral("Couldn't append arguments to dbus message.");
    result = false;
  } else {
    result = dbus_func_send(mConnection, nullptr, reply);
  }

  dbus_message_unref(msg);
  dbus_message_unref(reply);

  mPairingReqTable.Remove(aDeviceAddress);
  DispatchBluetoothReply(aRunnable, BluetoothValue(true), errorStr);
  return result;
}

bool
BluetoothDBusService::SetPasskeyInternal(const nsAString& aDeviceAddress,
                                         uint32_t aPasskey,
                                         BluetoothReplyRunnable* aRunnable)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  CHECK_SERVICE_STATUS(aRunnable, false);

  DBusMessage *msg;
  if (!mPairingReqTable.Get(aDeviceAddress, &msg)) {
    BT_WARNING("%s: %s", __FUNCTION__, ERR_PAIRING_REQUEST_RETRIEVAL);
    DispatchBluetoothReply(aRunnable, BluetoothValue(),
                           NS_LITERAL_STRING(ERR_PAIRING_REQUEST_RETRIEVAL));
    return false;
  }

  DBusMessage *reply = dbus_message_new_method_return(msg);

  if (!reply) {
    BT_WARNING("%s: %s", __FUNCTION__, ERR_MEMORY_ALLOCATION);
    dbus_message_unref(msg);
    DispatchBluetoothReply(aRunnable, BluetoothValue(),
                           NS_LITERAL_STRING(ERR_MEMORY_ALLOCATION));
    return false;
  }

  uint32_t passkey = aPasskey;
  bool result;

  nsAutoString errorStr;
  if (!dbus_message_append_args(reply,
                                DBUS_TYPE_UINT32, &passkey,
                                DBUS_TYPE_INVALID)) {
    BT_WARNING("%s: Couldn't append arguments to dbus message.", __FUNCTION__);
    errorStr.AssignLiteral("Couldn't append arguments to dbus message.");
    result = false;
  } else {
    result = dbus_func_send(mConnection, nullptr, reply);
  }

  dbus_message_unref(msg);
  dbus_message_unref(reply);

  mPairingReqTable.Remove(aDeviceAddress);
  DispatchBluetoothReply(aRunnable, BluetoothValue(true), errorStr);
  return result;
}

bool
BluetoothDBusService::SetPairingConfirmationInternal(
                                              const nsAString& aDeviceAddress,
                                              bool aConfirm,
                                              BluetoothReplyRunnable* aRunnable)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  CHECK_SERVICE_STATUS(aRunnable, false);

  DBusMessage *msg;
  if (!mPairingReqTable.Get(aDeviceAddress, &msg)) {
    BT_WARNING("%s: %s", __FUNCTION__, ERR_PAIRING_REQUEST_RETRIEVAL);
    DispatchBluetoothReply(aRunnable, BluetoothValue(),
                           NS_LITERAL_STRING(ERR_PAIRING_REQUEST_RETRIEVAL));
    return false;
  }

  DBusMessage *reply;

  if (aConfirm) {
    reply = dbus_message_new_method_return(msg);
  } else {
    reply = dbus_message_new_error(msg, "org.bluez.Error.Rejected",
                                   "User rejected confirmation");
  }

  if (!reply) {
    BT_WARNING("%s: %s", __FUNCTION__, ERR_MEMORY_ALLOCATION);
    dbus_message_unref(msg);
    DispatchBluetoothReply(aRunnable, BluetoothValue(),
                           NS_LITERAL_STRING(ERR_MEMORY_ALLOCATION));
    return false;
  }

  nsAutoString errorStr;
  bool result = dbus_func_send(mConnection, nullptr, reply);
  if (!result) {
    errorStr.AssignLiteral("Can't send message!");
  }
  dbus_message_unref(msg);
  dbus_message_unref(reply);

  mPairingReqTable.Remove(aDeviceAddress);
  DispatchBluetoothReply(aRunnable, BluetoothValue(true), errorStr);
  return result;
}

bool
BluetoothDBusService::SetAuthorizationInternal(
                                              const nsAString& aDeviceAddress,
                                              bool aAllow,
                                              BluetoothReplyRunnable* aRunnable)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  CHECK_SERVICE_STATUS(aRunnable, false);

  DBusMessage *msg;
  if (!mAuthorizeReqTable.Get(aDeviceAddress, &msg)) {
    BT_WARNING("%s: %s", __FUNCTION__, ERR_PAIRING_REQUEST_RETRIEVAL);
    DispatchBluetoothReply(aRunnable, BluetoothValue(),
                           NS_LITERAL_STRING(ERR_PAIRING_REQUEST_RETRIEVAL));
    return false;
  }

  DBusMessage *reply;
  if (aAllow) {
    reply = dbus_message_new_method_return(msg);
  } else {
    reply = dbus_message_new_error(msg, "org.bluez.Error.Rejected",
                                   "User rejected authorization");
  }

  if (!reply) {
    BT_WARNING("%s: %s", __FUNCTION__, ERR_MEMORY_ALLOCATION);
    dbus_message_unref(msg);
    DispatchBluetoothReply(aRunnable, BluetoothValue(),
                           NS_LITERAL_STRING(ERR_MEMORY_ALLOCATION));
    return false;
  }

  bool result = dbus_func_send(mConnection, nullptr, reply);
  nsAutoString errorStr;
  if (!result) {
    errorStr.AssignLiteral("Can't send message!");
  }
  dbus_message_unref(msg);
  dbus_message_unref(reply);

  mAuthorizeReqTable.Remove(aDeviceAddress);
  DispatchBluetoothReply(aRunnable, BluetoothValue(), errorStr);
  return result;
}

void
BluetoothDBusService::Connect(const nsAString& aDeviceAddress,
                              const uint16_t aProfileId,
                              BluetoothReplyRunnable* aRunnable)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  CHECK_SERVICE_STATUS_VOID(aRunnable);

  if (aProfileId == BluetoothServiceClass::HANDSFREE) {
    BluetoothHfpManager* hfp = BluetoothHfpManager::Get();
    hfp->Connect(aDeviceAddress, true, aRunnable);
  } else if (aProfileId == BluetoothServiceClass::HEADSET) {
    BluetoothHfpManager* hfp = BluetoothHfpManager::Get();
    hfp->Connect(aDeviceAddress, false, aRunnable);
  } else if (aProfileId == BluetoothServiceClass::OBJECT_PUSH) {
    BluetoothOppManager* opp = BluetoothOppManager::Get();
    opp->Connect(aDeviceAddress, aRunnable);
  } else {
    DispatchBluetoothReply(aRunnable, BluetoothValue(),
                           NS_LITERAL_STRING(ERR_UNKNOWN_PROFILE));
  }
}

void
BluetoothDBusService::Disconnect(const uint16_t aProfileId,
                                 BluetoothReplyRunnable* aRunnable)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  CHECK_SERVICE_STATUS_VOID(aRunnable);

  if (aProfileId == BluetoothServiceClass::HANDSFREE ||
      aProfileId == BluetoothServiceClass::HEADSET) {
    BluetoothHfpManager* hfp = BluetoothHfpManager::Get();
    hfp->Disconnect();
  } else if (aProfileId == BluetoothServiceClass::OBJECT_PUSH) {
    BluetoothOppManager* opp = BluetoothOppManager::Get();
    opp->Disconnect();
  } else {
    BT_WARNING(ERR_UNKNOWN_PROFILE);
    return;
  }

  // Currently, just fire success because Disconnect() doesn't fail,
  // but we still make aRunnable pass into this function for future
  // once Disconnect will fail.
  DispatchBluetoothReply(aRunnable, BluetoothValue(true), EmptyString());
}

bool
BluetoothDBusService::IsConnected(const uint16_t aProfileId)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_TRUE(IsReady(), false);

  BluetoothProfileManagerBase* profile;
  if (aProfileId == BluetoothServiceClass::HANDSFREE ||
      aProfileId == BluetoothServiceClass::HEADSET) {
    profile = BluetoothHfpManager::Get();
  } else if (aProfileId == BluetoothServiceClass::OBJECT_PUSH) {
    profile = BluetoothOppManager::Get();
  } else {
    NS_WARNING(ERR_UNKNOWN_PROFILE);
    return false;
  }

  return profile->IsConnected();
}

nsresult
BluetoothDBusService::GetServiceChannel(const nsAString& aDeviceAddress,
                                        const nsAString& aServiceUuid,
                                        BluetoothProfileManagerBase* aManager)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mBluetoothCommandThread);
  NS_ENSURE_TRUE(IsReady(), NS_ERROR_FAILURE);
  LOG("[B] %s", __FUNCTION__);

  nsString objectPath(GetObjectPathFromAddress(mAdapterPath, aDeviceAddress));

  nsRefPtr<nsRunnable> r(new GetServiceChannelRunnable(objectPath,
                                                       aServiceUuid,
                                                       aManager));
  mBluetoothCommandThread->Dispatch(r, NS_DISPATCH_NORMAL);

  return NS_OK;
}

bool
BluetoothDBusService::UpdateSdpRecords(const nsAString& aDeviceAddress,
                                       BluetoothProfileManagerBase* aManager)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!aDeviceAddress.IsEmpty());
  MOZ_ASSERT(aManager);
  MOZ_ASSERT(mConnection);
  NS_ENSURE_TRUE(IsReady(), false);

  nsString objectPath(GetObjectPathFromAddress(mAdapterPath, aDeviceAddress));

  // I choose to use raw pointer here because this is going to be passed as an
  // argument into dbus_func_args_async() at once.
  OnUpdateSdpRecordsRunnable* callbackRunnable =
    new OnUpdateSdpRecordsRunnable(objectPath, aManager);

  return dbus_func_args_async(mConnection,
                              -1,
                              OnDiscoverServicesReply,
                              (void*)callbackRunnable,
                              NS_ConvertUTF16toUTF8(objectPath).get(),
                              DBUS_DEVICE_IFACE,
                              "DiscoverServices",
                              DBUS_TYPE_STRING, &EmptyCString(),
                              DBUS_TYPE_INVALID);
}

nsresult
BluetoothDBusService::GetScoSocket(const nsAString& aAddress,
                                   bool aAuth,
                                   bool aEncrypt,
                                   mozilla::ipc::UnixSocketConsumer* aConsumer)
{
  MOZ_ASSERT(NS_IsMainThread());
  LOG("[B] %s", __FUNCTION__);

  if (!mConnection || !mCommandThreadConnection) {
    NS_ERROR("Bluetooth service not started yet!");
    return NS_ERROR_FAILURE;
  }

  BluetoothUnixSocketConnector* c =
    new BluetoothUnixSocketConnector(BluetoothSocketType::SCO, -1,
                                     aAuth, aEncrypt);

  if (!aConsumer->ConnectSocket(c, NS_ConvertUTF16toUTF8(aAddress).get())) {
    nsAutoString replyError;
    replyError.AssignLiteral("SocketConnectionError");
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

void
BluetoothDBusService::SendFile(const nsAString& aDeviceAddress,
                               BlobParent* aBlobParent,
                               BlobChild* aBlobChild,
                               BluetoothReplyRunnable* aRunnable)
{
  MOZ_ASSERT(NS_IsMainThread());

  // Currently we only support one device sending one file at a time,
  // so we don't need aDeviceAddress here because the target device
  // has been determined when calling 'Connect()'. Nevertheless, keep
  // it for future use.
  BluetoothOppManager* opp = BluetoothOppManager::Get();
  NS_ENSURE_TRUE_VOID(opp);

  nsAutoString errorStr;
  if (!opp->SendFile(aDeviceAddress, aBlobParent)) {
    errorStr.AssignLiteral("Calling SendFile() failed");
  }

  DispatchBluetoothReply(aRunnable, BluetoothValue(true), errorStr);
}

void
BluetoothDBusService::StopSendingFile(const nsAString& aDeviceAddress,
                                      BluetoothReplyRunnable* aRunnable)
{
  MOZ_ASSERT(NS_IsMainThread());
  LOG("[B] %s", __FUNCTION__);

  // Currently we only support one device sending one file at a time,
  // so we don't need aDeviceAddress here because the target device
  // has been determined when calling 'Connect()'. Nevertheless, keep
  // it for future use.
  BluetoothOppManager* opp = BluetoothOppManager::Get();
  NS_ENSURE_TRUE_VOID(opp);

  nsAutoString errorStr;
  if (!opp->StopSendingFile()) {
    errorStr.AssignLiteral("Calling StopSendingFile() failed");
  }

  DispatchBluetoothReply(aRunnable, BluetoothValue(true), errorStr);
}

void
BluetoothDBusService::ConfirmReceivingFile(const nsAString& aDeviceAddress,
                                           bool aConfirm,
                                           BluetoothReplyRunnable* aRunnable)
{
  LOG("[B] %s", __FUNCTION__);
  NS_ASSERTION(NS_IsMainThread(), "Must be called from main thread!");

  // Currently we only support one device sending one file at a time,
  // so we don't need aDeviceAddress here because the target device
  // has been determined when calling 'Connect()'. Nevertheless, keep
  // it for future use.
  BluetoothOppManager* opp = BluetoothOppManager::Get();
  NS_ENSURE_TRUE_VOID(opp);

  nsAutoString errorStr;
  if (!opp->ConfirmReceivingFile(aConfirm)) {
    errorStr.AssignLiteral("Calling ConfirmReceivingFile() failed");
  }

  DispatchBluetoothReply(aRunnable, BluetoothValue(true), errorStr);
}

void
BluetoothDBusService::ConnectSco(BluetoothReplyRunnable* aRunnable)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());

  BluetoothHfpManager* hfp = BluetoothHfpManager::Get();
  NS_ENSURE_TRUE_VOID(hfp);
  if(!hfp->ConnectSco(aRunnable)) {
    NS_NAMED_LITERAL_STRING(replyError,
      "SCO socket exists or HFP is not connected");
    DispatchBluetoothReply(aRunnable, BluetoothValue(), replyError);
  }
}

void
BluetoothDBusService::DisconnectSco(BluetoothReplyRunnable* aRunnable)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());

  BluetoothHfpManager* hfp = BluetoothHfpManager::Get();
  NS_ENSURE_TRUE_VOID(hfp);
  if (hfp->DisconnectSco()) {
    DispatchBluetoothReply(aRunnable,
                           BluetoothValue(true), NS_LITERAL_STRING(""));
    return;
  }

  NS_NAMED_LITERAL_STRING(replyError,
    "SCO socket doesn't exist or HFP is not connected");
  DispatchBluetoothReply(aRunnable, BluetoothValue(), replyError);
}

void
BluetoothDBusService::IsScoConnected(BluetoothReplyRunnable* aRunnable)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());

  BluetoothHfpManager* hfp = BluetoothHfpManager::Get();
  NS_ENSURE_TRUE_VOID(hfp);
  DispatchBluetoothReply(aRunnable,
                         hfp->IsScoConnected(), EmptyString());
}

void
BluetoothDBusService::SendMetaData(const nsAString& aTitle,
                                   const nsAString& aArtist,
                                   const nsAString& aAlbum,
                                   uint32_t aMediaNumber,
                                   uint32_t aTotalMediaCount,
                                   uint32_t aDuration,
                                   BluetoothReplyRunnable* aRunnable)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  CHECK_SERVICE_STATUS_VOID(aRunnable);

  BluetoothA2dpManager* a2dp = BluetoothA2dpManager::Get();
  NS_ENSURE_TRUE_VOID(a2dp);

  if (!a2dp->IsConnected()) {
    DispatchBluetoothReply(aRunnable, BluetoothValue(),
                           NS_LITERAL_STRING(ERR_A2DP_IS_DISCONNECTED));
    return;
  } else if (!a2dp->IsAvrcpConnected()) {
    DispatchBluetoothReply(aRunnable, BluetoothValue(),
                           NS_LITERAL_STRING(ERR_AVRCP_IS_DISCONNECTED));
    return;
  }

  nsAutoString address;
  a2dp->GetAddress(address);
  nsString objectPath =
    GetObjectPathFromAddress(mAdapterPath, address);

  LOG("[B] aTitle: %s", NS_ConvertUTF16toUTF8(aTitle).get());
  LOG("[B] aArtist: %s", NS_ConvertUTF16toUTF8(aArtist).get());
  LOG("[B] aAlbum: %s", NS_ConvertUTF16toUTF8(aAlbum).get());
  LOG("[B] aMediaNumber: %d", aMediaNumber);
  LOG("[B] aTotalMediaCount: %d", aTotalMediaCount);
  LOG("[B] aDuration: %d", aDuration);
  nsCString tempTitle = NS_ConvertUTF16toUTF8(aTitle);
  nsCString tempArtist = NS_ConvertUTF16toUTF8(aArtist);
  nsCString tempAlbum = NS_ConvertUTF16toUTF8(aAlbum);
  nsCString tempMediaNumber, tempTotalMediaCount, tempDuration;
  tempMediaNumber.AppendInt(aMediaNumber);
  tempTotalMediaCount.AppendInt(aTotalMediaCount);
  tempDuration.AppendInt(aDuration);

  const char* title = tempTitle.get();
  const char* album = tempAlbum.get();
  const char* artist = tempArtist.get();
  const char* mediaNumber = tempMediaNumber.get();
  const char* totalMediaCount = tempTotalMediaCount.get();
  const char* duration = tempDuration.get();

  nsRefPtr<BluetoothReplyRunnable> runnable(aRunnable);
  LOG("[B] objectPath: %s", NS_ConvertUTF16toUTF8(objectPath).get());
  LOG("[B] title: %s", title);
  LOG("[B] album: %s", album);
  LOG("[B] artist: %s", artist);
  LOG("[B] mediaNumber: %s", mediaNumber);
  LOG("[B] totalMediaCount: %s", totalMediaCount);
  LOG("[B] duration: %s", duration);

  bool ret = dbus_func_args_async(mConnection,
                                  -1,
                                  OnControlReply,
                                  (void*)runnable,
                                  NS_ConvertUTF16toUTF8(objectPath).get(),
                                  DBUS_CTL_IFACE,
                                  "UpdateMetaData",
                                  DBUS_TYPE_STRING, &title,
                                  DBUS_TYPE_STRING, &artist,
                                  DBUS_TYPE_STRING, &album,
                                  DBUS_TYPE_STRING, &mediaNumber,
                                  DBUS_TYPE_STRING, &totalMediaCount,
                                  DBUS_TYPE_STRING, &duration,
                                  DBUS_TYPE_INVALID);
  NS_ENSURE_TRUE_VOID(ret);

  runnable.forget();

  uint32_t prevMediaNumber;
  a2dp->GetMediaNumber(&prevMediaNumber);
  nsAutoString prevTitle;
  a2dp->GetTitle(prevTitle);

  ControlEventId eventId = ControlEventId::EVENT_UNKNOWN;
  uint64_t data;
  if (aMediaNumber != prevMediaNumber || !aTitle.Equals(prevTitle)) {
    eventId = ControlEventId::EVENT_TRACK_CHANGED;
    data = aMediaNumber;
    UpdateNotification(eventId, data);
  }

  a2dp->UpdateMetaData(aTitle, aArtist, aAlbum,
                       aMediaNumber, aTotalMediaCount, aDuration);
}

static ControlPlayStatus
PlayStatusStringToControlPlayStatus(const nsAString& aPlayStatus)
{
  ControlPlayStatus playStatus = ControlPlayStatus::PLAYSTATUS_UNKNOWN;
  if (aPlayStatus.EqualsLiteral("STOPPED")) {
    playStatus = ControlPlayStatus::PLAYSTATUS_STOPPED;
  } if (aPlayStatus.EqualsLiteral("PLAYING")) {
    playStatus = ControlPlayStatus::PLAYSTATUS_PLAYING;
  } else if (aPlayStatus.EqualsLiteral("PAUSED")) {
    playStatus = ControlPlayStatus::PLAYSTATUS_PAUSED;
  } else if (aPlayStatus.EqualsLiteral("FWD_SEEK")) {
    playStatus = ControlPlayStatus::PLAYSTATUS_FWD_SEEK;
  } else if (aPlayStatus.EqualsLiteral("REV_SEEK")) {
    playStatus = ControlPlayStatus::PLAYSTATUS_REV_SEEK;
  } else if (aPlayStatus.EqualsLiteral("ERROR")) {
    playStatus = ControlPlayStatus::PLAYSTATUS_ERROR;
  }

  return playStatus;
}

void
BluetoothDBusService::SendPlayStatus(uint32_t aDuration,
                                     uint32_t aPosition,
                                     const nsAString& aPlayStatus,
                                     BluetoothReplyRunnable* aRunnable)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  CHECK_SERVICE_STATUS_VOID(aRunnable);

  ControlPlayStatus playStatus =
    PlayStatusStringToControlPlayStatus(aPlayStatus);
  if (playStatus ==  ControlPlayStatus::PLAYSTATUS_UNKNOWN) {
    DispatchBluetoothReply(aRunnable, BluetoothValue(),
                           NS_LITERAL_STRING("Invalid play status"));
    return;
  }

  BluetoothA2dpManager* a2dp = BluetoothA2dpManager::Get();
  NS_ENSURE_TRUE_VOID(a2dp);

  if (!a2dp->IsConnected()) {
    DispatchBluetoothReply(aRunnable, BluetoothValue(),
                           NS_LITERAL_STRING(ERR_A2DP_IS_DISCONNECTED));
    return;
  } else if (!a2dp->IsAvrcpConnected()) {
    DispatchBluetoothReply(aRunnable, BluetoothValue(),
                           NS_LITERAL_STRING(ERR_AVRCP_IS_DISCONNECTED));
    return;
  }

  nsAutoString address;
  a2dp->GetAddress(address);
  nsString objectPath =
    GetObjectPathFromAddress(mAdapterPath, address);

  nsRefPtr<BluetoothReplyRunnable> runnable(aRunnable);
  LOG("[B] objectPath: %s", NS_ConvertUTF16toUTF8(objectPath).get());
  LOG("[B] duration: %d", aDuration);
  LOG("[B] position: %d", aPosition);
  LOG("[B] playStatus: %s", NS_ConvertUTF16toUTF8(aPlayStatus).get());

  uint32_t tempPlayStatus = playStatus;
  bool ret = dbus_func_args_async(mConnection,
                                  -1,
                                  OnControlReply,
                                  (void*)runnable,
                                  NS_ConvertUTF16toUTF8(objectPath).get(),
                                  DBUS_CTL_IFACE,
                                  "UpdatePlayStatus",
                                  DBUS_TYPE_UINT32, &aDuration,
                                  DBUS_TYPE_UINT32, &aPosition,
                                  DBUS_TYPE_UINT32, &tempPlayStatus,
                                  DBUS_TYPE_INVALID);
  NS_ENSURE_TRUE_VOID(ret);

  runnable.forget();

  uint32_t prevPosition;
  a2dp->GetPosition(&prevPosition);
  ControlPlayStatus prevPlayStauts;
  a2dp->GetPlayStatus(&prevPlayStauts);

  ControlEventId eventId = ControlEventId::EVENT_UNKNOWN;
  uint64_t data;
  if (aPosition != prevPosition) {
    eventId = ControlEventId::EVENT_PLAYBACK_POS_CHANGED;
    data = aPosition;
  } else if (playStatus != prevPlayStauts) {
    eventId = ControlEventId::EVENT_PLAYBACK_STATUS_CHANGED;
    data = tempPlayStatus;
  }

  if (eventId != ControlEventId::EVENT_UNKNOWN) {
    UpdateNotification(eventId, data);
  }

  a2dp->UpdatePlayStatus(aDuration, aPosition, playStatus);
}

void
BluetoothDBusService::UpdatePlayStatus(uint32_t aDuration,
                                       uint32_t aPosition,
                                       ControlPlayStatus aPlayStatus)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_TRUE_VOID(this->IsReady());

  BluetoothA2dpManager* a2dp = BluetoothA2dpManager::Get();
  NS_ENSURE_TRUE_VOID(a2dp);
  MOZ_ASSERT(a2dp->IsConnected());
  MOZ_ASSERT(a2dp->IsAvrcpConnected());

  nsAutoString address;
  a2dp->GetAddress(address);
  nsString objectPath =
    GetObjectPathFromAddress(mAdapterPath, address);

  LOG("[B] objectPath: %s", NS_ConvertUTF16toUTF8(objectPath).get());
  LOG("[B] duration: %d", aDuration);
  LOG("[B] position: %d", aPosition);
  LOG("[B] playStatus: %d", aPlayStatus);

  uint32_t tempPlayStatus = aPlayStatus;
  bool ret = dbus_func_args_async(mConnection,
                                  -1,
                                  OnUpdatePlayStatusReply,
                                  nullptr,
                                  NS_ConvertUTF16toUTF8(objectPath).get(),
                                  DBUS_CTL_IFACE,
                                  "UpdatePlayStatus",
                                  DBUS_TYPE_UINT32, &aDuration,
                                  DBUS_TYPE_UINT32, &aPosition,
                                  DBUS_TYPE_UINT32, &tempPlayStatus,
                                  DBUS_TYPE_INVALID);
  NS_ENSURE_TRUE_VOID(ret);
}

void
BluetoothDBusService::UpdateNotification(ControlEventId aEventId,
                                         uint64_t aData)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_TRUE_VOID(this->IsReady());

  BluetoothA2dpManager* a2dp = BluetoothA2dpManager::Get();
  NS_ENSURE_TRUE_VOID(a2dp);
  MOZ_ASSERT(a2dp->IsConnected());
  MOZ_ASSERT(a2dp->IsAvrcpConnected());

  nsAutoString address;
  a2dp->GetAddress(address);
  nsString objectPath =
    GetObjectPathFromAddress(mAdapterPath, address);

  LOG("[B] objectPath: %s", NS_ConvertUTF16toUTF8(objectPath).get());
  LOG("[B] eventId: %d", aEventId);
  LOG("[B] data: %llu", aData);

  bool ret = dbus_func_args_async(mConnection,
                                  -1,
                                  OnControlReply,
                                  nullptr,
                                  NS_ConvertUTF16toUTF8(objectPath).get(),
                                  DBUS_CTL_IFACE,
                                  "UpdateNotification",
                                  DBUS_TYPE_UINT16, &aEventId,
                                  DBUS_TYPE_UINT64, &aData,
                                  DBUS_TYPE_INVALID);
  NS_ENSURE_TRUE_VOID(ret);
}
