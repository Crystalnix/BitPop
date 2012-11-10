// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_WIN_ELEVATED_CONTROLLER_H_
#define REMOTING_HOST_WIN_ELEVATED_CONTROLLER_H_

#include <atlbase.h>
#include <atlcom.h>
#include <atlctl.h>

#include "remoting/base/scoped_sc_handle_win.h"

// MIDL-generated declarations.
#include "remoting/host/elevated_controller.h"

namespace remoting {

class ATL_NO_VTABLE __declspec(uuid(DAEMON_CONTROLLER_CLSID)) ElevatedController
    : public ATL::CComObjectRootEx<ATL::CComSingleThreadModel>,
      public ATL::CComCoClass<ElevatedController,
                              &__uuidof(ElevatedController)>,
      public ATL::IDispatchImpl<IDaemonControl2, &IID_IDaemonControl2,
                                &LIBID_ChromotingElevatedControllerLib, 1, 1> {
 public:
  ElevatedController();

  HRESULT FinalConstruct();
  void FinalRelease();

  // IDaemonControl implementation.
  STDMETHOD(GetConfig)(BSTR* config_out);
  STDMETHOD(GetVersion)(BSTR* version_out);
  STDMETHOD(SetConfig)(BSTR config);
  STDMETHOD(SetOwnerWindow)(LONG_PTR owner_window);
  STDMETHOD(StartDaemon)();
  STDMETHOD(StopDaemon)();
  STDMETHOD(UpdateConfig)(BSTR config);

  // IDaemonControl2 implementation.
  STDMETHOD(GetUsageStatsConsent)(BOOL* allowed, BOOL* set_by_policy);
  STDMETHOD(SetUsageStatsConsent)(BOOL allowed);

  DECLARE_NO_REGISTRY()

 private:
  HRESULT OpenService(ScopedScHandle* service_out);

  BEGIN_COM_MAP(ElevatedController)
    COM_INTERFACE_ENTRY(IDaemonControl)
    COM_INTERFACE_ENTRY(IDaemonControl2)
    COM_INTERFACE_ENTRY(IDispatch)
  END_COM_MAP()

  // Handle of the owner window (if any) for any UI to be shown.
  HWND owner_window_;

  DECLARE_PROTECT_FINAL_CONSTRUCT()
};

OBJECT_ENTRY_AUTO(__uuidof(ElevatedController), ElevatedController)

} // namespace remoting

#endif  // REMOTING_HOST_WIN_ELEVATED_CONTROLLER_H_
