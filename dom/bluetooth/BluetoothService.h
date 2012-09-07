/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetootheventservice_h__
#define mozilla_dom_bluetooth_bluetootheventservice_h__

#include "BluetoothCommon.h"
#include "nsAutoPtr.h"
#include "nsClassHashtable.h"
#include "nsDOMEventTargetHelper.h"
#include "nsIObserver.h"
#include "nsIThread.h"
#include "nsTObserverArray.h"

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothManager;
class BluetoothNamedValue;
class BluetoothReplyRunnable;
class BluetoothSignal;

class BluetoothService : public nsIObserver
                       , public BluetoothSignalObserver
                       , public nsDOMEventTargetHelper
{
  class ToggleBtAck;
  friend class ToggleBtAck;

  class ToggleBtTask;
  friend class ToggleBtTask;

  class StartupTask;
  friend class StartupTask;

public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  /** 
   * Add a message handler object from message distribution observer.
   * Must be called from the main thread.
   *
   * @param aNodeName Node name of the object
   * @param aMsgHandler Weak pointer to the object
   *
   * @return NS_OK on successful addition to observer, NS_ERROR_FAILED
   * otherwise
   */
  nsresult RegisterBluetoothSignalHandler(const nsAString& aNodeName,
                                          BluetoothSignalObserver* aMsgHandler);

  /** 
   * Remove a message handler object from message distribution observer.
   * Must be called from the main thread.
   *
   * @param aNodeName Node name of the object
   * @param aMsgHandler Weak pointer to the object
   *
   * @return NS_OK on successful removal from observer service,
   * NS_ERROR_FAILED otherwise
   */
  nsresult UnregisterBluetoothSignalHandler(const nsAString& aNodeName,
                                            BluetoothSignalObserver* aMsgHandler);

  /** 
   * Distribute a signal to the observer list
   *
   * @param aSignal Signal object to distribute
   *
   * @return NS_OK if signal distributed, NS_ERROR_FAILURE on error
   */
  nsresult DistributeSignal(const BluetoothSignal& aEvent);

  /** 
   * Start bluetooth services. Starts up any threads and connections that
   * bluetooth needs to operate on the current platform. Assumed to be run on
   * the main thread with delayed return for blocking startup functions via
   * runnable.
   * @return NS_OK on initialization starting correctly, NS_ERROR_FAILURE
   * otherwise
   */
  nsresult Start();

  /** 
   * Stop bluetooth services. Starts up any threads and connections that
   * bluetooth needs to operate on the current platform. Assumed to be run on
   * the main thread with delayed return for blocking startup functions via
   * runnable.
   *
   * @return NS_OK on initialization starting correctly, NS_ERROR_FAILURE
   * otherwise
   */
  nsresult Stop();

  /**
   * Called when XPCOM first creates this service.
   */
  nsresult HandleStartup();

  /**
   * Called when "mozsettings-changed" observer topic fires.
   */
  nsresult HandleSettingsChanged(const nsAString& aData);

  /**
   * Called when XPCOM is shutting down.
   */
  nsresult HandleShutdown();

  /**
   * Called when a BluetoothManager is created.
   */
  void RegisterManager(BluetoothManager* aManager);

  /**
   * Called when a BluetoothManager is destroyed.
   */
  void UnregisterManager(BluetoothManager* aManager);

  void Notify(const BluetoothSignal& aParam);

  /** 
   * Returns the BluetoothService singleton. Only to be called from main thread.
   *
   * @param aService Pointer to return singleton into. 
   *
   * @return NS_OK on proper assignment, NS_ERROR_FAILURE otherwise (if service
   * has not yet been started, for instance)
   */
  static BluetoothService* Get();

  static already_AddRefed<BluetoothService> FactoryCreate()
  {
    nsRefPtr<BluetoothService> service = Get();
    return service.forget();
  }

  /**
   * Returns the path of the default adapter, implemented via a platform
   * specific method.
   *
   * @return Default adapter path/name on success, NULL otherwise
   */
  virtual nsresult GetDefaultAdapterPathInternal(BluetoothReplyRunnable* aRunnable) = 0;

  /**
   * Returns the properties of paired devices, implemented via a platform
   * specific method.
   *
   * @return NS_OK on success, NS_ERROR_FAILURE otherwise
   */
  virtual nsresult GetPairedDevicePropertiesInternal(const nsTArray<nsString>& aDeviceAddresses,
                                                     BluetoothReplyRunnable* aRunnable) = 0;

  /** 
   * Stop device discovery (platform specific implementation)
   *
   * @param aAdapterPath Adapter to stop discovery on
   *
   * @return NS_OK if discovery stopped correctly, false otherwise
   */
  virtual nsresult StopDiscoveryInternal(const nsAString& aAdapterPath,
                                         BluetoothReplyRunnable* aRunnable) = 0;

  /** 
   * Start device discovery (platform specific implementation)
   *
   * @param aAdapterPath Adapter to start discovery on
   *
   * @return NS_OK if discovery stopped correctly, false otherwise
   */
  virtual nsresult StartDiscoveryInternal(const nsAString& aAdapterPath,
                                          BluetoothReplyRunnable* aRunnable) = 0;

  /** 
   * Platform specific startup functions go here. Usually deals with member
   * variables, so not static. Guaranteed to be called outside of main thread.
   *
   * @return NS_OK on correct startup, NS_ERROR_FAILURE otherwise
   */
  virtual nsresult StartInternal() = 0;

  /** 
   * Platform specific startup functions go here. Usually deals with member
   * variables, so not static. Guaranteed to be called outside of main thread.
   *
   * @return NS_OK on correct startup, NS_ERROR_FAILURE otherwise
   */
  virtual nsresult StopInternal() = 0;

  /** 
   * Fetches the propertes for the specified object
   *
   * @param aType Type of the object (see BluetoothObjectType in BluetoothCommon.h)
   * @param aPath Path of the object
   * @param aRunnable Runnable to return to after receiving callback
   *
   * @return NS_OK on function run, NS_ERROR_FAILURE otherwise
   */
  virtual nsresult
  GetProperties(BluetoothObjectType aType,
                const nsAString& aPath,
                BluetoothReplyRunnable* aRunnable) = 0;

  /** 
   * Set a property for the specified object
   *
   * @param aPath Path to the object
   * @param aPropName Name of the property
   * @param aValue Boolean value
   * @param aRunnable Runnable to run on async reply
   *
   * @return NS_OK if property is set correctly, NS_ERROR_FAILURE otherwise
   */
  virtual nsresult
  SetProperty(BluetoothObjectType aType,
              const nsAString& aPath,
              const BluetoothNamedValue& aValue,
              BluetoothReplyRunnable* aRunnable) = 0;

  /** 
   * Get the path of a device
   *
   * @param aAdapterPath Path to the Adapter that's communicating with the device
   * @param aDeviceAddress Device address (XX:XX:XX:XX:XX:XX format)
   * @param aDevicePath Return value of path
   *
   * @return True if path set correctly, false otherwise
   */
  virtual bool
  GetDevicePath(const nsAString& aAdapterPath,
                const nsAString& aDeviceAddress,
                nsAString& aDevicePath) = 0;

  virtual nsTArray<uint32_t>
  AddReservedServicesInternal(const nsAString& aAdapterPath,
                              const nsTArray<uint32_t>& aServices) = 0;

  virtual bool
  RemoveReservedServicesInternal(const nsAString& aAdapterPath,
                                 const nsTArray<uint32_t>& aServiceHandles) = 0;

  virtual nsresult
  CreatePairedDeviceInternal(const nsAString& aAdapterPath,
                             const nsAString& aAddress,
                             int aTimeout,
                             BluetoothReplyRunnable* aRunnable) = 0;

  virtual nsresult
  RemoveDeviceInternal(const nsAString& aAdapterPath,
                       const nsAString& aObjectPath,
                       BluetoothReplyRunnable* aRunnable) = 0;

  virtual nsresult
  GetSocketViaService(const nsAString& aObjectPath,
                      const nsAString& aService,
                      int aType,
                      bool aAuth,
                      bool aEncrypt,
                      BluetoothReplyRunnable* aRunnable) = 0;

  virtual bool
  CloseSocket(int aFd, BluetoothReplyRunnable* aRunnable) = 0;

  virtual bool SetPinCodeInternal(const nsAString& aDeviceAddress, const nsAString& aPinCode) = 0;
  virtual bool SetPasskeyInternal(const nsAString& aDeviceAddress, uint32_t aPasskey) = 0;
  virtual bool SetPairingConfirmationInternal(const nsAString& aDeviceAddress, bool aConfirm) = 0;
  virtual bool SetAuthorizationInternal(const nsAString& aDeviceAddress, bool aAllow) = 0;

  virtual bool IsEnabled()
  {
    return mEnabled;
  }

protected:
  BluetoothService()
  : mEnabled(false)
#ifdef DEBUG
    , mLastRequestedEnable(false)
#endif
  {
    mBluetoothSignalObserverTable.Init();
  }

  virtual ~BluetoothService()
  { }

  nsresult StartStopBluetooth(bool aStart);

  // Called by ToggleBtAck.
  void SetEnabled(bool aEnabled);

  // This function is implemented in platform-specific BluetoothServiceFactory
  // files
  static BluetoothService* Create();

  /**
   * Due to the fact that some operations require multiple calls, a
   * CommandThread is created that can run blocking, platform-specific calls
   * where either no asynchronous equivilent exists, or else where multiple
   * asynchronous calls would require excessive runnable bouncing between main
   * thread and IO thread.
   *
   * For instance, when we retrieve an Adapter object, we would like it to come
   * with all of its properties filled in and registered as an agent, which
   * requires a minimum of 3 calls to platform specific code on some platforms.
   *
   */
  nsCOMPtr<nsIThread> mBluetoothCommandThread;

  typedef mozilla::ObserverList<BluetoothSignal> BluetoothSignalObserverList;
  typedef nsClassHashtable<nsStringHashKey, BluetoothSignalObserverList >
  BluetoothSignalObserverTable;

  BluetoothSignalObserverTable mBluetoothSignalObserverTable;

  typedef nsTObserverArray<BluetoothManager*> BluetoothManagerList;
  BluetoothManagerList mLiveManagers;

  bool mEnabled;
#ifdef DEBUG
  bool mLastRequestedEnable;
#endif
};

END_BLUETOOTH_NAMESPACE

#endif
