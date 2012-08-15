/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=40: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_deviceaddressevent_h__
#define mozilla_dom_bluetooth_deviceaddressevent_h__

#include "BluetoothCommon.h"

#include "nsIDOMBluetoothDeviceAddressEvent.h"
#include "nsIDOMEventTarget.h"

#include "nsDOMEvent.h"

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothDeviceAddressEvent : public nsDOMEvent
                                  , public nsIDOMBluetoothDeviceAddressEvent
{
public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_FORWARD_TO_NSDOMEVENT
  NS_DECL_NSIDOMBLUETOOTHDEVICEADDRESSEVENT
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(BluetoothDeviceAddressEvent, nsDOMEvent)

  static already_AddRefed<BluetoothDeviceAddressEvent>
  Create(const nsAString& aDeviceAddress);

  nsresult
  Dispatch(nsIDOMEventTarget* aTarget, const nsAString& aEventType)
  {
    NS_ASSERTION(aTarget, "Null pointer!");
    NS_ASSERTION(!aEventType.IsEmpty(), "Empty event type!");

    nsresult rv = InitEvent(aEventType, false, false);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = SetTrusted(true);
    NS_ENSURE_SUCCESS(rv, rv);

    nsIDOMEvent* thisEvent =
      static_cast<nsDOMEvent*>(const_cast<BluetoothDeviceAddressEvent*>(this));

    bool dummy;
    rv = aTarget->DispatchEvent(thisEvent, &dummy);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

private:
  BluetoothDeviceAddressEvent()
    : nsDOMEvent(nullptr, nullptr)
  { }

  ~BluetoothDeviceAddressEvent()
  { }

  nsString mDeviceAddress;
};

END_BLUETOOTH_NAMESPACE

#endif
