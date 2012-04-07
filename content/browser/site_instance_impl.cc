// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/site_instance_impl.h"

#include "base/command_line.h"
#include "content/browser/browsing_instance.h"
#include "content/browser/child_process_security_policy.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_process_host_factory.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/url_constants.h"
#include "net/base/registry_controlled_domain.h"

using content::SiteInstance;

static bool IsURLSameAsAnySiteInstance(const GURL& url) {
  if (!url.is_valid())
    return false;

  // We treat javascript: as the same site as any URL since it is actually
  // a modifier on existing pages.
  if (url.SchemeIs(chrome::kJavaScriptScheme))
    return true;

  return
      content::GetContentClient()->browser()->IsURLSameAsAnySiteInstance(url);
}

int32 SiteInstanceImpl::next_site_instance_id_ = 1;

SiteInstanceImpl::SiteInstanceImpl(BrowsingInstance* browsing_instance)
    : id_(next_site_instance_id_++),
      browsing_instance_(browsing_instance),
      render_process_host_factory_(NULL),
      process_(NULL),
      has_site_(false) {
  DCHECK(browsing_instance);

  registrar_.Add(this, content::NOTIFICATION_RENDERER_PROCESS_TERMINATED,
                 content::NotificationService::AllBrowserContextsAndSources());
}

SiteInstanceImpl::~SiteInstanceImpl() {
  content::GetContentClient()->browser()->SiteInstanceDeleting(this);

  // Now that no one is referencing us, we can safely remove ourselves from
  // the BrowsingInstance.  Any future visits to a page from this site
  // (within the same BrowsingInstance) can safely create a new SiteInstance.
  if (has_site_)
    browsing_instance_->UnregisterSiteInstance(
        static_cast<SiteInstance*>(this));
}

int32 SiteInstanceImpl::GetId() {
  return id_;
}

bool SiteInstanceImpl::HasProcess() const {
  return (process_ != NULL);
}

content::RenderProcessHost* SiteInstanceImpl::GetProcess() {
  // TODO(erikkay) It would be nice to ensure that the renderer type had been
  // properly set before we get here.  The default tab creation case winds up
  // with no site set at this point, so it will default to TYPE_NORMAL.  This
  // may not be correct, so we'll wind up potentially creating a process that
  // we then throw away, or worse sharing a process with the wrong process type.
  // See crbug.com/43448.

  // Create a new process if ours went away or was reused.
  if (!process_) {
    // See if we should reuse an old process
    if (content::RenderProcessHost::ShouldTryToUseExistingProcessHost())
      process_ = content::RenderProcessHost::GetExistingProcessHost(
          browsing_instance_->browser_context(), site_);

    // Otherwise (or if that fails), create a new one.
    if (!process_) {
      if (render_process_host_factory_) {
        process_ = render_process_host_factory_->CreateRenderProcessHost(
            browsing_instance_->browser_context());
      } else {
        process_ =
            new RenderProcessHostImpl(browsing_instance_->browser_context());
      }
    }

    content::GetContentClient()->browser()->SiteInstanceGotProcess(this);

    if (has_site_)
      LockToOrigin();
  }
  DCHECK(process_);

  return process_;
}

void SiteInstanceImpl::SetSite(const GURL& url) {
  // A SiteInstance's site should not change.
  // TODO(creis): When following links or script navigations, we can currently
  // render pages from other sites in this SiteInstance.  This will eventually
  // be fixed, but until then, we should still not set the site of a
  // SiteInstance more than once.
  DCHECK(!has_site_);

  // Remember that this SiteInstance has been used to load a URL, even if the
  // URL is invalid.
  has_site_ = true;
  site_ = GetSiteForURL(browsing_instance_->browser_context(), url);

  // Now that we have a site, register it with the BrowsingInstance.  This
  // ensures that we won't create another SiteInstance for this site within
  // the same BrowsingInstance, because all same-site pages within a
  // BrowsingInstance can script each other.
  browsing_instance_->RegisterSiteInstance(this);

  if (process_)
    LockToOrigin();
}

const GURL& SiteInstanceImpl::GetSite() const {
  return site_;
}

bool SiteInstanceImpl::HasSite() const {
  return has_site_;
}

bool SiteInstanceImpl::HasRelatedSiteInstance(const GURL& url) {
  return browsing_instance_->HasSiteInstance(url);
}

SiteInstance* SiteInstanceImpl::GetRelatedSiteInstance(const GURL& url) {
  return browsing_instance_->GetSiteInstanceForURL(url);
}

bool SiteInstanceImpl::HasWrongProcessForURL(const GURL& url) const {
  // Having no process isn't a problem, since we'll assign it correctly.
  if (!HasProcess())
    return false;

  // If the URL to navigate to can be associated with any site instance,
  // we want to keep it in the same process.
  if (IsURLSameAsAnySiteInstance(url))
    return false;

  // If the site URL is an extension (e.g., for hosted apps or WebUI) but the
  // process is not (or vice versa), make sure we notice and fix it.
  GURL site_url = GetSiteForURL(browsing_instance_->browser_context(), url);
  return !RenderProcessHostImpl::IsSuitableHost(
      process_, browsing_instance_->browser_context(), site_url);
}

content::BrowserContext* SiteInstanceImpl::GetBrowserContext() const {
  return browsing_instance_->browser_context();
}

/*static*/
SiteInstance* SiteInstance::Create(content::BrowserContext* browser_context) {
  return new SiteInstanceImpl(new BrowsingInstance(browser_context));
}

/*static*/
SiteInstance* SiteInstance::CreateForURL(
    content::BrowserContext* browser_context, const GURL& url) {
  // This BrowsingInstance may be deleted if it returns an existing
  // SiteInstance.
  scoped_refptr<BrowsingInstance> instance(
      new BrowsingInstance(browser_context));
  return instance->GetSiteInstanceForURL(url);
}

/*static*/
GURL SiteInstanceImpl::GetSiteForURL(content::BrowserContext* browser_context,
                                     const GURL& real_url) {
  GURL url = SiteInstanceImpl::GetEffectiveURL(browser_context, real_url);

  // URLs with no host should have an empty site.
  GURL site;

  // TODO(creis): For many protocols, we should just treat the scheme as the
  // site, since there is no host.  e.g., file:, about:, chrome:

  // If the url has a host, then determine the site.
  if (url.has_host()) {
    // Only keep the scheme and registered domain as given by GetOrigin.  This
    // may also include a port, which we need to drop.
    site = url.GetOrigin();

    // Remove port, if any.
    if (site.has_port()) {
      GURL::Replacements rep;
      rep.ClearPort();
      site = site.ReplaceComponents(rep);
    }

    // If this URL has a registered domain, we only want to remember that part.
    std::string domain =
        net::RegistryControlledDomainService::GetDomainAndRegistry(url);
    if (!domain.empty()) {
      GURL::Replacements rep;
      rep.SetHostStr(domain);
      site = site.ReplaceComponents(rep);
    }
  }
  return site;
}

/*static*/
bool SiteInstance::IsSameWebSite(content::BrowserContext* browser_context,
                                 const GURL& real_url1,
                                 const GURL& real_url2) {
  GURL url1 = SiteInstanceImpl::GetEffectiveURL(browser_context, real_url1);
  GURL url2 = SiteInstanceImpl::GetEffectiveURL(browser_context, real_url2);

  // We infer web site boundaries based on the registered domain name of the
  // top-level page and the scheme.  We do not pay attention to the port if
  // one is present, because pages served from different ports can still
  // access each other if they change their document.domain variable.

  // Some special URLs will match the site instance of any other URL. This is
  // done before checking both of them for validity, since we want these URLs
  // to have the same site instance as even an invalid one.
  if (IsURLSameAsAnySiteInstance(url1) || IsURLSameAsAnySiteInstance(url2))
    return true;

  // If either URL is invalid, they aren't part of the same site.
  if (!url1.is_valid() || !url2.is_valid())
    return false;

  // If the schemes differ, they aren't part of the same site.
  if (url1.scheme() != url2.scheme())
    return false;

  return net::RegistryControlledDomainService::SameDomainOrHost(url1, url2);
}

/*static*/
GURL SiteInstanceImpl::GetEffectiveURL(
    content::BrowserContext* browser_context,
    const GURL& url) {
  return content::GetContentClient()->browser()->
      GetEffectiveURL(browser_context, url);
}

void SiteInstanceImpl::Observe(int type,
                               const content::NotificationSource& source,
                               const content::NotificationDetails& details) {
  DCHECK(type == content::NOTIFICATION_RENDERER_PROCESS_TERMINATED);
  content::RenderProcessHost* rph =
      content::Source<content::RenderProcessHost>(source).ptr();
  if (rph == process_)
    process_ = NULL;
}

void SiteInstanceImpl::LockToOrigin() {
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(switches::kEnableStrictSiteIsolation)) {
    ChildProcessSecurityPolicy* policy =
        ChildProcessSecurityPolicy::GetInstance();
    policy->LockToOrigin(process_->GetID(), site_);
  }
}

