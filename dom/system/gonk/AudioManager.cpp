/* Copyright 2012 Mozilla Foundation and Mozilla contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <android/log.h>

#include "mozilla/Hal.h"
#include "AudioManager.h"
#include "nsIObserverService.h"
#include "mozilla/Services.h"
#include "AudioChannelService.h"

using namespace mozilla::dom::gonk;
using namespace android;
using namespace mozilla::hal;
using namespace mozilla;

#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "AudioManager" , ## args)

#define HEADPHONES_STATUS_CHANGED "headphones-status-changed"
#define HEADPHONES_STATUS_HEADSET   NS_LITERAL_STRING("headset").get()
#define HEADPHONES_STATUS_HEADPHONE NS_LITERAL_STRING("headphone").get()
#define HEADPHONES_STATUS_OFF       NS_LITERAL_STRING("off").get()
#define HEADPHONES_STATUS_UNKNOWN   NS_LITERAL_STRING("unknown").get()
#define BLUETOOTH_SCO_STATUS_CHANGED "bluetooth-sco-status-changed"
#define BLUETOOTH_A2DP_STATUS_CHANGED "bluetooth-a2dp-status-changed"

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "GonkDBus", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

// Refer AudioService.java from Android
static int sMaxStreamVolumeTbl[AUDIO_STREAM_CNT] = {
  5,   // voice call
  15,  // system
  15,  // ring
  15,  // music
  15,  // alarm
  15,  // notification
  15,  // BT SCO
  15,  // enforced audible
  15,  // DTMF
  15,  // TTS
  15,  // FM
};
// A bitwise variable for recording what kind of headset is attached.
static int sHeadsetState;
static int kBtSampleRate = 8000;

class RecoverTask : public nsRunnable
{
public:
  RecoverTask() {}
  NS_IMETHODIMP Run() {
    nsCOMPtr<nsIAudioManager> am = do_GetService(NS_AUDIOMANAGER_CONTRACTID);
    NS_ENSURE_TRUE(am, NS_OK);
    for (int i = 0; i < AUDIO_STREAM_CNT; i++) {
      AudioSystem::initStreamVolume(static_cast<audio_stream_type_t>(i), 0,
                                    sMaxStreamVolumeTbl[i]);
      int32_t volidx = 0;
      am->GetStreamVolumeIndex(i, &volidx);
      am->SetStreamVolumeIndex(static_cast<audio_stream_type_t>(i),
                               volidx);
    }
    if (sHeadsetState & AUDIO_DEVICE_OUT_WIRED_HEADSET)
      InternalSetAudioRoutes(SWITCH_STATE_HEADSET);
    else if (sHeadsetState & AUDIO_DEVICE_OUT_WIRED_HEADPHONE)
      InternalSetAudioRoutes(SWITCH_STATE_HEADPHONE);
    else
      InternalSetAudioRoutes(SWITCH_STATE_OFF);

    int32_t phoneState = nsIAudioManager::PHONE_STATE_INVALID;
    am->GetPhoneState(&phoneState);
#if ANDROID_VERSION < 17
    AudioSystem::setPhoneState(phoneState);
#else
    AudioSystem::setPhoneState(static_cast<audio_mode_t>(phoneState));
#endif

    AudioSystem::get_audio_flinger();
    return NS_OK;
  }
};

static void
BinderDeadCallback(status_t aErr)
{
  if (aErr == DEAD_OBJECT) {
    NS_DispatchToMainThread(new RecoverTask());
  }
}

static bool
IsDeviceOn(audio_devices_t device)
{
  if (static_cast<
      audio_policy_dev_state_t (*) (audio_devices_t, const char *)
      >(AudioSystem::getDeviceConnectionState))
    return AudioSystem::getDeviceConnectionState(device, "") ==
           AUDIO_POLICY_DEVICE_STATE_AVAILABLE;

  return false;
}

NS_IMPL_ISUPPORTS2(AudioManager, nsIAudioManager, nsIObserver)

static AudioSystem::audio_devices
GetRoutingMode(int aType) {
  if (aType == nsIAudioManager::FORCE_SPEAKER) {
    return AudioSystem::DEVICE_OUT_SPEAKER;
  } else if (aType == nsIAudioManager::FORCE_HEADPHONES) {
    return AudioSystem::DEVICE_OUT_WIRED_HEADSET;
  } else if (aType == nsIAudioManager::FORCE_BT_SCO) {
    return AudioSystem::DEVICE_OUT_BLUETOOTH_SCO;
  } else if (aType == nsIAudioManager::FORCE_BT_A2DP) {
    return AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP;
  } else {
    return AudioSystem::DEVICE_IN_DEFAULT;
  }
}

static void
InternalSetAudioRoutesICS(SwitchState aState)
{
  if (aState == SWITCH_STATE_HEADSET) {
    AudioSystem::setDeviceConnectionState(AUDIO_DEVICE_OUT_WIRED_HEADSET,
                                          AUDIO_POLICY_DEVICE_STATE_AVAILABLE, "");
    sHeadsetState |= AUDIO_DEVICE_OUT_WIRED_HEADSET;
  } else if (aState == SWITCH_STATE_HEADPHONE) {
    AudioSystem::setDeviceConnectionState(AUDIO_DEVICE_OUT_WIRED_HEADPHONE,
                                          AUDIO_POLICY_DEVICE_STATE_AVAILABLE, "");
    sHeadsetState |= AUDIO_DEVICE_OUT_WIRED_HEADPHONE;
  } else if (aState == SWITCH_STATE_OFF) {
    AudioSystem::setDeviceConnectionState(static_cast<audio_devices_t>(sHeadsetState),
                                          AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE, "");
    sHeadsetState = 0;
  }

  // The audio volume is not consistent when we plug and unplug the headset.
  // Set the fm volume again here.
  if (IsDeviceOn(AUDIO_DEVICE_OUT_FM)) {
    float masterVolume;
    AudioSystem::getMasterVolume(&masterVolume);
    AudioSystem::setFmVolume(masterVolume);
  }
}

static void
InternalSetAudioRoutesGB(SwitchState aState)
{
  audio_io_handle_t handle = 
    AudioSystem::getOutput((AudioSystem::stream_type)AudioSystem::SYSTEM);
  String8 cmd;

  if (aState == SWITCH_STATE_HEADSET || aState == SWITCH_STATE_HEADPHONE) {
    cmd.appendFormat("routing=%d", GetRoutingMode(nsIAudioManager::FORCE_HEADPHONES));
  } else if (aState == SWITCH_STATE_OFF) {
    cmd.appendFormat("routing=%d", GetRoutingMode(nsIAudioManager::FORCE_SPEAKER));
  }

  AudioSystem::setParameters(handle, cmd);
}

static void
InternalSetAudioRoutes(SwitchState aState)
{
  if (static_cast<
    status_t (*)(audio_devices_t, audio_policy_dev_state_t, const char*)
    >(AudioSystem::setDeviceConnectionState)) {
    InternalSetAudioRoutesICS(aState);
  } else if (static_cast<
    audio_io_handle_t (*)(AudioSystem::stream_type, uint32_t, uint32_t, uint32_t, AudioSystem::output_flags)
    >(AudioSystem::getOutput)) {
    InternalSetAudioRoutesGB(aState);
  }
}

static nsresult
ParseBluetoothStatusChagnedMessage(const nsACString& aMessage,
                                   nsACString& aRetAddress,
                                   bool* aRetStatus)
{
  LOG("[Audio] %s", __FUNCTION__);
  LOG("aMessage: %s", aMessage.BeginReading());
  /**
   * Example message:
   * "address=xx:xx:xx:xx:xx:xx,status=true"
   * "status=false,address=xx:xx:xx:xx:xx:xx"
   */

  // Parse string with ','
  int begin = 0;
  nsTArray<nsCString> commands;
  for (uint32_t i = begin; i < aMessage.Length(); ++i) {
    if (aMessage[i] == ',') {
      nsCString tmp(nsDependentCSubstring(aMessage, begin, i - begin));
      LOG("tmp: %s", tmp.BeginReading());
      commands.AppendElement(tmp);
      begin = i + 1;
    }
  }
  nsCString tmp(nsDependentCSubstring(aMessage, begin));
  LOG("tmp: %s", tmp.BeginReading());
  commands.AppendElement(tmp);

  // Extract device address and connection status from parsing result
  int length = commands.Length();
  MOZ_ASSERT(length == 2);
  for (int i = 0; i < length; i++) {
    nsCString command(commands[i]);
    int pos = command.FindChar('=');
    if (pos == -1) {
      NS_WARNING("Wrong format in BluetoothStatusChanged message");
      return NS_ERROR_FAILURE;
    }

    nsCString key(nsDependentCSubstring(command, 0, pos));
    nsCString value(nsDependentCSubstring(command, pos + 1, command.Length()));
    LOG("value: %s", value.BeginReading());
    if (key.EqualsLiteral("status")) {
      if (value.EqualsLiteral("true")) {
        *aRetStatus = 1;
      } else if (value.EqualsLiteral("false")) {
        *aRetStatus = 0;
      } else {    
        NS_WARNING("Unknown value in BluetoothStatusChanged message");
        return NS_ERROR_FAILURE;
      }
    } else if (key.EqualsLiteral("address")) {
      aRetAddress = value; 
    } else {
      NS_WARNING("Unknown key in BluetoothStatusChanged message");
      return NS_ERROR_FAILURE;
    }
  }

  LOG("status: %d", *aRetStatus);
  LOG("address: %s", aRetAddress.BeginReading());
  return NS_OK;
}

nsresult
AudioManager::Observe(nsISupports* aSubject,
                      const char* aTopic,
                      const PRUnichar* aData)
{
  LOG("[Audio] %s", __FUNCTION__);
  bool status;
  nsCString address;
  nsCString data = NS_ConvertUTF16toUTF8(aData);
  nsresult rv = ParseBluetoothStatusChagnedMessage(data, address, &status);
  if (NS_FAILED(rv)) { 
    NS_WARNING("Failed to parse BluetoothStatusChanged message");
    LOG("Failed to parse BluetoothStatusChanged message");
    return NS_ERROR_FAILURE;
  }

  LOG("status: %d", status);
  LOG("address: %s", address.BeginReading());
  audio_policy_dev_state_t audioState = AUDIO_POLICY_DEVICE_STATE_AVAILABLE;
  if (!status) {
    audioState = AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE;
  }

  if (!strcmp(aTopic, BLUETOOTH_SCO_STATUS_CHANGED)) {
    AudioSystem::setDeviceConnectionState(AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET,
                                          audioState, address.BeginReading());
    AudioSystem::setDeviceConnectionState(AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET,
                                          audioState, address.BeginReading());
    if (status) {
      String8 cmd;
      cmd.appendFormat("bt_samplerate=%d", kBtSampleRate);
      AudioSystem::setParameters(0, cmd);
      SetForceForUse(nsIAudioManager::USE_COMMUNICATION, nsIAudioManager::FORCE_BT_SCO);
    } else {
      // only force to none if the current force setting is bt_sco
      int32_t force;
      GetForceForUse(nsIAudioManager::USE_COMMUNICATION, &force);
      if (force == nsIAudioManager::FORCE_BT_SCO)
        SetForceForUse(nsIAudioManager::USE_COMMUNICATION, nsIAudioManager::FORCE_NONE);
    }
  } else if (!strcmp(aTopic, BLUETOOTH_A2DP_STATUS_CHANGED)) {
    AudioSystem::setDeviceConnectionState(AUDIO_DEVICE_OUT_BLUETOOTH_A2DP,
                                          audioState, address.BeginReading());
    if (status) {
      String8 cmd("bluetooth_enabled=true");
      AudioSystem::setParameters(0, cmd);
      cmd.setTo("A2dpSuspended=false");
      AudioSystem::setParameters(0, cmd);
    }
  }
  return NS_OK;
}

static void
NotifyHeadphonesStatus(SwitchState aState)
{
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    if (aState == SWITCH_STATE_HEADSET) {
      obs->NotifyObservers(nullptr, HEADPHONES_STATUS_CHANGED, HEADPHONES_STATUS_HEADSET);
    } else if (aState == SWITCH_STATE_HEADPHONE) {
      obs->NotifyObservers(nullptr, HEADPHONES_STATUS_CHANGED, HEADPHONES_STATUS_HEADPHONE);
    } else if (aState == SWITCH_STATE_OFF) {
      obs->NotifyObservers(nullptr, HEADPHONES_STATUS_CHANGED, HEADPHONES_STATUS_OFF);
    } else {
      obs->NotifyObservers(nullptr, HEADPHONES_STATUS_CHANGED, HEADPHONES_STATUS_UNKNOWN);
    }
  }
}

class HeadphoneSwitchObserver : public SwitchObserver
{
public:
  void Notify(const SwitchEvent& aEvent) {
    InternalSetAudioRoutes(aEvent.status());
    NotifyHeadphonesStatus(aEvent.status());
  }
};

AudioManager::AudioManager() : mPhoneState(PHONE_STATE_CURRENT),
                 mObserver(new HeadphoneSwitchObserver())
{
  RegisterSwitchObserver(SWITCH_HEADPHONES, mObserver);

  InternalSetAudioRoutes(GetCurrentSwitchState(SWITCH_HEADPHONES));
  NotifyHeadphonesStatus(GetCurrentSwitchState(SWITCH_HEADPHONES));

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (NS_FAILED(obs->AddObserver(this, BLUETOOTH_SCO_STATUS_CHANGED, false))) {
    NS_WARNING("Failed to add bluetooth-sco-status-changed oberver!");
  } else if (NS_FAILED(obs->AddObserver(this, BLUETOOTH_A2DP_STATUS_CHANGED, false))) {
    NS_WARNING("Failed to add bluetooth-a2dp-status-changed oberver!");
  }

  for (int loop = 0; loop < AUDIO_STREAM_CNT; loop++) {
    AudioSystem::initStreamVolume(static_cast<audio_stream_type_t>(loop), 0,
                                  sMaxStreamVolumeTbl[loop]);
    mCurrentStreamVolumeTbl[loop] = sMaxStreamVolumeTbl[loop];
  }
  // Force publicnotification to output at maximal volume
#if ANDROID_VERSION < 17
  AudioSystem::setStreamVolumeIndex(
    static_cast<audio_stream_type_t>(AUDIO_STREAM_ENFORCED_AUDIBLE),
    sMaxStreamVolumeTbl[AUDIO_STREAM_ENFORCED_AUDIBLE]);
#else
  AudioSystem::setStreamVolumeIndex(
    static_cast<audio_stream_type_t>(AUDIO_STREAM_ENFORCED_AUDIBLE),
    sMaxStreamVolumeTbl[AUDIO_STREAM_ENFORCED_AUDIBLE],
    AUDIO_DEVICE_OUT_SPEAKER);
#endif

  AudioSystem::setErrorCallback(BinderDeadCallback);
}

AudioManager::~AudioManager() {
  UnregisterSwitchObserver(SWITCH_HEADPHONES, mObserver);

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (NS_FAILED(obs->RemoveObserver(this, BLUETOOTH_SCO_STATUS_CHANGED))) {
    NS_WARNING("Failed to remove bluetooth-sco-status-changed oberver!");
  } else if (NS_FAILED(obs->RemoveObserver(this, BLUETOOTH_A2DP_STATUS_CHANGED))) {
    NS_WARNING("Failed to remove bluetooth-a2dp-status-changed oberver!");
  }
}

NS_IMETHODIMP
AudioManager::GetMicrophoneMuted(bool* aMicrophoneMuted)
{
  if (AudioSystem::isMicrophoneMuted(aMicrophoneMuted)) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP
AudioManager::SetMicrophoneMuted(bool aMicrophoneMuted)
{
  if (AudioSystem::muteMicrophone(aMicrophoneMuted)) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP
AudioManager::GetMasterVolume(float* aMasterVolume)
{
  if (AudioSystem::getMasterVolume(aMasterVolume)) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP
AudioManager::SetMasterVolume(float aMasterVolume)
{
  if (AudioSystem::setMasterVolume(aMasterVolume)) {
    return NS_ERROR_FAILURE;
  }
  // For now, just set the voice volume at the same level
  if (AudioSystem::setVoiceVolume(aMasterVolume)) {
    return NS_ERROR_FAILURE;
  }

  if (IsDeviceOn(AUDIO_DEVICE_OUT_FM) &&
      AudioSystem::setFmVolume(aMasterVolume)) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

NS_IMETHODIMP
AudioManager::GetMasterMuted(bool* aMasterMuted)
{
  if (AudioSystem::getMasterMute(aMasterMuted)) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP
AudioManager::SetMasterMuted(bool aMasterMuted)
{
  if (AudioSystem::setMasterMute(aMasterMuted)) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP
AudioManager::GetPhoneState(int32_t* aState)
{
  *aState = mPhoneState;
  return NS_OK;
}

NS_IMETHODIMP
AudioManager::SetPhoneState(int32_t aState)
{
  if (mPhoneState == aState) {
    return NS_OK;
  }

#if ANDROID_VERSION < 17
  if (AudioSystem::setPhoneState(aState)) {
#else
  if (AudioSystem::setPhoneState(static_cast<audio_mode_t>(aState))) {
#endif
    return NS_ERROR_FAILURE;
  }

  mPhoneState = aState;

  if (mPhoneAudioAgent) {
    mPhoneAudioAgent->StopPlaying();
    mPhoneAudioAgent = nullptr;
  }

  if (aState == PHONE_STATE_IN_CALL || aState == PHONE_STATE_RINGTONE) {
    mPhoneAudioAgent = do_CreateInstance("@mozilla.org/audiochannelagent;1");
    MOZ_ASSERT(mPhoneAudioAgent);
    if (aState == PHONE_STATE_IN_CALL) {
      // Telephony doesn't be paused by any other channels.
      mPhoneAudioAgent->Init(AUDIO_CHANNEL_TELEPHONY, nullptr);
    } else {
      mPhoneAudioAgent->Init(AUDIO_CHANNEL_RINGER, nullptr);
    }

    // Telephony can always play.
    bool canPlay;
    mPhoneAudioAgent->StartPlaying(&canPlay);
  }

  return NS_OK;
}

//
// Kids, don't try this at home.  We want this to link and work on
// both GB and ICS.  Problem is, the symbol exported by audioflinger
// is different on the two gonks.
//
// So what we do here is weakly link to both of them, and then call
// whichever symbol resolves at dynamic link time (if any).
//
NS_IMETHODIMP
AudioManager::SetForceForUse(int32_t aUsage, int32_t aForce)
{
  status_t status = 0;

  if (IsDeviceOn(AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET) &&
      aUsage == nsIAudioManager::USE_COMMUNICATION &&
      aForce == nsIAudioManager::FORCE_NONE) {
    aForce = nsIAudioManager::FORCE_BT_SCO;
  }

  if (static_cast<
      status_t (*)(AudioSystem::force_use, AudioSystem::forced_config)
      >(AudioSystem::setForceUse)) {
    // Dynamically resolved the GB signature.
    status = AudioSystem::setForceUse((AudioSystem::force_use)aUsage,
                                      (AudioSystem::forced_config)aForce);
  } else if (static_cast<
             status_t (*)(audio_policy_force_use_t, audio_policy_forced_cfg_t)
             >(AudioSystem::setForceUse)) {
    // Dynamically resolved the ICS signature.
    status = AudioSystem::setForceUse((audio_policy_force_use_t)aUsage,
                                      (audio_policy_forced_cfg_t)aForce);
  }

  return status ? NS_ERROR_FAILURE : NS_OK;
}

NS_IMETHODIMP
AudioManager::GetForceForUse(int32_t aUsage, int32_t* aForce) {
  if (static_cast<
      AudioSystem::forced_config (*)(AudioSystem::force_use)
      >(AudioSystem::getForceUse)) {
    // Dynamically resolved the GB signature.
    *aForce = AudioSystem::getForceUse((AudioSystem::force_use)aUsage);
  } else if (static_cast<
             audio_policy_forced_cfg_t (*)(audio_policy_force_use_t)
             >(AudioSystem::getForceUse)) {
    // Dynamically resolved the ICS signature.
    *aForce = AudioSystem::getForceUse((audio_policy_force_use_t)aUsage);
  }
  return NS_OK;
}

NS_IMETHODIMP
AudioManager::GetFmRadioAudioEnabled(bool *aFmRadioAudioEnabled)
{
  *aFmRadioAudioEnabled = IsDeviceOn(AUDIO_DEVICE_OUT_FM);
  return NS_OK;
}

NS_IMETHODIMP
AudioManager::SetFmRadioAudioEnabled(bool aFmRadioAudioEnabled)
{
  if (static_cast<
      status_t (*) (AudioSystem::audio_devices, AudioSystem::device_connection_state, const char *)
      >(AudioSystem::setDeviceConnectionState)) {
    AudioSystem::setDeviceConnectionState(AUDIO_DEVICE_OUT_FM,
      aFmRadioAudioEnabled ? AUDIO_POLICY_DEVICE_STATE_AVAILABLE :
      AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE, "");
    InternalSetAudioRoutes(GetCurrentSwitchState(SWITCH_HEADPHONES));
    // sync volume with music after powering on fm radio
    if (aFmRadioAudioEnabled) {
      int32_t volIndex = 0;
#if ANDROID_VERSION < 17
      AudioSystem::getStreamVolumeIndex(
        static_cast<audio_stream_type_t>(AUDIO_STREAM_MUSIC),
        &volIndex);
      AudioSystem::setStreamVolumeIndex(
        static_cast<audio_stream_type_t>(AUDIO_STREAM_FM),
        volIndex);
#else
      AudioSystem::getStreamVolumeIndex(
        static_cast<audio_stream_type_t>(AUDIO_STREAM_MUSIC),
        &volIndex,
        AUDIO_DEVICE_OUT_DEFAULT);
      AudioSystem::setStreamVolumeIndex(
        static_cast<audio_stream_type_t>(AUDIO_STREAM_FM),
        volIndex,
        AUDIO_DEVICE_OUT_SPEAKER);
#endif
      mCurrentStreamVolumeTbl[AUDIO_STREAM_FM] = volIndex;
    }
    return NS_OK;
  } else {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
}

NS_IMETHODIMP
AudioManager::SetStreamVolumeIndex(int32_t aStream, int32_t aIndex) {
#if ANDROID_VERSION < 17
  status_t status =
    AudioSystem::setStreamVolumeIndex(
      static_cast<audio_stream_type_t>(aStream),
      aIndex);
#else
  status_t status =
    AudioSystem::setStreamVolumeIndex(
      static_cast<audio_stream_type_t>(aStream),
      aIndex,
      AUDIO_DEVICE_OUT_SPEAKER);
#endif

  // sync fm volume with music stream type
  if (aStream == AUDIO_STREAM_MUSIC && IsDeviceOn(AUDIO_DEVICE_OUT_FM)) {
#if ANDROID_VERSION < 17
    AudioSystem::setStreamVolumeIndex(
      static_cast<audio_stream_type_t>(AUDIO_STREAM_FM),
      aIndex);
#else
    AudioSystem::setStreamVolumeIndex(
      static_cast<audio_stream_type_t>(AUDIO_STREAM_FM),
      aIndex,
      AUDIO_DEVICE_OUT_SPEAKER);
#endif
    mCurrentStreamVolumeTbl[AUDIO_STREAM_FM] = aIndex;
  }
  mCurrentStreamVolumeTbl[aStream] = aIndex;

  return status ? NS_ERROR_FAILURE : NS_OK;
}

NS_IMETHODIMP
AudioManager::GetStreamVolumeIndex(int32_t aStream, int32_t* aIndex) {
  *aIndex = mCurrentStreamVolumeTbl[aStream];
  return NS_OK;
}

NS_IMETHODIMP
AudioManager::GetMaxStreamVolumeIndex(int32_t aStream, int32_t* aMaxIndex) {
  *aMaxIndex = sMaxStreamVolumeTbl[aStream];
  return NS_OK;
}

