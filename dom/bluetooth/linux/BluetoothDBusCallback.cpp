/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"
#include "BluetoothCommon.h"
#include "BluetoothDBusCallback.h"
#include "BluetoothDBusService.h"
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
ParsePropertyChange(DBusMessage* aMsg, BluetoothValue& aValue,
                    nsAString& aErrorStr, Properties* aPropertyTypes,
                    int aPropertyTypeLen)
{
  LOGV("[B] %s", __FUNCTION__);
  DBusMessageIter iter;
  DBusError err;
  int prop_index = -1;
  InfallibleTArray<BluetoothNamedValue> props;

  dbus_error_init(&err);
  if (!dbus_message_iter_init(aMsg, &iter)) {
    NS_WARNING("Can't create iterator!");
    return;
  }

  if (!GetProperty(iter, aPropertyTypes, aPropertyTypeLen,
      &prop_index, props)) {
    NS_WARNING("Can't get property!");
    aErrorStr.AssignLiteral("Can't get property!");
    return;
  }
  aValue = props;
}

void
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
UnpackPropertiesMessage(DBusMessage* aMsg, DBusError* aErr,
                        BluetoothValue& aValue, nsAString& aErrorStr,
                        Properties* aPropertyTypes,
                        int aPropertyTypeLen)
{
  LOGV("[B] %s", __FUNCTION__);
  if (!IsDBusMessageError(aMsg, aErr, aErrorStr) &&
      dbus_message_get_type(aMsg) == DBUS_MESSAGE_TYPE_METHOD_RETURN) {
    DBusMessageIter iter;
    if (!dbus_message_iter_init(aMsg, &iter)) {
      aErrorStr.AssignLiteral("Cannot create dbus message iter!");
    } else {
      ParseProperties(&iter, aValue, aErrorStr, aPropertyTypes,
                      aPropertyTypeLen);
    }
  }
}

void
UnpackAdapterPropertiesMessage(DBusMessage* aMsg, DBusError* aErr,
                               BluetoothValue& aValue, nsAString& aErrorStr)
{
  LOGV("[B] %s", __FUNCTION__);
  UnpackPropertiesMessage(aMsg, aErr, aValue, aErrorStr,
                          sAdapterProperties, ArrayLength(sAdapterProperties));
}

void
UnpackDevicePropertiesMessage(DBusMessage* aMsg, DBusError* aErr,
                              BluetoothValue& aValue, nsAString& aErrorStr)
{
  LOGV("[B] %s", __FUNCTION__);
  UnpackPropertiesMessage(aMsg, aErr, aValue, aErrorStr,
                          sDeviceProperties, ArrayLength(sDeviceProperties));
}

void
UnpackManagerPropertiesMessage(DBusMessage* aMsg, DBusError* aErr,
                               BluetoothValue& aValue, nsAString& aErrorStr)
{
  LOGV("[B] %s", __FUNCTION__);
  UnpackPropertiesMessage(aMsg, aErr, aValue, aErrorStr,
                          sManagerProperties, ArrayLength(sManagerProperties));
}

void
ParseDeviceProperties(DBusMessageIter* aIter, BluetoothValue& aValue,
                      nsAString& aErrorStr)
{
  ParseProperties(aIter, aValue, aErrorStr,
                  sDeviceProperties, ArrayLength(sDeviceProperties));
}

void
ParsePropertyChange(const char* aInterface, DBusMessage* aMsg,
                    BluetoothValue& aValue, nsAString& aErrorStr)
{
  LOGV("[B] %s", __FUNCTION__);
  Properties* propertiesType;
  int propertiesLength;
  if (strcmp(aInterface, DBUS_MANAGER_IFACE) == 0) {
    propertiesType = sManagerProperties;
    propertiesLength = ArrayLength(sManagerProperties);
  } else if (strcmp(aInterface, DBUS_ADAPTER_IFACE) == 0) {
    propertiesType = sAdapterProperties;
    propertiesLength = ArrayLength(sAdapterProperties);
  } else if (strcmp(aInterface, DBUS_DEVICE_IFACE) == 0) {
    propertiesType = sDeviceProperties;
    propertiesLength = ArrayLength(sDeviceProperties);
  } else if (strcmp(aInterface, DBUS_SINK_IFACE) == 0) {
    propertiesType = sSinkProperties;
    propertiesLength = ArrayLength(sSinkProperties);
  } else if (strcmp(aInterface, DBUS_CTL_IFACE) == 0) {
    propertiesType = sControlProperties;
    propertiesLength = ArrayLength(sControlProperties);
  } else {
    BT_WARNING("Unknown Interface");
    aErrorStr.AssignLiteral("Unknown Interface");
    return;
  }

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

  if (!GetProperty(iter, propertiesType, propertiesLength,
      &prop_index, props)) {
    BT_WARNING("Can't get property!");
    aErrorStr.AssignLiteral("Can't get property!");
    return;
  }
  aValue = props;
}

static void
RunDBusCallback(DBusMessage* aMsg, void* aBluetoothReplyRunnable,
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
    dont_AddRef(static_cast< BluetoothReplyRunnable* >(aBluetoothReplyRunnable));

  MOZ_ASSERT(replyRunnable, "Callback reply runnable is null!");

  nsAutoString replyError;
  BluetoothValue v;
  aFunc(aMsg, nullptr, v, replyError);
  DispatchBluetoothReply(replyRunnable, v, replyError);
}

void
OnCreatePairedDeviceReply(DBusMessage* aMsg, void* aBluetoothReplyRunnable)
{
  LOGV("[B] %s", __FUNCTION__);
  int32_t isPairing;
  BluetoothDBusService::GetIsPairing(&isPairing);
  if (isPairing) {
    RunDBusCallback(aMsg, aBluetoothReplyRunnable,
                    UnpackObjectPathMessage);
    isPairing--;
    BluetoothDBusService::SetIsPairing(isPairing);
  }
}

void
OnControlReply(DBusMessage* aMsg, void* aBluetoothReplyRunnable)
{
  LOGV("[B] %s", __FUNCTION__);
  RunDBusCallback(aMsg, aBluetoothReplyRunnable,
                  UnpackVoidMessage);
}

void
OnSetPropertyReply(DBusMessage* aMsg, void* aBluetoothReplyRunnable)
{
  LOGV("[B] %s", __FUNCTION__);
  RunDBusCallback(aMsg, aBluetoothReplyRunnable,
                  UnpackVoidMessage);
}

#ifdef DEBUG
static void
CheckForError(DBusMessage* aMsg, void *aParam, const nsAString& aError)
{
  LOG("[B] %s", __FUNCTION__);
  BluetoothValue v;
  nsAutoString replyError;
  UnpackVoidMessage(aMsg, nullptr, v, replyError);
  if (!v.get_bool()) {
    BT_WARNING(NS_ConvertUTF16toUTF8(aError).get());
  }
}
#endif

void
OnSendSinkConnectReply(DBusMessage* aMsg, void* aParam)
{
  LOG("[B] %s", __FUNCTION__);
#ifdef DEBUG
  NS_NAMED_LITERAL_STRING(errorStr, "Failed to connect sink");
  CheckForError(aMsg, aParam, errorStr);
#endif
}

void
OnSendSinkDisconnectReply(DBusMessage* aMsg, void* aParam)
{
  LOG("[B] %s", __FUNCTION__);
#ifdef DEBUG
  NS_NAMED_LITERAL_STRING(errorStr, "Failed to disconnect sink");
  CheckForError(false, aMsg, errorStr);
#endif
}

void
OnUpdatePlayStatusReply(DBusMessage* aMsg, void* aParam)
{
  LOG("[B] %s", __FUNCTION__);
#ifdef DEBUG
  NS_NAMED_LITERAL_STRING(errorStr, "Failed to update playstatus");
  CheckForError(aMsg, aParam, errorStr);
#endif
}

void
OnSendDiscoveryMessageReply(DBusMessage *aReply, void *aData)
{
  MOZ_ASSERT(!NS_IsMainThread());
  LOG("[B] %s", __FUNCTION__);

  nsAutoString errorStr;

  if (!aReply) {
    errorStr.AssignLiteral("SendDiscovery failed");
  }

  nsRefPtr<BluetoothReplyRunnable> runnable =
    dont_AddRef<BluetoothReplyRunnable>(static_cast<BluetoothReplyRunnable*>(aData));

  DispatchBluetoothReply(runnable.get(), BluetoothValue(true), errorStr);
}

void
OnDiscoverServicesReply(DBusMessage* aMsg, void* aData)
{
  LOG("[B] %s", __FUNCTION__);
  MOZ_ASSERT(!NS_IsMainThread());

  nsRefPtr<OnUpdateSdpRecordsRunnable> r(
      static_cast<OnUpdateSdpRecordsRunnable*>(aData));
  NS_DispatchToMainThread(r);
}

END_BLUETOOTH_NAMESPACE
