/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BluetoothUuid.h"

USING_BLUETOOTH_NAMESPACE

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "GonkDBus", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

void
BluetoothUuidHelper::GetString(BluetoothServiceClass aServiceClassUuid,
                               nsAString& aRetUuidStr)
{
  aRetUuidStr.Truncate();

  aRetUuidStr.AppendLiteral("0000");
  aRetUuidStr.AppendInt(aServiceClassUuid, 16);
  aRetUuidStr.AppendLiteral("-0000-1000-8000-00805F9B34FB");
}

int32_t
BluetoothUuidHelper::GetServiceClass(const nsAString& aUuidStr)
{
  nsString uuid(Substring(aUuidStr, 0, 8));
  nsresult rv;
  int32_t ret = uuid.ToInteger(&rv, 16);
  NS_ENSURE_SUCCESS(rv, -1);
  return ret;
}
