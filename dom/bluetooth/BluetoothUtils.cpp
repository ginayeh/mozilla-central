/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=40: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BluetoothUtils.h"
#include "jsapi.h"
#include "nsTArray.h"
#include "nsString.h"

nsresult
mozilla::dom::bluetooth::nsStringArrayToJSArray(JSContext* aCx, JSObject* aGlobal,
                                                const nsTArray<nsString>& aSourceArray,
                                                JSObject** aResultArray)
{
  NS_ASSERTION(aCx, "Null context!");
  NS_ASSERTION(aGlobal, "Null global!");

  JSAutoRequest ar(aCx);
  JSAutoEnterCompartment ac;
  if (!ac.enter(aCx, aGlobal)) {
    NS_WARNING("Failed to enter compartment!");
    return NS_ERROR_FAILURE;
  }

  JSObject* arrayObj;

  if (aSourceArray.IsEmpty()) {
    arrayObj = JS_NewArrayObject(aCx, 0, nsnull);
  } else {
    nsTArray<jsval> valArray;
    valArray.SetLength(aSourceArray.Length());
    for (PRUint32 index = 0; index < valArray.Length(); index++) {
      nsString str = aSourceArray[index];
      JSString* s = JS_NewUCStringCopyN(aCx, str.BeginReading(), str.Length());      
      valArray[index] = STRING_TO_JSVAL(s);
    }

    arrayObj = JS_NewArrayObject(aCx, valArray.Length(), valArray.Elements());
  }

  if (!arrayObj) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  // XXX This is not what Jonas wants. He wants it to be live.
  if (!JS_FreezeObject(aCx, arrayObj)) {
    return NS_ERROR_FAILURE;
  }

  *aResultArray = arrayObj;
  return NS_OK;
}

