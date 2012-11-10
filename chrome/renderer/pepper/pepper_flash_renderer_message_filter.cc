// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/pepper/pepper_flash_renderer_message_filter.h"

#include "chrome/renderer/pepper/ppb_pdf_impl.h"
#include "ppapi/proxy/ppapi_messages.h"

namespace chrome {

PepperFlashRendererMessageFilter::PepperFlashRendererMessageFilter(
    ppapi::host::PpapiHost* host)
    : InstanceMessageFilter(host) {
}

PepperFlashRendererMessageFilter::~PepperFlashRendererMessageFilter() {
}

bool PepperFlashRendererMessageFilter::OnInstanceMessageReceived(
    const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PepperFlashRendererMessageFilter, msg)
    IPC_MESSAGE_HANDLER(PpapiHostMsg_PPBFlash_InvokePrinting,
                        OnHostMsgInvokePrinting)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void PepperFlashRendererMessageFilter::OnHostMsgInvokePrinting(
    PP_Instance instance) {
  PPB_PDF_Impl::InvokePrintingForInstance(instance);
}

}  // namespace chrome
