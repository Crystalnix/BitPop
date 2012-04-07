/* Copyright (c) 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* From private/ppb_flash_fullscreen.idl modified Wed Dec 14 18:08:00 2011. */

#ifndef PPAPI_C_PRIVATE_PPB_FLASH_FULLSCREEN_H_
#define PPAPI_C_PRIVATE_PPB_FLASH_FULLSCREEN_H_

#include "ppapi/c/pp_bool.h"
#include "ppapi/c/pp_instance.h"
#include "ppapi/c/pp_macros.h"
#include "ppapi/c/pp_size.h"
#include "ppapi/c/pp_stdint.h"

#define PPB_FLASHFULLSCREEN_INTERFACE_0_1 "PPB_FlashFullscreen;0.1"
#define PPB_FLASHFULLSCREEN_INTERFACE PPB_FLASHFULLSCREEN_INTERFACE_0_1

/**
 * @file
 * This file defines the <code>PPB_FlashFullscreen</code> interface.
 */


/**
 * @addtogroup Interfaces
 * @{
 */
struct PPB_FlashFullscreen_0_1 {
  /**
   * Checks whether the plugin instance is currently in fullscreen mode.
   */
  PP_Bool (*IsFullscreen)(PP_Instance instance);
  /**
   * Switches the plugin instance to/from fullscreen mode. Returns PP_TRUE on
   * success, PP_FALSE on failure.
   *
   * This unbinds the current Graphics2D or Graphics3D. Pending flushes and
   * swapbuffers will execute as if the resource was off-screen. The transition
   * is asynchronous. During the transition, IsFullscreen will return PP_FALSE,
   * and no Graphics2D or Graphics3D can be bound. The transition ends at the
   * next DidChangeView when going into fullscreen mode. The transition out of
   * fullscreen mode is synchronous.
   *
   * Note: when switching to and from fullscreen, Graphics3D resources need to
   * be re-created. This is a current limitation that will be lifted in a later
   * revision.
   */
  PP_Bool (*SetFullscreen)(PP_Instance instance, PP_Bool fullscreen);
  /**
   * Gets the size of the screen in pixels. When going fullscreen, the instance
   * will be resized to that size.
   */
  PP_Bool (*GetScreenSize)(PP_Instance instance, struct PP_Size* size);
};

typedef struct PPB_FlashFullscreen_0_1 PPB_FlashFullscreen;
/**
 * @}
 */

#endif  /* PPAPI_C_PRIVATE_PPB_FLASH_FULLSCREEN_H_ */

