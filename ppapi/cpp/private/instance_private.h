// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_CPP_PRIVATE_INSTANCE_PRIVATE_H_
#define PPAPI_CPP_PRIVATE_INSTANCE_PRIVATE_H_

/**
 * @file
 * Defines the API ...
 *
 * @addtogroup CPP
 * @{
 */

#include "ppapi/cpp/instance.h"

/** The C++ interface to the Pepper API. */
namespace pp {

class VarPrivate;

class InstancePrivate : public Instance {
 public:
  explicit InstancePrivate(PP_Instance instance);
  virtual ~InstancePrivate();

  // @{
  /// @name PPP_Instance_Private methods for the plugin to override:

  /// See PPP_Instance_Private.GetInstanceObject.
  // TODO(dmichael):  Put this virtual function here and remove the
  //                  'Var GetInstanceObject()' version from pp::Instance.
  //virtual VarPrivate GetInstanceObject();

  // @}

  // @{
  /// @name PPB_Instance_Private methods for querying the browser:

  /// See PPB_Instance_Private.GetWindowObject.
  VarPrivate GetWindowObject();

  /// See PPB_Instance_Private.GetOwnerElementObject.
  VarPrivate GetOwnerElementObject();

  /// See PPB_Instance.ExecuteScript.
  VarPrivate ExecuteScript(const VarPrivate& script,
                           VarPrivate* exception = NULL);

  // @}
};

}  // namespace pp

/**
 * @}
 * End addtogroup CPP
 */
#endif  // PPAPI_CPP_PRIVATE_INSTANCE_PRIVATE_H_
