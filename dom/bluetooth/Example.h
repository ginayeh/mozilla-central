/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_example_h__
#define mozilla_dom_example_h__

#include "nsDOMEventTargetHelper.h"

namespace mozilla {
namespace dom {

class Example : public nsDOMEventTargetHelper
              , public nsIObserver
{
public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIOBSERVER

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(Example, nsDOMEventTargetHelper)

  Example();
  virtual ~Example();

  void DispatchTrustedEvent();

  void BroadcastSystemMessage();

  IMPL_EVENT_HANDLER(example);
};

}
}

#endif
