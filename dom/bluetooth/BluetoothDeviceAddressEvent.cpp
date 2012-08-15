/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=40: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"
#include "BluetoothDeviceAddressEvent.h"
#include "BluetoothTypes.h"

#include "nsDOMClassInfo.h"

USING_BLUETOOTH_NAMESPACE

// static
already_AddRefed<BluetoothDeviceAddressEvent>
BluetoothDeviceAddressEvent::Create(const nsAString& aDeviceAddress)
{
  NS_ASSERTION(!aDeviceAddress.IsEmpty(), "Empty Device Address!");

  nsRefPtr<BluetoothDeviceAddressEvent> event = new BluetoothDeviceAddressEvent();

  event->mDeviceAddress = aDeviceAddress;

  return event.forget();
}

NS_IMPL_CYCLE_COLLECTION_CLASS(BluetoothDeviceAddressEvent)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(BluetoothDeviceAddressEvent,
                                                  nsDOMEvent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(BluetoothDeviceAddressEvent,
                                                nsDOMEvent)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(BluetoothDeviceAddressEvent)
  NS_INTERFACE_MAP_ENTRY(nsIDOMBluetoothDeviceAddressEvent)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(BluetoothDeviceAddressEvent)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEvent)

NS_IMPL_ADDREF_INHERITED(BluetoothDeviceAddressEvent, nsDOMEvent)
NS_IMPL_RELEASE_INHERITED(BluetoothDeviceAddressEvent, nsDOMEvent)

DOMCI_DATA(BluetoothDeviceAddressEvent, BluetoothDeviceAddressEvent)

NS_IMETHODIMP
BluetoothDeviceAddressEvent::GetDeviceAddress(nsAString& aDeviceAddress)
{
  aDeviceAddress = mDeviceAddress;
  return NS_OK;
}
