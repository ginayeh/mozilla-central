/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_example_notifier_h__
#define mozilla_dom_example_notifier_h__

#include "nsISupports.h"

namespace mozilla {
namespace dom {

class ExampleNotifier : public nsISupports
{
public:
  NS_DECL_ISUPPORTS

  ExampleNotifier();
  virtual ~ExampleNotifier();

  void NotifyObservers();
  int32_t GetValue();

private:
  int32_t mValue;
};

}
}

#endif
