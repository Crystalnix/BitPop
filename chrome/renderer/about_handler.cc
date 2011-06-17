// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/about_handler.h"

#include "base/process_util.h"
#include "base/threading/platform_thread.h"
#include "chrome/common/about_handler.h"
#include "googleurl/src/gurl.h"

typedef void (*AboutHandlerFuncPtr)();

// This needs to match up with chrome_about_handler::about_urls in
// chrome/common/about_handler.cc.
static const AboutHandlerFuncPtr about_urls_handlers[] = {
    AboutHandler::AboutCrash,
    AboutHandler::AboutKill,
    AboutHandler::AboutHang,
    AboutHandler::AboutShortHang,
    NULL,
};

// static
bool AboutHandler::MaybeHandle(const GURL& url) {
  if (url.scheme() != chrome_about_handler::kAboutScheme)
    return false;

  int about_urls_handler_index = 0;
  const char* const* url_handler = chrome_about_handler::about_urls;
  while (*url_handler) {
    if (GURL(*url_handler) == url) {
      about_urls_handlers[about_urls_handler_index]();
      return true;  // theoretically :]
    }
    url_handler++;
    about_urls_handler_index++;
  }
  return false;
}

// static
void AboutHandler::AboutCrash() {
  int *zero = NULL;
  *zero = 0;  // Null pointer dereference: kaboom!
}

// static
void AboutHandler::AboutKill() {
  base::KillProcess(base::GetCurrentProcessHandle(), 1, false);
}

// static
void AboutHandler::AboutHang() {
  for (;;) {
    base::PlatformThread::Sleep(1000);
  }
}

// static
void AboutHandler::AboutShortHang() {
  base::PlatformThread::Sleep(20000);
}

// static
size_t AboutHandler::AboutURLHandlerSize() {
  return arraysize(about_urls_handlers);
}
