// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/browsing_instance.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "content/browser/site_instance_impl.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/web_ui_controller_factory.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/url_constants.h"

using content::SiteInstance;
using content::WebUIControllerFactory;

// static
base::LazyInstance<BrowsingInstance::ContextSiteInstanceMap>::Leaky
    BrowsingInstance::context_site_instance_map_ = LAZY_INSTANCE_INITIALIZER;

BrowsingInstance::BrowsingInstance(content::BrowserContext* browser_context)
    : browser_context_(browser_context) {
}

bool BrowsingInstance::ShouldUseProcessPerSite(const GURL& url) {
  // Returns true if we should use the process-per-site model.  This will be
  // the case if the --process-per-site switch is specified, or in
  // process-per-site-instance for particular sites (e.g., the new tab page).

  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(switches::kProcessPerSite))
    return true;

  // We want to consolidate particular sites like extensions and WebUI whether
  // it is in process-per-tab or process-per-site-instance.
  // Note that --single-process may have been specified, but that affects the
  // process creation logic in RenderProcessHost, so we do not need to worry
  // about it here.

  if (content::GetContentClient()->browser()->
      ShouldUseProcessPerSite(browser_context_, url))
    return true;

  // DevTools pages have WebUI type but should not reuse the same host.
  WebUIControllerFactory* factory =
      content::GetContentClient()->browser()->GetWebUIControllerFactory();
  if (factory && factory->UseWebUIForURL(browser_context_, url) &&
      !url.SchemeIs(chrome::kChromeDevToolsScheme)) {
    return true;
  }

  // In all other cases, don't use process-per-site logic.
  return false;
}

BrowsingInstance::SiteInstanceMap* BrowsingInstance::GetSiteInstanceMap(
    content::BrowserContext* browser_context, const GURL& url) {
  if (!ShouldUseProcessPerSite(
      SiteInstanceImpl::GetEffectiveURL(browser_context, url))) {
    // Not using process-per-site, so use a map specific to this instance.
    return &site_instance_map_;
  }

  // Otherwise, process-per-site is in use, at least for this URL.  Look up the
  // global map for this context, creating an entry if necessary.
  return &context_site_instance_map_.Get()[browser_context];
}

bool BrowsingInstance::HasSiteInstance(const GURL& url) {
  std::string site =
      SiteInstanceImpl::GetSiteForURL(browser_context_, url)
          .possibly_invalid_spec();

  SiteInstanceMap* map = GetSiteInstanceMap(browser_context_, url);
  SiteInstanceMap::iterator i = map->find(site);
  return (i != map->end());
}

SiteInstance* BrowsingInstance::GetSiteInstanceForURL(const GURL& url) {
  std::string site =
      SiteInstanceImpl::GetSiteForURL(browser_context_, url)
          .possibly_invalid_spec();

  SiteInstanceMap* map = GetSiteInstanceMap(browser_context_, url);
  SiteInstanceMap::iterator i = map->find(site);
  if (i != map->end()) {
    return i->second;
  }

  // No current SiteInstance for this site, so let's create one.
  SiteInstanceImpl* instance = new SiteInstanceImpl(this);

  // Set the site of this new SiteInstance, which will register it with us.
  instance->SetSite(url);
  return instance;
}

void BrowsingInstance::RegisterSiteInstance(SiteInstance* site_instance) {
  DCHECK(static_cast<SiteInstanceImpl*>(site_instance)->
         browsing_instance_ == this);
  DCHECK(static_cast<SiteInstanceImpl*>(site_instance)->HasSite());
  std::string site = site_instance->GetSite().possibly_invalid_spec();

  // Only register if we don't have a SiteInstance for this site already.
  // It's possible to have two SiteInstances point to the same site if two
  // tabs are navigated there at the same time.  (We don't call SetSite or
  // register them until DidNavigate.)  If there is a previously existing
  // SiteInstance for this site, we just won't register the new one.
  SiteInstanceMap* map = GetSiteInstanceMap(browser_context_,
                                            site_instance->GetSite());
  SiteInstanceMap::iterator i = map->find(site);
  if (i == map->end()) {
    // Not previously registered, so register it.
    (*map)[site] = site_instance;
  }
}

void BrowsingInstance::UnregisterSiteInstance(SiteInstance* site_instance) {
  DCHECK(static_cast<SiteInstanceImpl*>(site_instance)->
         browsing_instance_ == this);
  DCHECK(static_cast<SiteInstanceImpl*>(site_instance)->HasSite());
  std::string site = site_instance->GetSite().possibly_invalid_spec();

  // Only unregister the SiteInstance if it is the same one that is registered
  // for the site.  (It might have been an unregistered SiteInstance.  See the
  // comments in RegisterSiteInstance.)

  // We look for the site instance in both the local site_instance_map_ and also
  // the static context_site_instance_map_ - this is because the logic in
  // ShouldUseProcessPerSite() can produce different results over the lifetime
  // of Chrome (e.g. installation of apps with web extents can change our
  // process-per-site policy for a given domain), so we don't know which map
  // the site was put into when it was originally registered.
  if (!RemoveSiteInstanceFromMap(&site_instance_map_, site, site_instance)) {
    // Wasn't in our local map, so look in the static per-browser context map.
    RemoveSiteInstanceFromMap(
        &context_site_instance_map_.Get()[browser_context_],
        site,
        site_instance);
  }
}

bool BrowsingInstance::RemoveSiteInstanceFromMap(
    SiteInstanceMap* map,
    const std::string& site,
    SiteInstance* site_instance) {
  SiteInstanceMap::iterator i = map->find(site);
  if (i != map->end() && i->second == site_instance) {
    // Matches, so erase it.
    map->erase(i);
    return true;
  }
  return false;
}

BrowsingInstance::~BrowsingInstance() {
  // We should only be deleted when all of the SiteInstances that refer to
  // us are gone.
  DCHECK(site_instance_map_.empty());
}
