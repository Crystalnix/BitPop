// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/ssl/ssl_client_auth_handler.h"

#include "base/bind.h"
#include "content/browser/renderer_host/resource_dispatcher_host.h"
#include "content/browser/renderer_host/resource_dispatcher_host_request_info.h"
#include "content/browser/ssl/ssl_client_auth_notification_details.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/notification_service.h"
#include "net/base/x509_certificate.h"
#include "net/http/http_transaction_factory.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"

using content::BrowserThread;

SSLClientAuthHandler::SSLClientAuthHandler(
    net::URLRequest* request,
    net::SSLCertRequestInfo* cert_request_info)
    : request_(request),
      http_network_session_(
          request_->context()->http_transaction_factory()->GetSession()),
      cert_request_info_(cert_request_info) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
}

SSLClientAuthHandler::~SSLClientAuthHandler() {
  // If we were simply dropped, then act as if we selected no certificate.
  DoCertificateSelected(NULL);
}

void SSLClientAuthHandler::OnRequestCancelled() {
  request_ = NULL;
}

void SSLClientAuthHandler::SelectCertificate() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  int render_process_host_id;
  int render_view_host_id;
  if (!ResourceDispatcherHost::RenderViewForRequest(request_,
                                                    &render_process_host_id,
                                                    &render_view_host_id))
    NOTREACHED();

  // If the RVH does not exist by the time this task gets run, then the task
  // will be dropped and the scoped_refptr to SSLClientAuthHandler will go
  // away, so we do not leak anything. The destructor takes care of ensuring
  // the net::URLRequest always gets a response.
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(
          &SSLClientAuthHandler::DoSelectCertificate, this,
          render_process_host_id, render_view_host_id));
}

// Sends an SSL_CLIENT_AUTH_CERT_SELECTED notification and notifies the IO
// thread that we have selected a cert.
void SSLClientAuthHandler::CertificateSelected(net::X509Certificate* cert) {
  VLOG(1) << this << " CertificateSelected " << cert;
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  SSLClientAuthNotificationDetails details(cert_request_info_, this, cert);
  content::NotificationService* service =
      content::NotificationService::current();
  service->Notify(content::NOTIFICATION_SSL_CLIENT_AUTH_CERT_SELECTED,
                  content::Source<net::HttpNetworkSession>(
                      http_network_session()),
                  content::Details<SSLClientAuthNotificationDetails>(&details));

  CertificateSelectedNoNotify(cert);
}

// Notifies the IO thread that we have selected a cert.
void SSLClientAuthHandler::CertificateSelectedNoNotify(
    net::X509Certificate* cert) {
  VLOG(1) << this << " CertificateSelectedNoNotify " << cert;
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(
          &SSLClientAuthHandler::DoCertificateSelected, this,
          make_scoped_refptr(cert)));
}

void SSLClientAuthHandler::DoCertificateSelected(net::X509Certificate* cert) {
  VLOG(1) << this << " DoCertificateSelected " << cert;
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  // request_ could have been NULLed if the request was cancelled while the
  // user was choosing a cert, or because we have already responded to the
  // certificate.
  if (request_) {
    request_->ContinueWithCertificate(cert);

    ResourceDispatcherHostRequestInfo* info =
        ResourceDispatcherHost::InfoForRequest(request_);
    if (info)
      info->set_ssl_client_auth_handler(NULL);

    request_ = NULL;
  }
}

void SSLClientAuthHandler::DoSelectCertificate(
    int render_process_host_id, int render_view_host_id) {
  content::GetContentClient()->browser()->SelectClientCertificate(
      render_process_host_id, render_view_host_id, this);
}

SSLClientAuthObserver::SSLClientAuthObserver(
    net::SSLCertRequestInfo* cert_request_info,
    SSLClientAuthHandler* handler)
    : cert_request_info_(cert_request_info), handler_(handler) {
}

SSLClientAuthObserver::~SSLClientAuthObserver() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

void SSLClientAuthObserver::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  VLOG(1) << "SSLClientAuthObserver::Observe " << this << " " << handler_.get();
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(type == content::NOTIFICATION_SSL_CLIENT_AUTH_CERT_SELECTED);

  SSLClientAuthNotificationDetails* auth_details =
      content::Details<SSLClientAuthNotificationDetails>(details).ptr();

  if (auth_details->IsSameHandler(handler_.get())) {
    VLOG(1) << "got notification from ourself " << handler_.get();
    return;
  }

  if (!auth_details->IsSameHost(cert_request_info_))
    return;

  VLOG(1) << this << " got matching notification for "
          << handler_.get() << ", selecting cert "
          << auth_details->selected_cert();
  StopObserving();
  handler_->CertificateSelectedNoNotify(auth_details->selected_cert());
  OnCertSelectedByNotification();
}

void SSLClientAuthObserver::StartObserving() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  notification_registrar_.Add(
      this, content::NOTIFICATION_SSL_CLIENT_AUTH_CERT_SELECTED,
      content::Source<net::HttpNetworkSession>(
          handler_->http_network_session()));
}

void SSLClientAuthObserver::StopObserving() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  notification_registrar_.RemoveAll();
}
