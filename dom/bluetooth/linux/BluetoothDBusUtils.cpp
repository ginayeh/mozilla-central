/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"
#include "BluetoothCommon.h"
#include "BluetoothDBusUtils.h"
#include "BluetoothReplyRunnable.h"
#include "BluetoothUtils.h"
#include "mozilla/dom/bluetooth/BluetoothTypes.h"
#include "mozilla/ipc/DBusUtils.h"
#include "nsString.h"

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

using namespace mozilla;
using namespace mozilla::ipc;
BEGIN_BLUETOOTH_NAMESPACE

Properties sManagerProperties[] = {
  {"Adapters", DBUS_TYPE_ARRAY},
};

Properties sAdapterProperties[] = {
  {"Address", DBUS_TYPE_STRING},
  {"Name", DBUS_TYPE_STRING},
  {"Class", DBUS_TYPE_UINT32},
  {"Powered", DBUS_TYPE_BOOLEAN},
  {"Discoverable", DBUS_TYPE_BOOLEAN},
  {"DiscoverableTimeout", DBUS_TYPE_UINT32},
  {"Pairable", DBUS_TYPE_BOOLEAN},
  {"PairableTimeout", DBUS_TYPE_UINT32},
  {"Discovering", DBUS_TYPE_BOOLEAN},
  {"Devices", DBUS_TYPE_ARRAY},
  {"UUIDs", DBUS_TYPE_ARRAY},
  {"Type", DBUS_TYPE_STRING}
};

Properties sDeviceProperties[] = {
  {"Address", DBUS_TYPE_STRING},
  {"Name", DBUS_TYPE_STRING},
  {"Icon", DBUS_TYPE_STRING},
  {"Class", DBUS_TYPE_UINT32},
  {"UUIDs", DBUS_TYPE_ARRAY},
  {"Paired", DBUS_TYPE_BOOLEAN},
  {"Connected", DBUS_TYPE_BOOLEAN},
  {"Trusted", DBUS_TYPE_BOOLEAN},
  {"Blocked", DBUS_TYPE_BOOLEAN},
  {"Alias", DBUS_TYPE_STRING},
  {"Nodes", DBUS_TYPE_ARRAY},
  {"Adapter", DBUS_TYPE_OBJECT_PATH},
  {"LegacyPairing", DBUS_TYPE_BOOLEAN},
  {"RSSI", DBUS_TYPE_INT16},
  {"TX", DBUS_TYPE_UINT32},
  {"Type", DBUS_TYPE_STRING},
  {"Broadcaster", DBUS_TYPE_BOOLEAN},
  {"Services", DBUS_TYPE_ARRAY}
};

Properties sSinkProperties[] = {
  {"State", DBUS_TYPE_STRING},
  {"Connected", DBUS_TYPE_BOOLEAN},
  {"Playing", DBUS_TYPE_BOOLEAN}
};

Properties sControlProperties[] = {
  {"Connected", DBUS_TYPE_BOOLEAN}
};

static const DBusObjectPathVTable agentVtable = {
  NULL, AgentEventFilter, NULL, NULL, NULL, NULL
};

static bool
IsDBusMessageError(DBusMessage* aMsg, DBusError* aErr, nsAString& aErrorStr)
{
  LOGV("[B] %s", __FUNCTION__);
  if (aErr && dbus_error_is_set(aErr)) {
    aErrorStr = NS_ConvertUTF8toUTF16(aErr->message);
    LOG_AND_FREE_DBUS_ERROR(aErr);
    return true;
  }

  DBusError err;
  dbus_error_init(&err);
  if (dbus_message_get_type(aMsg) == DBUS_MESSAGE_TYPE_ERROR) {
    const char* error_msg;
    if (!dbus_message_get_args(aMsg, &err, DBUS_TYPE_STRING,
          &error_msg, DBUS_TYPE_INVALID) ||
        !error_msg) {
      if (dbus_error_is_set(&err)) {
        aErrorStr = NS_ConvertUTF8toUTF16(err.message);
        LOG_AND_FREE_DBUS_ERROR(&err);
        return true;
      } else {
        aErrorStr.AssignLiteral("Unknown Error");
        return true;
      }
    } else {
      aErrorStr = NS_ConvertUTF8toUTF16(error_msg);
      return true;
    }
  }
  return false;
}

static bool
GetProperty(DBusMessageIter aIter, Properties* aPropertyTypes,
            int aPropertyTypeLen, int* aPropIndex,
            InfallibleTArray<BluetoothNamedValue>& aProperties)
{
  LOGV("[B] %s", __FUNCTION__);
  DBusMessageIter prop_val, array_val_iter;
  char* property = NULL;
  uint32_t array_type;
  int i, expectedType, receivedType;

  if (dbus_message_iter_get_arg_type(&aIter) != DBUS_TYPE_STRING) {
    return false;
  }

  dbus_message_iter_get_basic(&aIter, &property);

  if (!dbus_message_iter_next(&aIter) ||
      dbus_message_iter_get_arg_type(&aIter) != DBUS_TYPE_VARIANT) {
    return false;
  }

  for (i = 0; i < aPropertyTypeLen; i++) {
    if (!strncmp(property, aPropertyTypes[i].name, strlen(property))) {
      break;
    }
  }

  if (i == aPropertyTypeLen) {
    return false;
  }

  nsAutoString propertyName;
  propertyName.AssignASCII(aPropertyTypes[i].name);
  *aPropIndex = i;

  dbus_message_iter_recurse(&aIter, &prop_val);
  expectedType = aPropertyTypes[*aPropIndex].type;
  receivedType = dbus_message_iter_get_arg_type(&prop_val);

  /**
   * Bug 857896. Since device property "Connected" could be a boolean value or
   * an 2-byte array, we need to check the value type here and convert the
   * first byte into a boolean manually.
   */
  bool convert = false;
  if (propertyName.EqualsLiteral("Connected") &&
      receivedType == DBUS_TYPE_ARRAY) {
    convert = true;
  }

  if ((receivedType != expectedType) && !convert) {
    NS_WARNING("Iterator not type we expect!");
    nsCString str;
    str.AppendLiteral("Property Name: ");
    str.Append(NS_ConvertUTF16toUTF8(propertyName));
    str.AppendLiteral(", Property Type Expected: ");
    str.AppendInt(expectedType);
    str.AppendLiteral(", Property Type Received: ");
    str.AppendInt(receivedType);
    NS_WARNING(str.get());
    return false;
  }

  BluetoothValue propertyValue;
  switch (receivedType) {
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
      const char* c;
      dbus_message_iter_get_basic(&prop_val, &c);
      propertyValue = NS_ConvertUTF8toUTF16(c);
      break;
    case DBUS_TYPE_UINT32:
    case DBUS_TYPE_INT16:
      uint32_t i;
      dbus_message_iter_get_basic(&prop_val, &i);
      propertyValue = i;
      break;
    case DBUS_TYPE_BOOLEAN:
      bool b;
      dbus_message_iter_get_basic(&prop_val, &b);
      propertyValue = b;
      break;
    case DBUS_TYPE_ARRAY:
      dbus_message_iter_recurse(&prop_val, &array_val_iter);
      array_type = dbus_message_iter_get_arg_type(&array_val_iter);
      if (array_type == DBUS_TYPE_OBJECT_PATH ||
          array_type == DBUS_TYPE_STRING) {
        InfallibleTArray<nsString> arr;
        do {
          const char* tmp;
          dbus_message_iter_get_basic(&array_val_iter, &tmp);
          nsAutoString s;
          s = NS_ConvertUTF8toUTF16(tmp);
          arr.AppendElement(s);
        } while (dbus_message_iter_next(&array_val_iter));
        propertyValue = arr;
      } else if (array_type == DBUS_TYPE_BYTE) {
        InfallibleTArray<uint8_t> arr;
        do {
          uint8_t tmp;
          dbus_message_iter_get_basic(&array_val_iter, &tmp);
          arr.AppendElement(tmp);
        } while (dbus_message_iter_next(&array_val_iter));
        propertyValue = arr;
      } else {
        // This happens when the array is 0-length.
        // Apparently we get a DBUS_TYPE_INVALID type.
        propertyValue = InfallibleTArray<nsString>();
      }
      break;
    default:
      NS_NOTREACHED("Cannot find dbus message type!");
  }

  if (convert) {
    MOZ_ASSERT(propertyValue.type() == BluetoothValue::TArrayOfuint8_t);

    bool b = propertyValue.get_ArrayOfuint8_t()[0];
    propertyValue = BluetoothValue(b);
  }

  aProperties.AppendElement(BluetoothNamedValue(propertyName, propertyValue));
  return true;
}

static void
ParseProperties(DBusMessageIter* aIter, BluetoothValue& aValue,
                nsAString& aErrorStr, Properties* aPropertyTypes,
                int aPropertyTypeLen)
{
  LOGV("[B] %s", __FUNCTION__);
  DBusMessageIter dict_entry, dict;
  int prop_index = -1;

  MOZ_ASSERT(dbus_message_iter_get_arg_type(aIter) == DBUS_TYPE_ARRAY,
             "Trying to parse a property from sth. that's not an array");

  dbus_message_iter_recurse(aIter, &dict);
  InfallibleTArray<BluetoothNamedValue> props;
  do {
    MOZ_ASSERT(dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY,
               "Trying to parse a property from sth. that's not an dict!");
    dbus_message_iter_recurse(&dict, &dict_entry);

    if (!GetProperty(dict_entry, aPropertyTypes, aPropertyTypeLen,
        &prop_index, props)) {
      aErrorStr.AssignLiteral("Can't Create Property!");
      NS_WARNING("Can't create property!");
      return;
    }
  } while (dbus_message_iter_next(&dict));

  aValue = props;
}

static void
ParsePropertyChange(DBusMessage* aMsg, Properties* aPropType,
                    int aPropTypeLen, BluetoothValue& aValue,
                    nsAString& aErrorStr)
{
  LOGV("[B] %s", __FUNCTION__);

  int prop_index = -1;
  InfallibleTArray<BluetoothNamedValue> props;
  DBusMessageIter iter;
  DBusError err;
  dbus_error_init(&err);

  if (!dbus_message_iter_init(aMsg, &iter)) {
    NS_WARNING("Can't create iterator!");
    aErrorStr.AssignLiteral("Can't create iterator!");
    return;
  }

  if (!GetProperty(iter, aPropType, aPropTypeLen, &prop_index, props)) {
    BT_WARNING("Can't get property!");
    aErrorStr.AssignLiteral("Can't get property!");
    return;
  }
  aValue = props;
}

static void
UnpackVoidMessage(DBusMessage* aMsg, DBusError* aErr,
                  BluetoothValue& aValue, nsAString& aErrorStr)
{
  LOGV("[B] %s", __FUNCTION__);
  DBusError err;
  dbus_error_init(&err);
  if (!IsDBusMessageError(aMsg, aErr, aErrorStr) &&
      dbus_message_get_type(aMsg) == DBUS_MESSAGE_TYPE_METHOD_RETURN &&
      !dbus_message_get_args(aMsg, &err, DBUS_TYPE_INVALID)) {
    if (dbus_error_is_set(&err)) {
      aErrorStr = NS_ConvertUTF8toUTF16(err.message);
      LOG("[B] %s", err.message);
      LOG_AND_FREE_DBUS_ERROR(&err);
    }
  }
  aValue = aErrorStr.IsEmpty();
}

static void
UnpackObjectPathMessage(DBusMessage* aMsg, DBusError* aErr,
                        BluetoothValue& aValue, nsAString& aErrorStr)
{
  LOGV("[B] %s", __FUNCTION__);
  DBusError err;
  dbus_error_init(&err);
  if (!IsDBusMessageError(aMsg, aErr, aErrorStr)) {
    MOZ_ASSERT(dbus_message_get_type(aMsg) == DBUS_MESSAGE_TYPE_METHOD_RETURN,
               "Got dbus callback that's not a METHOD_RETURN!");
    const char* object_path;
    if (!dbus_message_get_args(aMsg, &err, DBUS_TYPE_OBJECT_PATH,
          &object_path, DBUS_TYPE_INVALID) ||
        !object_path) {
      if (dbus_error_is_set(&err)) {
        aErrorStr = NS_ConvertUTF8toUTF16(err.message);
        LOG_AND_FREE_DBUS_ERROR(&err);
      }
    } else {
      aValue = NS_ConvertUTF8toUTF16(object_path);
    }
  }
}

static void
UnpackPropertiesMessage(const char* aIface, DBusMessage* aMsg, DBusError* aErr,
                        BluetoothValue& aValue, nsAString& aErrorStr)
{
  LOGV("[B] %s", __FUNCTION__);
  Properties* propertiesType;
  int propertiesLength;

  if (strcmp(aIface, DBUS_MANAGER_IFACE) == 0) {
    propertiesType = sManagerProperties;
    propertiesLength = ArrayLength(sManagerProperties);
  } else if (strcmp(aIface, DBUS_ADAPTER_IFACE) == 0) {
    propertiesType = sAdapterProperties;
    propertiesLength = ArrayLength(sAdapterProperties);
  } else if (strcmp(aIface, DBUS_DEVICE_IFACE) == 0) {
    propertiesType = sDeviceProperties;
    propertiesLength = ArrayLength(sDeviceProperties);
  } else if (strcmp(aIface, DBUS_SINK_IFACE) == 0) {
    propertiesType = sSinkProperties;
    propertiesLength = ArrayLength(sSinkProperties);
  } else if (strcmp(aIface, DBUS_CTL_IFACE) == 0) {
    propertiesType = sControlProperties;
    propertiesLength = ArrayLength(sControlProperties);
  } else {
    BT_WARNING("Unknown Interface");
    aErrorStr.AssignLiteral("Unknown Interface");
    return;
  }

  if (!IsDBusMessageError(aMsg, aErr, aErrorStr) &&
      dbus_message_get_type(aMsg) == DBUS_MESSAGE_TYPE_METHOD_RETURN) {
    DBusMessageIter iter;
    if (!dbus_message_iter_init(aMsg, &iter)) {
      aErrorStr.AssignLiteral("Cannot create dbus message iter!");
    } else {
      ParseProperties(&iter, aValue, aErrorStr,
                      propertiesType, propertiesLength);
    }
  }
}

#ifdef DEBUG
static bool
UnpackReplyMessage(DBusMessage* aMsg)
{
  LOG("[B] %s", __FUNCTION__);
  BluetoothValue v;
  nsAutoString replyError;
  UnpackVoidMessage(aMsg, nullptr, v, replyError);
  if (!v.get_bool()) {
    BT_WARNING(NS_ConvertUTF16toUTF8(replyError).get());
  }
  return v.get_bool();
}
#endif

static void
UnpackReplyMessageAndReply(DBusMessage* aMsg,
                           void* aBluetoothReplyRunnable,
                           UnpackFunc aFunc)
{
  LOGV("[B] %s", __FUNCTION__);
#ifdef MOZ_WIDGET_GONK
  // Due to the fact that we're running two dbus loops on desktop implicitly by
  // being gtk based, sometimes we'll get signals/reply coming in on the main
  // thread. There's not a lot we can do about that for the time being and it
  // (technically) shouldn't hurt anything. However, on gonk, die.
  MOZ_ASSERT(!NS_IsMainThread());
#endif
  nsRefPtr<BluetoothReplyRunnable> replyRunnable =
    dont_AddRef(static_cast<BluetoothReplyRunnable*>(aBluetoothReplyRunnable));

  MOZ_ASSERT(replyRunnable, "Callback reply runnable is null!");

  BluetoothValue v;
  nsAutoString replyError;
  aFunc(aMsg, nullptr, v, replyError);
  DispatchBluetoothReply(replyRunnable, v, replyError);
}

// Local agent means agent for Adapter, not agent for Device. Some signals
// will be passed to local agent, some will be passed to device agent.
// For example, if a remote device would like to pair with us, then the
// signal will be passed to local agent. If we start pairing process with
// calling CreatePairedDevice, we'll get signal which should be passed to
// device agent.
static bool
RegisterLocalAgent(const char* adapterPath,
                   const char* agentPath,
                   const char* capabilities)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(!NS_IsMainThread());

  if (!dbus_connection_register_object_path(
        BluetoothDBusService::GetCommandThreadConnection(),
        agentPath,
        &agentVtable,
        NULL)) {
    BT_WARNING("%s: Can't register object path %s for agent!",
                __FUNCTION__, agentPath);
    return false;
  }

  DBusMessage* msg =
    dbus_message_new_method_call("org.bluez", adapterPath,
                                 DBUS_ADAPTER_IFACE, "RegisterAgent");
  if (!msg) {
    BT_WARNING("%s: Can't allocate new method call for agent!", __FUNCTION__);
    return false;
  }

  if (!dbus_message_append_args(msg,
                                DBUS_TYPE_OBJECT_PATH, &agentPath,
                                DBUS_TYPE_STRING, &capabilities,
                                DBUS_TYPE_INVALID)) {
    BT_WARNING("%s: Couldn't append arguments to dbus message.", __FUNCTION__);
    return false;
  }

  DBusError err;
  dbus_error_init(&err);

  DBusMessage* reply =
    dbus_connection_send_with_reply_and_block(
      BluetoothDBusService::GetCommandThreadConnection(), msg, -1, &err);
  dbus_message_unref(msg);

  if (!reply) {
    if (dbus_error_is_set(&err)) {
      if (!strcmp(err.name, "org.bluez.Error.AlreadyExists")) {
        LOG_AND_FREE_DBUS_ERROR(&err);
#ifdef DEBUG
        BT_WARNING("Agent already registered, still returning true");
#endif
      } else {
        LOG_AND_FREE_DBUS_ERROR(&err);
        BT_WARNING("%s: Can't register agent!", __FUNCTION__);
        return false;
      }
    }
  } else {
    dbus_message_unref(reply);
  }
  
  dbus_connection_flush(BluetoothDBusService::GetCommandThreadConnection());
  return true;
}

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

class AppendDeviceNameRunnable : public nsRunnable
{
public:
  AppendDeviceNameRunnable(const BluetoothSignal& aSignal)
    : mSignal(aSignal)
  {
  }

  nsresult Run()
  {
//    LOG("[B] AppendDeviceNameRunnable::Run");
    MOZ_ASSERT(!NS_IsMainThread());

    InfallibleTArray<BluetoothNamedValue>& arr =
      mSignal.value().get_ArrayOfBluetoothNamedValue();
    nsString devicePath = arr[0].value().get_nsString();

    // Replace object path with device address
    InfallibleTArray<BluetoothNamedValue>& parameters =
      mSignal.value().get_ArrayOfBluetoothNamedValue();
    nsString address = GetAddressFromObjectPath(devicePath);
    parameters[0].name().AssignLiteral("address");
    parameters[0].value() = address;

    BluetoothValue prop;
    if(!GetPropertiesInternal(devicePath, DBUS_DEVICE_IFACE, prop)) {
      return NS_ERROR_FAILURE;
    }

    // Get device name from result of GetPropertiesInternal and append to
    // original signal
    InfallibleTArray<BluetoothNamedValue>& properties =
      prop.get_ArrayOfBluetoothNamedValue();
    uint8_t i;
    for (i = 0; i < properties.Length(); i++) {
      if (properties[i].name().EqualsLiteral("Name")) {
        properties[i].name().AssignLiteral("name");
        parameters.AppendElement(properties[i]);
        break;
      }
    }
    MOZ_ASSERT_IF(i == properties.Length(), "failed to get device name");

    nsRefPtr<DistributeBluetoothSignalTask> task =
      new DistributeBluetoothSignalTask(mSignal);
    NS_DispatchToMainThread(task);

    return NS_OK;
  }

private:
  BluetoothSignal mSignal;
};

class AppendDeviceNameHandler : public nsRunnable
{
public:
  AppendDeviceNameHandler(const BluetoothSignal& aSignal)
    : mSignal(aSignal)
  {
  }

  nsresult Run()
  {
//    LOG("[B] AppendDeviceNameHandler::Run");
    MOZ_ASSERT(NS_IsMainThread());
    BluetoothService* bs = BluetoothService::Get();
    NS_ENSURE_TRUE(bs, NS_ERROR_FAILURE);

    BluetoothValue v = mSignal.value();
    if (v.type() != BluetoothValue::TArrayOfBluetoothNamedValue ||
        v.get_ArrayOfBluetoothNamedValue().Length() == 0) {
      NS_WARNING("Invalid argument type for AppendDeviceNameHandler");
      return NS_ERROR_FAILURE;
    }
    const InfallibleTArray<BluetoothNamedValue>& arr =
      v.get_ArrayOfBluetoothNamedValue();

    // Device object path should be put in the first element
    if (!arr[0].name().EqualsLiteral("path") ||
        arr[0].value().type() != BluetoothValue::TnsString) {
      NS_WARNING("Invalid object path for AppendDeviceNameHandler");
      return NS_ERROR_FAILURE;
    }

    nsRefPtr<nsRunnable> func(new AppendDeviceNameRunnable(mSignal));
    bs->DispatchToCommandThread(func);

    return NS_OK;
  }

private:
  BluetoothSignal mSignal;
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

void
OnCreatePairedDeviceReply(DBusMessage* aMsg, void* aBluetoothReplyRunnable)
{
  LOGV("[B] %s", __FUNCTION__);
  int32_t isPairing;
  BluetoothDBusService::GetIsPairing(&isPairing);
  if (isPairing) {
    UnpackReplyMessageAndReply(aMsg, aBluetoothReplyRunnable,
                               UnpackObjectPathMessage);
    isPairing--;
    BluetoothDBusService::SetIsPairing(isPairing);
  }
}

void
OnRemoveDeviceReply(DBusMessage *aMsg, void *aBluetoothReplyRunnable)
{
  LOGV("[B] %s", __FUNCTION__);
  UnpackReplyMessageAndReply(aMsg, aBluetoothReplyRunnable, UnpackVoidMessage);
}


void
OnControlReply(DBusMessage* aMsg, void* aBluetoothReplyRunnable)
{
  LOGV("[B] %s", __FUNCTION__);
  UnpackReplyMessageAndReply(aMsg, aBluetoothReplyRunnable, UnpackVoidMessage);
}

void
OnSetPropertyReply(DBusMessage* aMsg, void* aBluetoothReplyRunnable)
{
  LOGV("[B] %s", __FUNCTION__);
  UnpackReplyMessageAndReply(aMsg, aBluetoothReplyRunnable, UnpackVoidMessage);
}

void
OnSendSinkConnectReply(DBusMessage* aMsg, void* aParam)
{
  LOG("[B] %s", __FUNCTION__);
#ifdef DEBUG
  if (!UnpackReplyMessage(aMsg)) {
    BT_WARNING("Failed to connect sink");
  }
#endif
}

void
OnSendSinkDisconnectReply(DBusMessage* aMsg, void* aParam)
{
  LOG("[B] %s", __FUNCTION__);
#ifdef DEBUG
  if (!UnpackReplyMessage(aMsg)) {
    BT_WARNING("Failed to disconnect sink");
  }
#endif
}

void
OnUpdatePlayStatusReply(DBusMessage* aMsg, void* aParam)
{
  LOG("[B] %s", __FUNCTION__);
#ifdef DEBUG
  if (!UnpackReplyMessage(aMsg)) {
    BT_WARNING("Failed to update playstatus");
  }
#endif
}

void
OnSendDiscoveryMessageReply(DBusMessage *aMsg, void *aBluetoothReplyRunnable)
{
  LOG("[B] %s", __FUNCTION__);
  UnpackReplyMessageAndReply(aMsg, aBluetoothReplyRunnable, UnpackVoidMessage);
}

void
OnDiscoverServicesReply(DBusMessage* aMsg, void* aRunnable)
{
  LOG("[B] %s", __FUNCTION__);

  if (!UnpackReplyMessage(aMsg)) {
    BT_WARNING("Failed to discover services");
  }

  nsRefPtr<OnUpdateSdpRecordsRunnable> r(
    static_cast<OnUpdateSdpRecordsRunnable*>(aRunnable));
  NS_DispatchToMainThread(r);
}

bool
GetConnectedDevicesFilter(const InfallibleTArray<BluetoothNamedValue>& aProp)
{
  return true;
}

bool
GetPairedDevicesFilter(const InfallibleTArray<BluetoothNamedValue>& aProperties)
{
  uint8_t length = aProperties.Length();
  for (uint8_t p = 0; p < length; ++p) {
    if (aProperties[p].name().EqualsLiteral("Paired")) {
      return aProperties[p].value().get_bool();
    }
  }

  return false;
}

bool
GetPropertiesInternal(const nsAString& aPath,
                      const char* aIface,
                      BluetoothValue& aValue)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(!NS_IsMainThread());

  DBusError err;
  dbus_error_init(&err);

  DBusMessage* msg =
    dbus_func_args_timeout(BluetoothDBusService::GetCommandThreadConnection(),
                           1000,
                           &err,
                           NS_ConvertUTF16toUTF8(aPath).get(),
                           aIface,
                           "GetProperties",
                           DBUS_TYPE_INVALID);

  nsAutoString replyError;
  UnpackPropertiesMessage(aIface, msg, &err, aValue, replyError);

  if (!replyError.IsEmpty()) {
    NS_WARNING("Failed to get device properties");
    return false;
  }

  if (msg) {
    dbus_message_unref(msg);
  }

  return true;
}

bool
GetDefaultAdapterPath(BluetoothValue& aValue, nsString& aError)
{
  LOG("[B] %s", __FUNCTION__);
  // This could block. It should never be run on the main thread.
  MOZ_ASSERT(!NS_IsMainThread());

  DBusError err;
  dbus_error_init(&err);

  DBusMessage* msg =
    dbus_func_args_timeout(BluetoothDBusService::GetCommandThreadConnection(),
                           1000,
                           &err,
                           "/",
                           DBUS_MANAGER_IFACE,
                           "DefaultAdapter",
                           DBUS_TYPE_INVALID);

  UnpackObjectPathMessage(msg, &err, aValue, aError);

  if (msg) {
    dbus_message_unref(msg);
  }

  if (!aError.IsEmpty()) {
    return false;
  }

  return true;
}

bool
RegisterAgent()
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(!NS_IsMainThread());

  nsString adapterPath;
  BluetoothDBusService::GetAdapterPath(adapterPath);
  if (!RegisterLocalAgent(NS_ConvertUTF16toUTF8(adapterPath).get(),
                          KEY_LOCAL_AGENT,
                          B2G_AGENT_CAPABILITIES)) {
    return false;
  }

  // There is no "RegisterAgent" function defined in device interface.
  // When we call "CreatePairedDevice", it will do device agent registration
  // for us. (See maemo.org/api_refs/5.0/beta/bluez/adapter.html)
  if (!dbus_connection_register_object_path(
        BluetoothDBusService::GetCommandThreadConnection(),
        KEY_REMOTE_AGENT,
        &agentVtable,
        NULL)) {
    BT_WARNING("%s: Can't register object path %s for remote device agent!",
               __FUNCTION__, KEY_REMOTE_AGENT);

    return false;
  }

  return true;
}

int
GetDeviceServiceChannel(const nsAString& aObjectPath,
                        const nsAString& aPattern,
                        int aAttributeId)
{
  LOG("[B] %s", __FUNCTION__);
  // This is a blocking call, should not be run on main thread.
  MOZ_ASSERT(!NS_IsMainThread());

#ifdef MOZ_WIDGET_GONK
  // GetServiceAttributeValue only exists in android's bluez dbus binding
  // implementation
  nsCString tempPattern = NS_ConvertUTF16toUTF8(aPattern);
  const char* pattern = tempPattern.get();

  DBusMessage *reply =
    dbus_func_args(BluetoothDBusService::GetCommandThreadConnection(),
                   NS_ConvertUTF16toUTF8(aObjectPath).get(),
                   DBUS_DEVICE_IFACE, "GetServiceAttributeValue",
                   DBUS_TYPE_STRING, &pattern,
                   DBUS_TYPE_UINT16, &aAttributeId,
                   DBUS_TYPE_INVALID);

  return reply ? dbus_returns_int32(reply) : -1;
#else
  // FIXME/Bug 793977 qdot: Just return something for desktop, until we have a
  // parser for the GetServiceAttributes xml block
  return 1;
#endif
}

/**
 * When service class of CoD is "Audio" but major class is "TOY", the device
 * property Icon would be missed somehow. We'll re-assign its value to
 * "audio-card". This is for PTS test TC_AG_COD_BV_02_I.
 */
void
AppendDeviceIcon(InfallibleTArray<BluetoothNamedValue>& aProperties)
{
  uint32_t cod = 0x0;
  for (uint8_t i = 0; i < aProperties.Length(); i++) {
    if (aProperties[i].name().EqualsLiteral("Icon")) {
      return;
    } else if (aProperties[i].name().EqualsLiteral("Class")) {
      cod = aProperties[i].value().get_uint32_t();
    }
  }

  // Icon is missed and the service class contains "Audio"
  if ((cod & 0x200000) == 0x200000) {
    aProperties.AppendElement(
      BluetoothNamedValue(NS_LITERAL_STRING("Icon"),
                          NS_LITERAL_STRING("audio-card")));
  }
}

// Called by dbus during WaitForAndDispatchEventNative()
// This function is called on the IOThread
DBusHandlerResult
EventFilter(DBusConnection* aConn, DBusMessage* aMsg, void* aData)
{
  MOZ_ASSERT(!NS_IsMainThread(), "Shouldn't be called from Main Thread!");
  LOG("[B] %s", __FUNCTION__);

  if (dbus_message_get_type(aMsg) != DBUS_MESSAGE_TYPE_SIGNAL) {
    BT_WARNING("%s: event handler not interested in %s (not a signal).\n",
        __FUNCTION__, dbus_message_get_member(aMsg));
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  if (dbus_message_get_path(aMsg) == NULL) {
    BT_WARNING("DBusMessage %s has no bluetooth destination, ignoring\n",
               dbus_message_get_member(aMsg));
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  DBusError err;
  dbus_error_init(&err);

  nsAutoString signalPath;
  nsAutoString signalName;
  nsAutoString signalInterface;

  BT_LOG("%s: %s, %s, %s", __FUNCTION__,
                          dbus_message_get_interface(aMsg),
                          dbus_message_get_path(aMsg),
                          dbus_message_get_member(aMsg));

  signalInterface = NS_ConvertUTF8toUTF16(dbus_message_get_interface(aMsg));
  signalPath = NS_ConvertUTF8toUTF16(dbus_message_get_path(aMsg));
  signalName = NS_ConvertUTF8toUTF16(dbus_message_get_member(aMsg));
  nsString errorStr;
  BluetoothValue v;

  // Since the signalPath extracted from dbus message is a object path,
  // we'd like to re-assign them to corresponding key entry in
  // BluetoothSignalObserverTable
  if (signalInterface.EqualsLiteral(DBUS_MANAGER_IFACE)) {
    signalPath.AssignLiteral(KEY_MANAGER);
  } else if (signalInterface.EqualsLiteral(DBUS_ADAPTER_IFACE)) {
    signalPath.AssignLiteral(KEY_ADAPTER);
  } else if (signalInterface.EqualsLiteral(DBUS_DEVICE_IFACE)){
    signalPath = GetAddressFromObjectPath(signalPath);
  }

  if (dbus_message_is_signal(aMsg, DBUS_ADAPTER_IFACE, "DeviceFound")) {
    DBusMessageIter iter;

    if (!dbus_message_iter_init(aMsg, &iter)) {
      NS_WARNING("Can't create iterator!");
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    const char* addr;
    dbus_message_iter_get_basic(&iter, &addr);

    if (!dbus_message_iter_next(&iter)) {
      errorStr.AssignLiteral("Unexpected message struct in msg DeviceFound");
    } else {
      ParseProperties(&iter, v, errorStr,
                      sDeviceProperties, ArrayLength(sDeviceProperties));

      InfallibleTArray<BluetoothNamedValue>& properties =
        v.get_ArrayOfBluetoothNamedValue();
      AppendDeviceIcon(properties);

      // The DBus DeviceFound message actually passes back a key value object
      // with the address as the key and the rest of the device properties as
      // a dict value. After we parse out the properties, we need to go back
      // and add the address to the ipdl dict we've created to make sure we
      // have all of the information to correctly build the device.
      nsAutoString address = NS_ConvertUTF8toUTF16(addr);
      properties.AppendElement(
        BluetoothNamedValue(NS_LITERAL_STRING("Address"), address));
      properties.AppendElement(
        BluetoothNamedValue(NS_LITERAL_STRING("Path"),
                            GetObjectPathFromAddress(signalPath, address)));
    }
  } else if (dbus_message_is_signal(aMsg, DBUS_ADAPTER_IFACE,
                                    "DeviceDisappeared")) {
    const char* str;
    if (!dbus_message_get_args(aMsg, &err,
                               DBUS_TYPE_STRING, &str,
                               DBUS_TYPE_INVALID)) {
      LOG_AND_FREE_DBUS_ERROR_WITH_MSG(&err, aMsg);
      errorStr.AssignLiteral("Cannot parse device address!");
    } else {
      v = NS_ConvertUTF8toUTF16(str);
    }
  } else if (dbus_message_is_signal(aMsg, DBUS_ADAPTER_IFACE,
                                    "DeviceCreated")) {
    const char* str;
    if (!dbus_message_get_args(aMsg, &err,
                               DBUS_TYPE_OBJECT_PATH, &str,
                               DBUS_TYPE_INVALID)) {
      LOG_AND_FREE_DBUS_ERROR_WITH_MSG(&err, aMsg);
      errorStr.AssignLiteral("Cannot parse device path!");
    } else {
      v = NS_ConvertUTF8toUTF16(str);
    }
  } else if (dbus_message_is_signal(aMsg, DBUS_ADAPTER_IFACE,
                                    "DeviceRemoved")) {
    const char* str;
    if (!dbus_message_get_args(aMsg, &err,
                               DBUS_TYPE_OBJECT_PATH, &str,
                               DBUS_TYPE_INVALID)) {
      LOG_AND_FREE_DBUS_ERROR_WITH_MSG(&err, aMsg);
      errorStr.AssignLiteral("Cannot parse device path!");
    } else {
      v = NS_ConvertUTF8toUTF16(str);
    }
  } else if (dbus_message_is_signal(aMsg, DBUS_ADAPTER_IFACE,
                                    "PropertyChanged")) {
    ParsePropertyChange(aMsg, sAdapterProperties,
                        ArrayLength(sAdapterProperties), v, errorStr);
  } else if (dbus_message_is_signal(aMsg, DBUS_DEVICE_IFACE,
                                    "PropertyChanged")) {
    ParsePropertyChange(aMsg, sDeviceProperties,
                        ArrayLength(sDeviceProperties), v, errorStr);

    // Fire another task for sending system message of
    // "bluetooth-pairedstatuschanged"
    BluetoothNamedValue& property = v.get_ArrayOfBluetoothNamedValue()[0];
    if (property.name().EqualsLiteral("Paired")) {
      BluetoothValue newValue(v);
      ToLowerCase(newValue.get_ArrayOfBluetoothNamedValue()[0].name());
      BluetoothSignal signal(NS_LITERAL_STRING("PairedStatusChanged"),
                             NS_LITERAL_STRING(KEY_LOCAL_AGENT),
                             newValue);
      NS_DispatchToMainThread(new DistributeBluetoothSignalTask(signal));
    }
  } else if (dbus_message_is_signal(aMsg, DBUS_MANAGER_IFACE, "AdapterAdded")) {
    const char* str;
    if (!dbus_message_get_args(aMsg, &err,
                               DBUS_TYPE_OBJECT_PATH, &str,
                               DBUS_TYPE_INVALID)) {
      LOG_AND_FREE_DBUS_ERROR_WITH_MSG(&err, aMsg);
      errorStr.AssignLiteral("Cannot parse manager path!");
    } else {
      v = NS_ConvertUTF8toUTF16(str);
      NS_DispatchToMainThread(new PrepareAdapterTask(v.get_nsString()));
    }
  } else if (dbus_message_is_signal(aMsg, DBUS_MANAGER_IFACE,
                                    "PropertyChanged")) {
    ParsePropertyChange(aMsg, sManagerProperties,
                        ArrayLength(sManagerProperties), v, errorStr);
  } else if (dbus_message_is_signal(aMsg, DBUS_SINK_IFACE,
                                    "PropertyChanged")) {
    ParsePropertyChange(aMsg, sSinkProperties,
                        ArrayLength(sSinkProperties), v, errorStr);
  } else if (dbus_message_is_signal(aMsg, DBUS_CTL_IFACE, "GetPlayStatus")) {
    nsRefPtr<nsRunnable> task = new SendPlayStatusTask();
    NS_DispatchToMainThread(task);
    return DBUS_HANDLER_RESULT_HANDLED;
  } else if (dbus_message_is_signal(aMsg, DBUS_CTL_IFACE, "PropertyChanged")) {
    ParsePropertyChange(aMsg, sControlProperties,
                        ArrayLength(sControlProperties), v, errorStr);
  } else {
    errorStr = NS_ConvertUTF8toUTF16(dbus_message_get_member(aMsg));
    errorStr.AppendLiteral(" Signal not handled!");
  }

  if (!errorStr.IsEmpty()) {
    NS_WARNING(NS_ConvertUTF16toUTF8(errorStr).get());
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  BluetoothSignal signal(signalName, signalPath, v);
  nsRefPtr<nsRunnable> task;
  if (signalInterface.EqualsLiteral(DBUS_SINK_IFACE)) {
    task = new SinkPropertyChangedHandler(signal);
  } else if (signalInterface.EqualsLiteral(DBUS_CTL_IFACE) &&
             signalName.EqualsLiteral("PropertyChanged")) {
    task = new ControlPropertyChangedHandler(signal);
  } else {
    task = new DistributeBluetoothSignalTask(signal);
  }

  NS_DispatchToMainThread(task);

  return DBUS_HANDLER_RESULT_HANDLED;
}

DBusHandlerResult
AgentEventFilter(DBusConnection *conn, DBusMessage *msg, void *data)
{
  LOG("[B] %s", __FUNCTION__);
  if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL) {
    BT_WARNING("%s: agent handler not interested (not a method call).\n",
               __FUNCTION__);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  DBusError err;
  dbus_error_init(&err);

  LOG("%s: %s, %s", __FUNCTION__,
                    dbus_message_get_path(msg),
                    dbus_message_get_member(msg));

  nsString signalPath = NS_ConvertUTF8toUTF16(dbus_message_get_path(msg));
  nsString signalName = NS_ConvertUTF8toUTF16(dbus_message_get_member(msg));
  nsString errorStr;
  BluetoothValue v;
  InfallibleTArray<BluetoothNamedValue> parameters;
  nsRefPtr<nsRunnable> handler;
  bool isPairingReq = false;
  BluetoothSignal signal(signalName, signalPath, v);
  char *objectPath;

  // The following descriptions of each signal are retrieved from:
  //
  // http://maemo.org/api_refs/5.0/beta/bluez/agent.html
  //
  if (dbus_message_is_method_call(msg, DBUS_AGENT_IFACE, "Cancel")) {
    // This method gets called to indicate that the agent request failed before
    // a reply was returned.

    // Return directly
    DBusMessage *reply = dbus_message_new_method_return(msg);

    if (!reply) {
      errorStr.AssignLiteral(ERR_MEMORY_ALLOCATION);
      goto handle_error;
    }

    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
    v = parameters;
  } else if (dbus_message_is_method_call(msg, DBUS_AGENT_IFACE, "Authorize")) {
    // This method gets called when the service daemon needs to authorize a
    // connection/service request.
    const char *uuid;
    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_OBJECT_PATH, &objectPath,
                               DBUS_TYPE_STRING, &uuid,
                               DBUS_TYPE_INVALID)) {
      BT_WARNING("%s: Invalid arguments for Authorize() method", __FUNCTION__);
      errorStr.AssignLiteral("Invalid arguments for Authorize() method");
      goto handle_error;
    }

    nsString deviceAddress =
      GetAddressFromObjectPath(NS_ConvertUTF8toUTF16(objectPath));

    parameters.AppendElement(
      BluetoothNamedValue(NS_LITERAL_STRING("deviceAddress"), deviceAddress));
    parameters.AppendElement(
      BluetoothNamedValue(NS_LITERAL_STRING("uuid"),
                          NS_ConvertUTF8toUTF16(uuid)));

    BluetoothDBusService::PutAuthorizeRequest(deviceAddress, msg);

    v = parameters;
  } else if (dbus_message_is_method_call(msg, DBUS_AGENT_IFACE,
                                         "RequestConfirmation")) {
    // This method gets called when the service daemon needs to confirm a
    // passkey for an authentication.
    uint32_t passkey;
    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_OBJECT_PATH, &objectPath,
                               DBUS_TYPE_UINT32, &passkey,
                               DBUS_TYPE_INVALID)) {
      BT_WARNING("%s: Invalid arguments: RequestConfirmation()", __FUNCTION__);
      errorStr.AssignLiteral("Invalid arguments: RequestConfirmation()");
      goto handle_error;
    }

    parameters.AppendElement(
      BluetoothNamedValue(NS_LITERAL_STRING("path"),
                          NS_ConvertUTF8toUTF16(objectPath)));
    parameters.AppendElement(
      BluetoothNamedValue(NS_LITERAL_STRING("method"),
                          NS_LITERAL_STRING("confirmation")));
    parameters.AppendElement(
      BluetoothNamedValue(NS_LITERAL_STRING("passkey"), passkey));

    v = parameters;
    isPairingReq = true;
  } else if (dbus_message_is_method_call(msg, DBUS_AGENT_IFACE,
                                         "RequestPinCode")) {
    // This method gets called when the service daemon needs to get the passkey
    // for an authentication. The return value should be a string of 1-16
    // characters length. The string can be alphanumeric.
    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_OBJECT_PATH, &objectPath,
                               DBUS_TYPE_INVALID)) {
      BT_WARNING("%s: Invalid arguments for RequestPinCode() method",
                 __FUNCTION__);
      errorStr.AssignLiteral("Invalid arguments for RequestPinCode() method");
      goto handle_error;
    }

    parameters.AppendElement(
      BluetoothNamedValue(NS_LITERAL_STRING("path"),
                          NS_ConvertUTF8toUTF16(objectPath)));
    parameters.AppendElement(
      BluetoothNamedValue(NS_LITERAL_STRING("method"),
                          NS_LITERAL_STRING("pincode")));

    v = parameters;
    isPairingReq = true;
  } else if (dbus_message_is_method_call(msg, DBUS_AGENT_IFACE,
                                         "RequestPasskey")) {
    // This method gets called when the service daemon needs to get the passkey
    // for an authentication. The return value should be a numeric value
    // between 0-999999.
    if (!dbus_message_get_args(msg, NULL,
                               DBUS_TYPE_OBJECT_PATH, &objectPath,
                               DBUS_TYPE_INVALID)) {
      BT_WARNING("%s: Invalid arguments for RequestPasskey() method",
                 __FUNCTION__);
      errorStr.AssignLiteral("Invalid arguments for RequestPasskey() method");
      goto handle_error;
    }

    parameters.AppendElement(BluetoothNamedValue(
                               NS_LITERAL_STRING("path"),
                               NS_ConvertUTF8toUTF16(objectPath)));
    parameters.AppendElement(BluetoothNamedValue(
                               NS_LITERAL_STRING("method"),
                               NS_LITERAL_STRING("passkey")));

    v = parameters;
    isPairingReq = true;
  } else if (dbus_message_is_method_call(msg, DBUS_AGENT_IFACE, "Release")) {
    // This method gets called when the service daemon unregisters the agent.
    // An agent can use it to do cleanup tasks. There is no need to unregister
    // the agent, because when this method gets called it has already been
    // unregistered.
    DBusMessage *reply = dbus_message_new_method_return(msg);

    if (!reply) {
      errorStr.AssignLiteral(ERR_MEMORY_ALLOCATION);
      goto handle_error;
    }

    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);

    // Do not send an notification to upper layer, too annoying.
    return DBUS_HANDLER_RESULT_HANDLED;
  } else {
#ifdef DEBUG
    BT_WARNING("agent handler %s: Unhandled event. Ignore.", __FUNCTION__);
#endif
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  if (!errorStr.IsEmpty()) {
    NS_WARNING(NS_ConvertUTF16toUTF8(errorStr).get());
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  // Update value after parsing DBus message
  signal.value() = v;

  if (isPairingReq) {
    BluetoothDBusService::PutPairingRequest(
      GetAddressFromObjectPath(NS_ConvertUTF8toUTF16(objectPath)), msg);
    handler = new AppendDeviceNameHandler(signal);
  } else {
    handler = new DistributeBluetoothSignalTask(signal);
  }
  NS_DispatchToMainThread(handler);

  return DBUS_HANDLER_RESULT_HANDLED;

handle_error:
  NS_WARNING(NS_ConvertUTF16toUTF8(errorStr).get());
  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

PLDHashOperator
UnrefDBusMessages(const nsAString& key, DBusMessage* value, void* arg)
{
  LOGV("[B] %s", __FUNCTION__);
  dbus_message_unref(value);

  return PL_DHASH_NEXT;
}

END_BLUETOOTH_NAMESPACE
