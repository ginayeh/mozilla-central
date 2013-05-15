/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"

#include "BluetoothA2dpManager.h"

#include "BluetoothService.h"
#include "BluetoothSocket.h"
#include "BluetoothUtils.h"

#include "mozilla/dom/bluetooth/BluetoothTypes.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPtr.h"
#include "nsContentUtils.h"
#include "nsIAudioManager.h"
#include "nsIObserverService.h"
#include "nsISettingsService.h"
#include "nsRadioInterfaceLayer.h"

#define BLUETOOTH_A2DP_STATUS_CHANGED "bluetooth-a2dp-status-changed"

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
#define CR_LF "\xd\xa";

using namespace mozilla;
using namespace mozilla::ipc;
USING_BLUETOOTH_NAMESPACE

namespace {
  StaticAutoPtr<BluetoothA2dpManager> gBluetoothA2dpManager;
  StaticRefPtr<BluetoothA2dpManagerObserver> sA2dpObserver;
  bool gInShutdown = false;
} // anonymous namespace

class mozilla::dom::bluetooth::BluetoothA2dpManagerObserver : public nsIObserver
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  BluetoothA2dpManagerObserver()
  {
  }

  bool Init()
  {
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    MOZ_ASSERT(obs);
    if (NS_FAILED(obs->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, false))) {
      NS_WARNING("Failed to add shutdown observer!");
      return false;
    }

    return true;
  }

  bool Shutdown()
  {
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    if (!obs ||
        NS_FAILED(obs->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID))) {
      NS_WARNING("Can't unregister observers, or already unregistered!");
      return false;
    }

    return true;
  }

  ~BluetoothA2dpManagerObserver()
  {
    Shutdown();
  }
};

NS_IMETHODIMP
BluetoothA2dpManagerObserver::Observe(nsISupports* aSubject,
                                     const char* aTopic,
                                     const PRUnichar* aData)
{
  MOZ_ASSERT(gBluetoothA2dpManager);

  // XXX
  if (!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
//    return gBluetoothA2dpManager->HandleShutdown();
  }

  MOZ_ASSERT(false, "BluetoothA2dpManager got unexpected topic!");
  return NS_ERROR_UNEXPECTED;
}

NS_IMPL_ISUPPORTS1(BluetoothA2dpManagerObserver, nsIObserver)

BluetoothA2dpManager::BluetoothA2dpManager()
  : mPrevSinkState(SinkState::SINK_DISCONNECTED)
{
}

bool
BluetoothA2dpManager::Init()
{
  LOGV("[A2dp] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());

  sA2dpObserver = new BluetoothA2dpManagerObserver();
  if (!sA2dpObserver->Init()) {
    NS_WARNING("Cannot set up A2dp Observers!");
  }

  return true;
}

BluetoothA2dpManager::~BluetoothA2dpManager()
{
  Cleanup();
}

void
BluetoothA2dpManager::Cleanup()
{
  LOGV("[A2dp] %s", __FUNCTION__);
  sA2dpObserver->Shutdown();
  sA2dpObserver = nullptr;
}

SinkState
BluetoothA2dpManager::StatusStringToSinkState(const nsAString& aStatus)
{
  LOG("[A2dp] %s", __FUNCTION__);
  SinkState state;
  if (aStatus.EqualsLiteral("disconnected")) {
    state = SinkState::SINK_DISCONNECTED;
  } else if (aStatus.EqualsLiteral("connecting")) {
    state = SinkState::SINK_CONNECTING;
  } else if (aStatus.EqualsLiteral("connected")) {
    state = SinkState::SINK_CONNECTED;
  } else if (aStatus.EqualsLiteral("playing")) {
    state = SINK_PLAYING;
  } else if (aStatus.EqualsLiteral("disconnecting")) {
    state = SINK_DISCONNECTING;
  } else {
    NS_WARNING("Unknown sink status");
  }
  LOG("[A2dp] aStatus: %s, state: %d", NS_ConvertUTF16toUTF8(aStatus).get(), state);
  return state;
}

//static
BluetoothA2dpManager*
BluetoothA2dpManager::Get()
{
  LOGV("[A2dp] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());

  // If we already exist, exit early
  if (gBluetoothA2dpManager) {
    return gBluetoothA2dpManager;
  }

  // If we're in shutdown, don't create a new instance
  if (gInShutdown) {
    NS_WARNING("BluetoothA2dpManager can't be created during shutdown");
    return nullptr;
  }

  // Create new instance, register, return
  BluetoothA2dpManager* manager = new BluetoothA2dpManager();
  NS_ENSURE_TRUE(manager->Init(), nullptr);

  gBluetoothA2dpManager = manager;
  return gBluetoothA2dpManager;
}

nsresult
BluetoothA2dpManager::HandleShutdown()
{
  LOGV("[A2dp] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  gInShutdown = true;
  Disconnect();
  gBluetoothA2dpManager = nullptr;
  return NS_OK;
}

bool
BluetoothA2dpManager::Connect(const nsAString& aDeviceAddress)
{
  MOZ_ASSERT(NS_IsMainThread());
  LOG("[A2dp] %s", __FUNCTION__);

  if (gInShutdown) {
    NS_WARNING("Connect called while in shutdown!");
    return false;
  }

  LOG("[A2dp] mPrevSinkState: %d", mPrevSinkState);
  if (mPrevSinkState == SinkState::SINK_CONNECTED) {
//      mPrevSinkState == SinkState::SINK_CONNECTING) {
    NS_WARNING("BluetoothA2dpManager is connected");
    return false;
  }

  BluetoothService* bs = BluetoothService::Get();
  NS_ENSURE_TRUE(bs, false);
  nsresult rv = bs->SendSinkMessage(aDeviceAddress, NS_LITERAL_STRING("Connect"));

  mDeviceAddress = aDeviceAddress;
  mPrevSinkState = SinkState::SINK_CONNECTING;
  return NS_SUCCEEDED(rv);
}

void
BluetoothA2dpManager::Disconnect()
{
  LOG("[A2dp] %s", __FUNCTION__);
  if (mPrevSinkState == SinkState::SINK_DISCONNECTED) {
    NS_WARNING("BluetoothA2dpManager has been disconnected");
    return;
  }

  // FIXME
  if (!mPrevSinkState != SINK_CONNECTED) {
    NS_WARNING("BluetoothA2dpManager is not connected");
    mPrevSinkState = SinkState::SINK_DISCONNECTED;
    return;
  }

  BluetoothService* bs = BluetoothService::Get();
  NS_ENSURE_TRUE_VOID(bs);
  bs->SendSinkMessage(mDeviceAddress, NS_LITERAL_STRING("Disconnect"));

  mPrevSinkState = SinkState::SINK_DISCONNECTED;
}

void
BluetoothA2dpManager::HandleSinkPropertyChanged(const BluetoothSignal& aSignal)
{
  LOG("[A2dp] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  LOG("[A2dp] aSignal.path(): %s, mDeviceAddress: %s", NS_ConvertUTF16toUTF8(aSignal.path()).get(), NS_ConvertUTF16toUTF8(mDeviceAddress).get());
  if (mDeviceAddress.EqualsLiteral("")) {
    LOG("[A2dp] ignore '%s'", NS_ConvertUTF16toUTF8(aSignal.value().get_ArrayOfBluetoothNamedValue()[0].name()).get());
  }
  MOZ_ASSERT(aSignal.value().type() == BluetoothValue::TArrayOfBluetoothNamedValue);

  const InfallibleTArray<BluetoothNamedValue>& arr =
    aSignal.value().get_ArrayOfBluetoothNamedValue();
  MOZ_ASSERT(arr.Length() == 1);

  const nsString& name = arr[0].name();
  const BluetoothValue& value = arr[0].value();
  LOG("[A2dp] name: %s", NS_ConvertUTF16toUTF8(name).get());
  if (name.EqualsLiteral("Connected")) {
    MOZ_ASSERT(value.type() == BluetoothValue::Tbool);
    mConnected = value.get_bool();
    LOG("[A2dp] mConnected: %d", mConnected);
    // Indicates if a stream is setup to a A2DP sink on the remote device.
  } else if (name.EqualsLiteral("Playing")) {
    MOZ_ASSERT(value.type() == BluetoothValue::Tbool);
    mPlaying = value.get_bool();
    // Indicates if a stream is active to a A2DP sink on the remote device.
  } else if (name.EqualsLiteral("State")) {
    MOZ_ASSERT(value.type() == BluetoothValue::TnsString);
    SinkState state = StatusStringToSinkState(value.get_nsString());
    if (state) {
      HandleSinkStateChanged(state);
    }
  } else {
    NS_WARNING("Unknown sink property");
  }
}

/* HandleSinkPropertyChanged update sink state in A2dp
 *
 * Possible values: "disconnected", "connecting", "connected", "playing"
 *
 * 1. "disconnected" -> "connecting"
 * Either an incoming or outgoing connection attempt ongoing
 * 2. "connecting" -> "disconnected"
 * Connection attempt failed
 * 3. "connecting" -> "connected"
 * Successfully connected
 * 4. "connected" -> "playing"
 * Audio stream active
 * 5. "playing" -> "connected"
 * Audio stream suspended
 * 6. "connected" -> "disconnected"
 *    "playing" -> "disconnected"
 * Disconnected from the remote device
 * 7. "disconnecting" -> "disconnected"
 * Disconnected from local
 */
void
BluetoothA2dpManager::HandleSinkStateChanged(SinkState aState)
{
  LOG("[A2dp] %s, %d -> %d", __FUNCTION__, mPrevSinkState, aState);
  switch (aState) {
    case SinkState::SINK_DISCONNECTED:
      mDeviceAddress.Truncate();
      switch (mPrevSinkState) {
        case SinkState::SINK_CONNECTED:
        case SinkState::SINK_PLAYING:
          // Disconnected from the remote device

          break;
        case SinkState::SINK_DISCONNECTING:
          // Disconnected from local
        case SinkState::SINK_CONNECTING:
          // Connection attempt Failed

          break;
        default:
        break;
      }
      break;
    case SinkState::SINK_CONNECTING:
      LOG("[A2dp] mPrevSinkState: %d", mPrevSinkState);
/*      MOZ_ASSERT(mPrevSinkState == SinkState::SINK_DISCONNECTED ||
                 mPrevSinkState == SinkState::SINK_DISCONNECTING);*/

      break;
    case SinkState::SINK_CONNECTED:
      switch (mPrevSinkState) {
        case SinkState::SINK_CONNECTING:
          // Successfully connected
          NotifyStatusChanged();
          NotifyAudioManager();
          break;
        case SinkState::SINK_PLAYING:
          // Audio stream suspended

          break;
        default:
        break;
      }
      break;
    case SinkState::SINK_PLAYING:
      MOZ_ASSERT(mPrevSinkState == SinkState::SINK_CONNECTED);

      break;
    case SinkState::SINK_DISCONNECTING:
      break;
    default:
      NS_WARNING("Unknown sink status");
      break;
  }

  mPrevSinkState = aState;
}

void
BluetoothA2dpManager::NotifyStatusChanged()
{
  LOG("[A2dp] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());

  NS_NAMED_LITERAL_STRING(type, BLUETOOTH_A2DP_STATUS_CHANGED);
  InfallibleTArray<BluetoothNamedValue> parameters;

  nsString name;
  name.AssignLiteral("connected");
  BluetoothValue v = mConnected;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  name.AssignLiteral("address");
  v = mDeviceAddress;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  if (!BroadcastSystemMessage(type, parameters)) {
    NS_WARNING("Failed to broadcast system message to settings");
    return;
  }
}

void
BluetoothA2dpManager::NotifyAudioManager()
{
  LOG("[A2dp] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIObserverService> obs =
    do_GetService("@mozilla.org/observer-service;1");
  NS_ENSURE_TRUE_VOID(obs);

  nsAutoString message;
  message.AppendLiteral("address=");
  message.Append(mDeviceAddress);
  message.AppendLiteral(",status=");
  // FIXME
//  message.AppendInt(mPrevSinkState == SinkState::SINK_CONNECTING);
  if (mPrevSinkState == SinkState::SINK_CONNECTING) {
    message.AppendLiteral("true");
  } else {
    message.AppendLiteral("false");
  }
  LOG("[A2dp] message: %s", NS_ConvertUTF16toUTF8(message).get());

  if (NS_FAILED(obs->NotifyObservers(nullptr,
                                     BLUETOOTH_A2DP_STATUS_CHANGED,
                                     message.BeginReading()))) {
    NS_WARNING("Failed to notify bluetooth-a2dp-status-changed observsers!");
  }
}
