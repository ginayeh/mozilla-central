/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Example.h"
#include "mozilla/dom/ExampleEvent.h"

#include "nsIObserverService.h"
#include "mozilla/Services.h"
#include "ExampleNotifier.h"

using namespace mozilla;
using namespace mozilla::dom;

NS_IMPL_CYCLE_COLLECTION_CLASS(Example)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(Example,
                                                  nsDOMEventTargetHelper)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(Example,
                                                nsDOMEventTargetHelper)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(Example)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(Example, nsDOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(Example, nsDOMEventTargetHelper)

Example::Example()
{
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  NS_ENSURE_TRUE_VOID(obs);
  if (NS_FAILED(obs->AddObserver(this, "example-topic", false))) {
    NS_WARNING("Failed to add observer!");
  }
}

Example::~Example()
{
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  NS_ENSURE_TRUE_VOID(obs);
  if (NS_FAILED(obs->RemoveObserver(this, "example-topic"))) {
    NS_WARNING("Failed to remove observer!");
  }
}

NS_IMETHODIMP
Example::Observe(nsISupports* aSubject,
                 const char* aTopic,
                 const char16_t* aData)
{
  MOZ_ASSERT(NS_IsMainThread());

  if (!strcmp(aTopic, "example-topic")) {
    ExampleNotifier* notifier = static_cast<ExampleNotifier*>(aSubject);
    if (!notifier) {
      return NS_ERROR_FAILURE;
    }
    int32_t value = notifier->GetValue();
    return NS_OK;
  }

  return NS_ERROR_UNEXPECTED;
}

void
Example::DispatchTrustedEvent()
{
  MOZ_ASSERT(NS_IsMainThread());

  ExampleEventInit init;
  init.mBubbles = false;
  init.mCancelable = false;
  init.mValue = 0.7;
  nsRefPtr<ExampleEvent> event =
    ExampleEvent::Constructor(this, NS_LITERAL_STRING("example"), init);
}

