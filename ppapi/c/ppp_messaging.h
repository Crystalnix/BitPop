/* Copyright (c) 2011 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef PPAPI_C_PPP_MESSAGING_H_
#define PPAPI_C_PPP_MESSAGING_H_

#include "ppapi/c/pp_instance.h"

struct PP_Var;

#define PPP_MESSAGING_INTERFACE "PPP_Messaging;0.1"

/**
 * @file
 * This file defines the PPP_Messaging interface containing pointers to
 * functions that you must implement to handle postMessage messages
 * on the associated DOM element.
 *
 */

/** @addtogroup Interfaces
 * @{
 */

/**
 * The PPP_Messaging interface contains pointers to functions that you must
 * implement to handle postMessage events on the associated DOM element.
 */
struct PPP_Messaging {
  /**
   * HandleMessage is a pointer to a function that the browser calls when
   * PostMessage() is invoked on the DOM element for the module instance in
   * JavaScript. Note that PostMessage() in the JavaScript interface is
   * asynchronous, meaning JavaScript execution will not be blocked while
   * HandleMessage() is processing the message.
   *
   * @param[in] instance A PP_Instance indentifying one instance of a module.
   * @param[in] message A PP_Var containing the data to be sent to JavaScript.
   * Message can have an int32_t, double, bool, or string value (objects
   * are not supported).
   *
   * <strong>Example:</strong>
   *
   * The following JavaScript code invokes HandleMessage, passing the module
   * instance on which it was invoked, with <code>message</code> being a
   * string PP_Var containing "Hello world!"
   *
   * @code
   *
   * <body>
   *   <object id="plugin"
   *           type="application/x-ppapi-postMessage-example"/>
   *   <script type="text/javascript">
   *     document.getElementById('plugin').postMessage("Hello world!");
   *   </script>
   * </body>
   *
   * @endcode
   *
   */
  void (*HandleMessage)(PP_Instance instance, struct PP_Var message);
};
/**
 * @}
 */
#endif  /* PPAPI_C_PPP_MESSAGING_H_ */

