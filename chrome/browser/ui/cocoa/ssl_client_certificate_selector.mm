// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ssl_client_certificate_selector.h"

#import <SecurityInterface/SFChooseIdentityPanel.h>

#include <vector>

#include "base/logging.h"
#include "base/memory/ref_counted.h"
#import "base/memory/scoped_nsobject.h"
#include "base/string_util.h"
#include "base/sys_string_conversions.h"
#include "base/utf_string_conversions.h"
#import "chrome/browser/ui/cocoa/constrained_window_mac.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "content/browser/ssl/ssl_client_auth_handler.h"
#include "content/public/browser/browser_thread.h"
#include "grit/generated_resources.h"
#include "net/base/x509_certificate.h"
#include "ui/base/l10n/l10n_util_mac.h"

using content::BrowserThread;

@interface SFChooseIdentityPanel (SystemPrivate)
// A system-private interface that dismisses a panel whose sheet was started by
// beginSheetForWindow:modalDelegate:didEndSelector:contextInfo:identities:message:
// as though the user clicked the button identified by returnCode. Verified
// present in 10.5, 10.6, and 10.7.
- (void)_dismissWithCode:(NSInteger)code;
@end

namespace {
class NotificationProxy;
}  // namespace

@interface SSLClientCertificateSelectorCocoa : NSObject {
 @private
  // The handler to report back to.
  scoped_refptr<SSLClientAuthHandler> handler_;
  // The certificate request we serve.
  scoped_refptr<net::SSLCertRequestInfo> certRequestInfo_;
  // The list of identities offered to the user.
  scoped_nsobject<NSMutableArray> identities_;
  // The corresponding list of certificates.
  std::vector<scoped_refptr<net::X509Certificate> > certificates_;
  // The currently open dialog.
  ConstrainedWindow* window_;
  // A C++ object to proxy SSLClientAuthObserver notifications to us.
  scoped_ptr<NotificationProxy> observer_;
}

- (id)initWithHandler:(SSLClientAuthHandler*)handler
      certRequestInfo:(net::SSLCertRequestInfo*)certRequestInfo;
- (void)onNotification;
- (void)displayDialog:(TabContentsWrapper*)wrapper;
@end

namespace {

class ConstrainedSFChooseIdentityPanel
    : public ConstrainedWindowMacDelegateSystemSheet {
 public:
  ConstrainedSFChooseIdentityPanel(SFChooseIdentityPanel* panel,
                                   id delegate, SEL didEndSelector,
                                   NSArray* identities, NSString* message)
      : ConstrainedWindowMacDelegateSystemSheet(delegate, didEndSelector),
        identities_([identities retain]),
        message_([message retain]) {
    set_sheet(panel);
  }

  virtual ~ConstrainedSFChooseIdentityPanel() {
  }

  // ConstrainedWindowMacDelegateSystemSheet implementation:
  virtual void DeleteDelegate() {
    // As required by ConstrainedWindowMacDelegate, close the sheet if
    // it's still open.
    if (is_sheet_open()) {
      [sheet() _dismissWithCode:NSFileHandlingPanelCancelButton];
    }

    // Now that the panel has closed, release it. Note that the autorelease is
    // needed. After this callback returns, the panel is still accessed, so a
    // normal release crashes.
    [sheet() autorelease];

    delete this;
  }

  // SFChooseIdentityPanel's beginSheetForWindow: method has more arguments
  // than the usual one. Also pass the panel through contextInfo argument
  // because the callback has the wrong signature.
  virtual NSArray* GetSheetParameters(id delegate, SEL didEndSelector) {
    return [NSArray arrayWithObjects:
        [NSNull null],  // window, must be [NSNull null]
        delegate,
        [NSValue valueWithPointer:didEndSelector],
        [NSValue valueWithPointer:sheet()],
        identities_.get(),
        message_.get(),
        nil];
  }

 private:
  scoped_nsobject<NSArray> identities_;
  scoped_nsobject<NSString> message_;
  DISALLOW_COPY_AND_ASSIGN(ConstrainedSFChooseIdentityPanel);
};

class NotificationProxy : public SSLClientAuthObserver {
 public:
  NotificationProxy(net::SSLCertRequestInfo* cert_request_info,
                    SSLClientAuthHandler* handler,
                    SSLClientCertificateSelectorCocoa* controller)
      : SSLClientAuthObserver(cert_request_info, handler),
        controller_(controller) {
  }

  // SSLClientAuthObserver implementation:
  virtual void OnCertSelectedByNotification() {
    [controller_ onNotification];
  }

 private:
  SSLClientCertificateSelectorCocoa* controller_;
};

}  // namespace

namespace browser {

void ShowSSLClientCertificateSelector(
    TabContentsWrapper* wrapper,
    net::SSLCertRequestInfo* cert_request_info,
    SSLClientAuthHandler* delegate) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  SSLClientCertificateSelectorCocoa* selector =
      [[[SSLClientCertificateSelectorCocoa alloc]
          initWithHandler:delegate
          certRequestInfo:cert_request_info] autorelease];
  [selector displayDialog:wrapper];
}

}  // namespace browser

@implementation SSLClientCertificateSelectorCocoa

- (id)initWithHandler:(SSLClientAuthHandler*)handler
      certRequestInfo:(net::SSLCertRequestInfo*)certRequestInfo {
  DCHECK(handler);
  DCHECK(certRequestInfo);
  if ((self = [super init])) {
    handler_ = handler;
    certRequestInfo_ = certRequestInfo;
    window_ = NULL;
    observer_.reset(new NotificationProxy(certRequestInfo, handler, self));
  }
  return self;
}

- (void)sheetDidEnd:(NSWindow*)parent
         returnCode:(NSInteger)returnCode
            context:(void*)context {
  DCHECK(context);
  SFChooseIdentityPanel* panel = static_cast<SFChooseIdentityPanel*>(context);

  net::X509Certificate* cert = NULL;
  if (returnCode == NSFileHandlingPanelOKButton) {
    NSUInteger index = [identities_ indexOfObject:(id)[panel identity]];
    if (index != NSNotFound)
      cert = certificates_[index];
    else
      NOTREACHED();
  }

  // Finally, tell the backend which identity (or none) the user selected.
  observer_->StopObserving();
  if (handler_) {
    handler_->CertificateSelected(cert);
    handler_ = NULL;
  }
  // Close the constrained window.
  DCHECK(window_);
  window_->CloseConstrainedWindow();
}

- (void)onNotification {
  handler_ = NULL;
  window_->CloseConstrainedWindow();
}

- (void)displayDialog:(TabContentsWrapper*)wrapper {
  DCHECK(!window_);
  // Create an array of CFIdentityRefs for the certificates:
  size_t numCerts = certRequestInfo_->client_certs.size();
  identities_.reset([[NSMutableArray alloc] initWithCapacity:numCerts]);
  for (size_t i = 0; i < numCerts; ++i) {
    SecCertificateRef cert;
    cert = certRequestInfo_->client_certs[i]->os_cert_handle();
    SecIdentityRef identity;
    if (SecIdentityCreateWithCertificate(NULL, cert, &identity) == noErr) {
      [identities_ addObject:(id)identity];
      CFRelease(identity);
      certificates_.push_back(certRequestInfo_->client_certs[i]);
    }
  }

  // Get the message to display:
  NSString* title = l10n_util::GetNSString(IDS_CLIENT_CERT_DIALOG_TITLE);
  NSString* message = l10n_util::GetNSStringF(
      IDS_CLIENT_CERT_DIALOG_TEXT,
      ASCIIToUTF16(certRequestInfo_->host_and_port));

  // Create and set up a system choose-identity panel.
  SFChooseIdentityPanel* panel = [[SFChooseIdentityPanel alloc] init];
  [panel setInformativeText:message];
  [panel setDefaultButtonTitle:l10n_util::GetNSString(IDS_OK)];
  [panel setAlternateButtonTitle:l10n_util::GetNSString(IDS_CANCEL)];
  SecPolicyRef sslPolicy;
  if (net::X509Certificate::CreateSSLClientPolicy(&sslPolicy) == noErr) {
    [panel setPolicies:(id)sslPolicy];
    CFRelease(sslPolicy);
  }

  window_ = new ConstrainedWindowMac(
      wrapper,
      new ConstrainedSFChooseIdentityPanel(
          panel, self,
          @selector(sheetDidEnd:returnCode:context:),
          identities_, title));
  observer_->StartObserving();
  // Note: SFChooseIdentityPanel does not take a reference to itself while the
  // sheet is open. ConstrainedSFChooseIdentityPanel will release ownership
  // on destruction.
}

@end
