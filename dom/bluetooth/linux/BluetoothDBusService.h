/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothdbuseventservice_h__
#define mozilla_dom_bluetooth_bluetoothdbuseventservice_h__

#include "mozilla/Attributes.h"
#include "BluetoothCommon.h"
#include "mozilla/ipc/RawDBusConnection.h"
#include "mozilla/ipc/DBusUtils.h"
#include "BluetoothService.h"
#include "BluetoothDBusCommon.h"

class DBusMessage;

BEGIN_BLUETOOTH_NAMESPACE

/**
 * BluetoothDBusService is the implementation of BluetoothService for DBus on
 * linux/android/B2G. Function comments are in BluetoothService.h
 */

class BluetoothDBusService : public BluetoothService
                           , private mozilla::ipc::RawDBusConnection
{
public:
  bool IsReady();

  virtual nsresult StartInternal() MOZ_OVERRIDE;

  virtual nsresult StopInternal() MOZ_OVERRIDE;

  virtual bool IsEnabledInternal() MOZ_OVERRIDE;

  virtual nsresult GetDefaultAdapterPathInternal(
                                             BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual nsresult GetConnectedDevicePropertiesInternal(uint16_t aProfileId,
                                             BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual nsresult GetPairedDevicePropertiesInternal(
                                     const nsTArray<nsString>& aDeviceAddresses,
                                     BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual nsresult StartDiscoveryInternal(BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual nsresult StopDiscoveryInternal(BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual nsresult
  SetProperty(BluetoothObjectType aType,
              const BluetoothNamedValue& aValue,
              BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual nsresult
  GetScoSocket(const nsAString& aObjectPath,
               bool aAuth,
               bool aEncrypt,
               mozilla::ipc::UnixSocketConsumer* aConsumer) MOZ_OVERRIDE;

  virtual nsresult
  GetServiceChannel(const nsAString& aDeviceAddress,
                    const nsAString& aServiceUuid,
                    BluetoothProfileManagerBase* aManager) MOZ_OVERRIDE;

  virtual bool
  UpdateSdpRecords(const nsAString& aDeviceAddress,
                   BluetoothProfileManagerBase* aManager) MOZ_OVERRIDE;

  virtual nsresult
  CreatePairedDeviceInternal(const nsAString& aDeviceAddress,
                             int aTimeout,
                             BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual nsresult
  RemoveDeviceInternal(const nsAString& aDeviceObjectPath,
                       BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual bool
  SetPinCodeInternal(const nsAString& aDeviceAddress, const nsAString& aPinCode,
                     BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual bool
  SetPasskeyInternal(const nsAString& aDeviceAddress, uint32_t aPasskey,
                     BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual bool
  SetPairingConfirmationInternal(const nsAString& aDeviceAddress, bool aConfirm,
                                 BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual bool
  SetAuthorizationInternal(const nsAString& aDeviceAddress, bool aAllow,
                           BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual void
  Connect(const nsAString& aDeviceAddress,
          const uint16_t aProfileId,
          BluetoothReplyRunnable* aRunnable);

  virtual bool
  IsConnected(uint16_t aProfileId) MOZ_OVERRIDE;

  virtual void
  Disconnect(const uint16_t aProfileId, BluetoothReplyRunnable* aRunnable);

  virtual void
  SendFile(const nsAString& aDeviceAddress,
           BlobParent* aBlobParent,
           BlobChild* aBlobChild,
           BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual void
  StopSendingFile(const nsAString& aDeviceAddress,
                  BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual void
  ConfirmReceivingFile(const nsAString& aDeviceAddress, bool aConfirm,
                       BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual void
  ConnectSco(BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual void
  DisconnectSco(BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual void
  IsScoConnected(BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual void
  SendMetaData(const nsAString& aTitle,
               const nsAString& aArtist,
               const nsAString& aAlbum,
               uint32_t aMediaNumber,
               uint32_t aTotalMediaCount,
               uint32_t aDuration,
               BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual void
  SendPlayStatus(uint32_t aDuration,
                 uint32_t aPosition,
                 const nsAString& aPlayStatus,
                 BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual void
  UpdatePlayStatus(uint32_t aDuration,
                   uint32_t aPosition,
                   ControlPlayStatus aPlayStatus) MOZ_OVERRIDE;

  virtual nsresult
  SendSinkMessage(const nsAString& aDeviceAddresses,
                  const nsAString& aMessage) MOZ_OVERRIDE;

  static bool
  AddServiceRecords(const char* serviceName,
                    unsigned long long uuidMsb,
                    unsigned long long uuidLsb,
                    int channel);

  static bool
  AddReservedServicesInternal(const nsTArray<uint32_t>& aServices,
                              nsTArray<uint32_t>& aServiceHandlesContainer);

  static bool
  RemoveReservedServicesInternal(const nsTArray<uint32_t>& aServiceHandles);

  static void
  GetIsPairing(int32_t* aIsPairing)
  {
    *aIsPairing = mIsPairing;
  }

  static void
  SetIsPairing(int32_t aIsPairing)
  {
    mIsPairing = aIsPairing;
  }

  static void
  SetAdapterPath(const nsAString& aAdapterPath)
  {
    mAdapterPath = aAdapterPath;
  }

  static void
  GetAdapterPath(nsAString& aAdapterPath)
  {
    aAdapterPath = mAdapterPath;
  }

  static void
  PutPairingRequest(const nsAString& aAddress, DBusMessage* aMsg)
  {
    mPairingReqTable.Put(aAddress, aMsg);
    // Increase ref count here because we need this message later.
    // It'll be unrefed when set*Internal() is called.
    dbus_message_ref(aMsg);
  }

  static void
  PutAuthorizeRequest(const nsAString& aAddress, DBusMessage* aMsg)
  {
    mAuthorizeReqTable.Put(aAddress, aMsg);
    // Increase ref count here because we need this message later.
    // It'll be unrefed when setAuthorizationInternal() is called.
    dbus_message_ref(aMsg);
  }

  static DBusConnection*
  GetCommandThreadConnection()
  {
    return mCommandThreadConnection->GetConnection();
  }

private:
  enum ControlEventId {
    EVENT_PLAYBACK_STATUS_CHANGED            = 0x01,
    EVENT_TRACK_CHANGED                      = 0x02,
    EVENT_TRACK_REACHED_END                  = 0x03,
    EVENT_TRACK_REACHED_START                = 0x04,
    EVENT_PLAYBACK_POS_CHANGED               = 0x05,
    EVENT_BATT_STATUS_CHANGED                = 0x06,
    EVENT_SYSTEM_STATUS_CHANGED              = 0x07,
    EVENT_PLAYER_APPLICATION_SETTING_CHANGED = 0x08,
    EVENT_UNKNOWN
  };

  nsresult SendGetPropertyMessage(const nsAString& aPath,
                                  const char* aInterface,
                                  void (*aCB)(DBusMessage *, void *),
                                  BluetoothReplyRunnable* aRunnable);

  nsresult SendDiscoveryMessage(const char* aMessageName,
                                BluetoothReplyRunnable* aRunnable);

  nsresult SendSetPropertyMessage(const char* aInterface,
                                  const BluetoothNamedValue& aValue,
                                  BluetoothReplyRunnable* aRunnable);

  void UpdateNotification(ControlEventId aEventId, uint64_t aData);

  void DisconnectAllAcls(const nsAString& aAdapterPath);

  static Atomic<int32_t> mIsPairing;
  static nsString mAdapterPath;

  /**
   * Because we may have authorization request and pairing request from the
   * same remote device at the same time, we need two tables to keep these
   * messages.
   */
  static nsDataHashtable<nsStringHashKey, DBusMessage* > mPairingReqTable;
  static nsDataHashtable<nsStringHashKey, DBusMessage* > mAuthorizeReqTable;

  /**
   * DBus Connection held for the BluetoothCommandThread to use. Should never be
   * used by any other thread.
   */
  static nsAutoPtr<RawDBusConnection> mCommandThreadConnection;
};

END_BLUETOOTH_NAMESPACE

#endif
