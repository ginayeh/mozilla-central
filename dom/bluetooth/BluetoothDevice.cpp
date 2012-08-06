/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=40: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"
#include "BluetoothDevice.h"
#include "BluetoothPropertyEvent.h"
#include "BluetoothTypes.h"
#include "BluetoothReplyRunnable.h"
#include "BluetoothService.h"
#include "BluetoothUtils.h"

#include "nsIDOMDOMRequest.h"
#include "nsDOMClassInfo.h"
#include "nsContentUtils.h"

USING_BLUETOOTH_NAMESPACE

DOMCI_DATA(BluetoothDevice, BluetoothDevice)

NS_IMPL_CYCLE_COLLECTION_CLASS(BluetoothDevice)

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(BluetoothDevice,
                                               nsDOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mJsUuids)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(BluetoothDevice, 
                                                  nsDOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_SCRIPT_OBJECTS
  NS_CYCLE_COLLECTION_TRAVERSE_EVENT_HANDLER(propertychanged)  
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(BluetoothDevice, 
                                                nsDOMEventTargetHelper)
  tmp->Unroot();
  NS_CYCLE_COLLECTION_UNLINK_EVENT_HANDLER(propertychanged)  
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(BluetoothDevice)
  NS_INTERFACE_MAP_ENTRY(nsIDOMBluetoothDevice)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(BluetoothDevice)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(BluetoothDevice, nsDOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(BluetoothDevice, nsDOMEventTargetHelper)

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "GonkDBus", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

class GetPropertiesTask : public BluetoothReplyRunnable
{
public:

  GetPropertiesTask(BluetoothPropertyContainer* aPropObj, nsIDOMDOMRequest* aReq) :
      BluetoothReplyRunnable(aReq),
      mPropObjPtr(aPropObj)
  {
    LOG("new GetPropertiesTask");
    MOZ_ASSERT(aReq && aPropObj);
  }

  bool
  ParseSuccessfulReply(jsval* aValue)
  {
//    nsCOMPtr<nsIDOMBluetoothAdapter> adapter;
    *aValue = JSVAL_VOID;

/*    const InfallibleTArray<BluetoothNamedValue>& v =
      mReply->get_BluetoothReplySuccess().value().get_ArrayOfBluetoothNamedValue();
    adapter = BluetoothAdapter::Create(mManagerPtr->GetOwner(), v); */

    BluetoothValue& v = mReply->get_BluetoothReplySuccess().value();
    if(v.type() != BluetoothValue::TArrayOfBluetoothNamedValue) {
      NS_WARNING("Not a BluetoothNamedValue array!");
      LOG("Not a BluetoothNamedValue array!");
      return false;
    }
    const InfallibleTArray<BluetoothNamedValue>& values =
      mReply->get_BluetoothReplySuccess().value().get_ArrayOfBluetoothNamedValue();
    for (uint32_t i = 0; i < values.Length(); ++i) {
      mPropObjPtr->SetPropertyByValue(values[i]);
    }
    return true;
/*
    nsresult rv;
    nsIScriptContext* sc = mManagerPtr->GetContextForEventHandlers(&rv);
    if (!sc) {
      NS_WARNING("Cannot create script context!");
      SetError(NS_LITERAL_STRING("BluetoothScriptContextError"));
      return false;
    }

    rv = nsContentUtils::WrapNative(sc->GetNativeContext(),
                                    sc->GetNativeGlobal(),
                                    adapter,
                                    aValue);
    bool result = NS_SUCCEEDED(rv) ? true : false;
    if (!result) {
      NS_WARNING("Cannot create native object!");
      SetError(NS_LITERAL_STRING("BluetoothNativeObjectError"));
    }

    return result;*/
  }

  void
  ReleaseMembers()
  {
    BluetoothReplyRunnable::ReleaseMembers();
    mPropObjPtr = nsnull;
  }

private:
  BluetoothPropertyContainer* mPropObjPtr;
};


BluetoothDevice::BluetoothDevice(nsPIDOMWindow* aOwner,
                                 const nsAString& aAdapterPath,
                                 const BluetoothValue& aValue) :
  BluetoothPropertyContainer(BluetoothObjectType::TYPE_DEVICE),
  mJsUuids(nsnull),
  mAdapterPath(aAdapterPath),
  mIsRooted(false)
{
  BindToOwner(aOwner);
  if(aValue.type() == BluetoothValue::TnsString) {
    mPath = aValue.get_nsString();
  } else {
    const InfallibleTArray<BluetoothNamedValue>& values =
      aValue.get_ArrayOfBluetoothNamedValue();
    for (uint32_t i = 0; i < values.Length(); ++i) {
      SetPropertyByValue(values[i]);
    }
  }
}

BluetoothDevice::~BluetoothDevice()
{
  BluetoothService* bs = BluetoothService::Get();
  // We can be null on shutdown, where this might happen
  if (bs) {
    if (NS_FAILED(bs->UnregisterBluetoothSignalHandler(mPath, this))) {
      NS_WARNING("Failed to unregister object with observer!");
    }
  }
  Unroot();
}

void
BluetoothDevice::Root()
{
  if(!mIsRooted) {
    NS_HOLD_JS_OBJECTS(this, BluetoothDevice);
    mIsRooted = true;
  }
}

void
BluetoothDevice::Unroot()
{
  if(mIsRooted) {
    NS_DROP_JS_OBJECTS(this, BluetoothDevice);
    mIsRooted = false;
  }
}
  
void
BluetoothDevice::SetPropertyByValue(const BluetoothNamedValue& aValue)
{
  const nsString& name = aValue.name();
  const BluetoothValue& value = aValue.value();
  if (name.EqualsLiteral("Name")) {
    mName = value.get_nsString();
  } else if (name.EqualsLiteral("Address")) {
    mAddress = value.get_nsString();
    BluetoothService* bs = BluetoothService::Get();
    if(!bs) {
      NS_WARNING("BluetoothService not available!");
      return;
    }
    // We can't actually set up our path until we know our address
    bs->GetDevicePath(mAdapterPath, mAddress, mPath);
  } else if (name.EqualsLiteral("Class")) {
    mClass = value.get_uint32_t();
  } else if (name.EqualsLiteral("Connected")) {
    mConnected = value.get_bool();
  } else if (name.EqualsLiteral("Paired")) {
    mPaired = value.get_bool();
  } else if (name.EqualsLiteral("UUIDs")) {
    mUuids = value.get_ArrayOfnsString();
    nsresult rv;
    nsIScriptContext* sc = GetContextForEventHandlers(&rv);
    if (sc) {
      rv =
        nsStringArrayToJSArray(sc->GetNativeContext(),
                               sc->GetNativeGlobal(), mUuids, &mJsUuids);
      if (NS_FAILED(rv)) {
        NS_WARNING("Cannot set JS UUIDs object!");
        return;
      }
      Root();
    } else {
      NS_WARNING("Could not get context!");
    }
#ifdef DEBUG
  } else {
    nsCString warningMsg;
    warningMsg.AssignLiteral("Not handling device property: ");
    warningMsg.Append(NS_ConvertUTF16toUTF8(name));
    NS_WARNING(warningMsg.get());
#endif
  }
}

// static
already_AddRefed<BluetoothDevice>
BluetoothDevice::Create(nsPIDOMWindow* aOwner,
                        const nsAString& aAdapterPath,
                        const BluetoothValue& aValue)
{
  // Make sure we at least have a service
  BluetoothService* bs = BluetoothService::Get();
  if(!bs) {
    NS_WARNING("BluetoothService not available!");
    return nsnull;
  }

  nsRefPtr<BluetoothDevice> device = new BluetoothDevice(aOwner, aAdapterPath,
                                                         aValue);
  nsString path;
  path = device->GetPath();
  if (NS_FAILED(bs->RegisterBluetoothSignalHandler(path, device))) {
    NS_WARNING("Failed to register object with observer!");
    return nsnull;
  }
  return device.forget();
}

void
BluetoothDevice::Notify(const BluetoothSignal& aData)
{
  if (aData.name().EqualsLiteral("PropertyChanged")) {
    // Get BluetoothNamedValue, make sure array length is 1
    BluetoothNamedValue v = aData.value().get_ArrayOfBluetoothNamedValue()[0];
    nsString name = v.name();
    SetPropertyByValue(v);
    nsRefPtr<BluetoothPropertyEvent> e = BluetoothPropertyEvent::Create(name);
    e->Dispatch(ToIDOMEventTarget(), NS_LITERAL_STRING("propertychanged"));
  } else {
#ifdef DEBUG
    nsCString warningMsg;
    warningMsg.AssignLiteral("Not handling device signal: ");
    warningMsg.Append(NS_ConvertUTF16toUTF8(aData.name()));
    NS_WARNING(warningMsg.get());
#endif
  }
}

NS_IMETHODIMP
BluetoothDevice::GetProperties()
{
  BluetoothService* bs = BluetoothService::Get();
  if(!bs) {
    NS_WARNING("Bluetooth service not available!");
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIDOMRequestService> rs = do_GetService("@mozilla.org/dom/dom-request-service;1");
  if (!rs) {
    NS_WARNING("No DOMRequest Service!");
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIDOMDOMRequest> request;
  nsresult rv = rs->CreateRequest(GetOwner(), getter_AddRefs(request));
  if (NS_FAILED(rv)) {
    NS_WARNING("Can't create DOMRequest!");
    return NS_ERROR_FAILURE;
  }

  nsRefPtr<BluetoothReplyRunnable> results = new GetPropertiesTask(this, request);

//  if (NS_FAILED(bs->GetDefaultAdapterPathInternal(results))) {
  if (NS_FAILED(bs->GetDevicePropertiesInternal(results))) {
//    return NS_ERROR_FAILURE;
  }


  return NS_OK;
//  return bs->GetProperties(mObjectType, mPath, results);
}

NS_IMETHODIMP
BluetoothDevice::GetAddress(nsAString& aAddress)
{
  aAddress = mAddress;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothDevice::GetName(nsAString& aName)
{
  aName = mName;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothDevice::GetDeviceClass(PRUint32* aClass)
{
  *aClass = mClass;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothDevice::GetPaired(bool* aPaired)
{
  *aPaired = mPaired;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothDevice::GetConnected(bool* aConnected)
{
  *aConnected = mConnected;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothDevice::GetUuids(JSContext* aCx, jsval* aUuids)
{
  if (mJsUuids) {
    aUuids->setObject(*mJsUuids);
  } else {
    NS_WARNING("UUIDs not yet set!\n");
    return NS_ERROR_FAILURE;
  }    
  return NS_OK;
}

NS_IMPL_EVENT_HANDLER(BluetoothDevice, propertychanged)
