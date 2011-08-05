// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_RENDERER_WEBIDBFACTORY_IMPL_H_
#define CONTENT_RENDERER_RENDERER_WEBIDBFACTORY_IMPL_H_
#pragma once

#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBCallbacks.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBFactory.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebVector.h"

namespace WebKit {
class WebDOMStringList;
class WebFrame;
class WebIDBDatabase;
class WebSecurityOrigin;
class WebString;
}

class RendererWebIDBFactoryImpl : public WebKit::WebIDBFactory {
 public:
  RendererWebIDBFactoryImpl();
  virtual ~RendererWebIDBFactoryImpl();

  // See WebIDBFactory.h for documentation on these functions.
  virtual void open(
      const WebKit::WebString& name,
      WebKit::WebIDBCallbacks* callbacks,
      const WebKit::WebSecurityOrigin& origin,
      WebKit::WebFrame* web_frame,
      const WebKit::WebString& data_dir,
      unsigned long long maximum_size,
      WebKit::WebIDBFactory::BackingStoreType);
  // FIXME(jochen): remove after webkit roll.
  virtual void deleteDatabase(
      const WebKit::WebString& name,
      WebKit::WebIDBCallbacks* callbacks,
      const WebKit::WebSecurityOrigin& origin,
      WebKit::WebFrame* web_frame,
      const WebKit::WebString& data_dir);
  virtual void deleteDatabase(
      const WebKit::WebString& name,
      WebKit::WebIDBCallbacks* callbacks,
      const WebKit::WebSecurityOrigin& origin,
      WebKit::WebFrame* web_frame,
      const WebKit::WebString& data_dir,
      WebKit::WebIDBFactory::BackingStoreType);
};

#endif  // CONTENT_RENDERER_RENDERER_WEBIDBFACTORY_IMPL_H_
