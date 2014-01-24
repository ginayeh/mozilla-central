/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ExampleNotifier.h"

#include "nsIObserverService.h"
#include "mozilla/Services.h"

using namespace mozilla;
using namespace mozilla::dom;

ExampleNotifier::ExampleNotifier() : mValue(0)
{
}

ExampleNotifier::~ExampleNotifier()
{
}

void
ExampleNotifier::NotifyObservers()
{
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  NS_ENSURE_TRUE_VOID(obs);

  if (NS_FAILED(obs->NotifyObservers(this,
                                     "example-topic",
                                     nullptr))) {
    NS_WARNING("Failed to notify bluetooth-a2dp-status-changed observsers!");
  }
}

int32_t
ExampleNotifier::GetValue()
{
  return mValue;
}
