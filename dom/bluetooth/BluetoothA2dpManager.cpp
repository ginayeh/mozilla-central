/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"

#include "BluetoothA2dpManager.h"

#include "BluetoothCommon.h"
#include "BluetoothService.h"
#include "BluetoothSocket.h"
#include "BluetoothUtils.h"

#include "mozilla/dom/bluetooth/BluetoothTypes.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPtr.h"
#include "nsIAudioManager.h"
#include "nsIObserverService.h"

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "GonkDBus", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

using namespace mozilla;
USING_BLUETOOTH_NAMESPACE

namespace {
  StaticRefPtr<BluetoothA2dpManager> gBluetoothA2dpManager;
  bool gInShutdown = false;
} // anonymous namespace

NS_IMETHODIMP
BluetoothA2dpManager::Observe(nsISupports* aSubject,
                              const char* aTopic,
                              const PRUnichar* aData)
{
  LOG("[A2dp] %s", __FUNCTION__);
  MOZ_ASSERT(gBluetoothA2dpManager);

  if (!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    HandleShutdown();
    return NS_OK;
  }

  MOZ_ASSERT(false, "BluetoothA2dpManager got unexpected topic!");
  return NS_ERROR_UNEXPECTED;
}

BluetoothA2dpManager::BluetoothA2dpManager()
  : mA2dpConnected(false)
  , mPlaying(false)
  , mSinkState(SinkState::SINK_DISCONNECTED)
  , mAvrcpConnected(false)
  , mDuration(0)
  , mMediaNumber(0)
  , mTotalMediaCount(0)
  , mPosition(0)
  , mPlayStatus(ControlPlayStatus::PLAYSTATUS_UNKNOWN)
{
}

bool
BluetoothA2dpManager::Init()
{
  LOG("[A2dp] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  NS_ENSURE_TRUE(obs, false);
  if (NS_FAILED(obs->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, false))) {
    BT_WARNING("Failed to add shutdown observer!");
    return false;
  }

  return true;
}

BluetoothA2dpManager::~BluetoothA2dpManager()
{
  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  NS_ENSURE_TRUE_VOID(obs);
  if (NS_FAILED(obs->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID))) {
    BT_WARNING("Failed to remove shutdown observer!");
  }
}

static BluetoothA2dpManager::SinkState
StatusStringToSinkState(const nsAString& aStatus)
{
  LOG("[A2dp] %s - '%s'", __FUNCTION__, NS_ConvertUTF16toUTF8(aStatus).get());
  BluetoothA2dpManager::SinkState state;
  if (aStatus.EqualsLiteral("disconnected")) {
    state = BluetoothA2dpManager::SinkState::SINK_DISCONNECTED;
  } else if (aStatus.EqualsLiteral("connecting")) {
    state = BluetoothA2dpManager::SinkState::SINK_CONNECTING;
  } else if (aStatus.EqualsLiteral("connected")) {
    state = BluetoothA2dpManager::SinkState::SINK_CONNECTED;
  } else if (aStatus.EqualsLiteral("playing")) {
    state = BluetoothA2dpManager::SinkState::SINK_PLAYING;
  } else if (aStatus.EqualsLiteral("disconnecting")) {
    state = BluetoothA2dpManager::SinkState::SINK_DISCONNECTING;
  } else {
    MOZ_ASSERT(false, "Unknown sink state");
  }
  return state;
}

//static
BluetoothA2dpManager*
BluetoothA2dpManager::Get()
{
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

void
BluetoothA2dpManager::HandleShutdown()
{
  LOG("[A2dp] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  gInShutdown = true;
  Disconnect();
  gBluetoothA2dpManager = nullptr;
}

bool
BluetoothA2dpManager::Connect(const nsAString& aDeviceAddress)
{
  LOG("[A2dp] %s, mA2dpConnected: %d", __FUNCTION__, mA2dpConnected);
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!aDeviceAddress.IsEmpty());

  if (gInShutdown) {
    NS_WARNING("Connect called while in shutdown!");
    return false;
  }

  if (mA2dpConnected) {
    NS_WARNING("BluetoothA2dpManager is connected");
    return false;
  }

  mDeviceAddress = aDeviceAddress;

  BluetoothService* bs = BluetoothService::Get();
  NS_ENSURE_TRUE(bs, false);
  nsresult rv = bs->SendSinkMessage(aDeviceAddress,
                                    NS_LITERAL_STRING("Connect"));

  return NS_SUCCEEDED(rv);
}

void
BluetoothA2dpManager::Disconnect()
{
  LOG("[A2dp] %s, mA2dpConnected: %d, mDeviceAddress: %s", __FUNCTION__, mA2dpConnected, NS_ConvertUTF16toUTF8(mDeviceAddress).get());

  if (!mA2dpConnected) {
    NS_WARNING("BluetoothA2dpManager has been disconnected");
    return;
  }

  MOZ_ASSERT(!mDeviceAddress.IsEmpty());

  BluetoothService* bs = BluetoothService::Get();
  NS_ENSURE_TRUE_VOID(bs);
  bs->SendSinkMessage(mDeviceAddress, NS_LITERAL_STRING("Disconnect"));
}

void
BluetoothA2dpManager::HandleSinkPropertyChanged(const BluetoothSignal& aSignal)
{
  LOG("[A2dp] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aSignal.value().type() == BluetoothValue::TArrayOfBluetoothNamedValue);

  const InfallibleTArray<BluetoothNamedValue>& arr =
    aSignal.value().get_ArrayOfBluetoothNamedValue();
  MOZ_ASSERT(arr.Length() == 1);

  const nsString& name = arr[0].name();
  const BluetoothValue& value = arr[0].value();
  if (name.EqualsLiteral("Connected")) {
    // Indicates if a stream is setup to a A2DP sink on the remote device.
    MOZ_ASSERT(value.type() == BluetoothValue::Tbool);
    mA2dpConnected = value.get_bool();
    NotifyStatusChanged();
    NotifyAudioManager();
  } else if (name.EqualsLiteral("Playing")) {
    // Indicates if a stream is active to a A2DP sink on the remote device.
    MOZ_ASSERT(value.type() == BluetoothValue::Tbool);
    mPlaying = value.get_bool();
  } else if (name.EqualsLiteral("State")) {
    MOZ_ASSERT(value.type() == BluetoothValue::TnsString);
    HandleSinkStateChanged(StatusStringToSinkState(value.get_nsString()));
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
  LOG("[A2dp] %s", __FUNCTION__);
  MOZ_ASSERT_IF(aState == SinkState::SINK_CONNECTED,
                mSinkState == SinkState::SINK_CONNECTING ||
                mSinkState == SinkState::SINK_PLAYING);
  MOZ_ASSERT_IF(aState == SinkState::SINK_PLAYING,
                mSinkState == SinkState::SINK_CONNECTED);

  if (aState == SinkState::SINK_DISCONNECTED) {
    mDeviceAddress.Truncate();
  }

  mSinkState = aState;
}

void
BluetoothA2dpManager::NotifyStatusChanged()
{
  LOG("[A2dp] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());

  NS_NAMED_LITERAL_STRING(type, BLUETOOTH_A2DP_STATUS_CHANGED_ID);
  InfallibleTArray<BluetoothNamedValue> parameters;

  BluetoothValue v = mA2dpConnected;
  parameters.AppendElement(
    BluetoothNamedValue(NS_LITERAL_STRING("connected"), v));

  v = mDeviceAddress;
  parameters.AppendElement(
    BluetoothNamedValue(NS_LITERAL_STRING("address"), v));

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

  nsAutoString data;
  data.AppendInt(mA2dpConnected);

  if (NS_FAILED(obs->NotifyObservers(this,
                                     BLUETOOTH_A2DP_STATUS_CHANGED_ID,
                                     data.BeginReading()))) {
    NS_WARNING("Failed to notify bluetooth-a2dp-status-changed observsers!");
  }
}

void
BluetoothA2dpManager::OnGetServiceChannel(const nsAString& aDeviceAddress,
                                          const nsAString& aServiceUuid,
                                          int aChannel)
{
}

void
BluetoothA2dpManager::OnUpdateSdpRecords(const nsAString& aDeviceAddress)
{
}

void
BluetoothA2dpManager::GetAddress(nsAString& aDeviceAddress)
{
  aDeviceAddress = mDeviceAddress;
}

bool
BluetoothA2dpManager::IsConnected()
{
  return mA2dpConnected;
}

void
BluetoothA2dpManager::SetAvrcpConnected(bool aConnected)
{
  mAvrcpConnected = aConnected;
}

bool
BluetoothA2dpManager::IsAvrcpConnected()
{
  return mAvrcpConnected;
}

void
BluetoothA2dpManager::UpdateMetaData(const nsAString& aTitle,
                                     const nsAString& aArtist,
                                     const nsAString& aAlbum,
                                     uint32_t aMediaNumber,
                                     uint32_t aTotalMediaCount,
                                     uint32_t aPosition)
{
  mTitle.Assign(aTitle);
  mArtist.Assign(aArtist);
  mAlbum.Assign(aAlbum);
  mMediaNumber = aMediaNumber;
  mTotalMediaCount = aTotalMediaCount;
  mPosition = aPosition; 
}

void
BluetoothA2dpManager::UpdatePlayStatus(uint32_t aDuration,
                                       uint32_t aPosition,
                                       ControlPlayStatus aPlayStatus)
{
  mDuration = aDuration;
  mPosition = aPosition;
  mPlayStatus = aPlayStatus;
}

void
BluetoothA2dpManager::GetPlayStatus(ControlPlayStatus* aPlayStatus)
{
  *aPlayStatus = mPlayStatus;
}

void
BluetoothA2dpManager::GetPosition(uint32_t* aPosition)
{
  *aPosition = mPosition;
}

void
BluetoothA2dpManager::GetTitle(nsAString& aTitle)
{
  aTitle.Assign(mTitle);
}

void
BluetoothA2dpManager::GetMediaNumber(uint32_t* aMediaNumber)
{
  *aMediaNumber = mMediaNumber;
}

NS_IMPL_ISUPPORTS1(BluetoothA2dpManager, nsIObserver)

