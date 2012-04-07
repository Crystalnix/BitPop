// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_CHROME_WEB_UI_FACTORY_H_
#define CHROME_BROWSER_UI_WEBUI_CHROME_WEB_UI_FACTORY_H_
#pragma once

#include "base/basictypes.h"
#include "base/memory/singleton.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_controller_factory.h"
#include "chrome/browser/favicon/favicon_service.h"

class Profile;
class RefCountedMemory;

class ChromeWebUIControllerFactory : public content::WebUIControllerFactory {
 public:
  virtual content::WebUI::TypeID GetWebUIType(
      content::BrowserContext* browser_context,
      const GURL& url) const OVERRIDE;
  virtual bool UseWebUIForURL(content::BrowserContext* browser_context,
                              const GURL& url) const OVERRIDE;
  virtual bool UseWebUIBindingsForURL(content::BrowserContext* browser_context,
                                      const GURL& url) const OVERRIDE;
  virtual bool HasWebUIScheme(const GURL& url) const OVERRIDE;
  virtual bool IsURLAcceptableForWebUI(content::BrowserContext* browser_context,
                                       const GURL& url) const OVERRIDE;
  virtual content::WebUIController* CreateWebUIControllerForURL(
      content::WebUI* web_ui,
      const GURL& url) const OVERRIDE;

  // Get the favicon for |page_url| and forward the result to the |request|
  // when loaded.
  void GetFaviconForURL(Profile* profile,
                        FaviconService::GetFaviconRequest* request,
                        const GURL& page_url) const;

  static ChromeWebUIControllerFactory* GetInstance();

 protected:
  ChromeWebUIControllerFactory();
  virtual ~ChromeWebUIControllerFactory();

 private:

  friend struct DefaultSingletonTraits<ChromeWebUIControllerFactory>;

  // Gets the data for the favicon for a WebUI page. Returns NULL if the WebUI
  // does not have a favicon.
  RefCountedMemory* GetFaviconResourceBytes(const GURL& page_url) const;

  DISALLOW_COPY_AND_ASSIGN(ChromeWebUIControllerFactory);
};

#endif  // CHROME_BROWSER_UI_WEBUI_CHROME_WEB_UI_FACTORY_H_
