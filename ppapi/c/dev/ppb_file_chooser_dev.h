/* Copyright (c) 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* From dev/ppb_file_chooser_dev.idl modified Mon Nov 14 10:36:01 2011. */

#ifndef PPAPI_C_DEV_PPB_FILE_CHOOSER_DEV_H_
#define PPAPI_C_DEV_PPB_FILE_CHOOSER_DEV_H_

#include "ppapi/c/pp_bool.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/pp_instance.h"
#include "ppapi/c/pp_macros.h"
#include "ppapi/c/pp_resource.h"
#include "ppapi/c/pp_stdint.h"
#include "ppapi/c/pp_var.h"

#define PPB_FILECHOOSER_DEV_INTERFACE_0_5 "PPB_FileChooser(Dev);0.5"
#define PPB_FILECHOOSER_DEV_INTERFACE PPB_FILECHOOSER_DEV_INTERFACE_0_5

/**
 * @file
 * This file defines the <code>PPB_FileChooser_Dev</code> interface.
 */


/**
 * @addtogroup Enums
 * @{
 */
/**
 * This enumeration contains constants to control the behavior of the file
 * chooser dialog.
 */
typedef enum {
  /**
   * Mode for choosing a single existing file.
   */
  PP_FILECHOOSERMODE_OPEN = 0,
  /**
   * Mode for choosing multiple existing files.
   */
  PP_FILECHOOSERMODE_OPENMULTIPLE = 1
} PP_FileChooserMode_Dev;
PP_COMPILE_ASSERT_SIZE_IN_BYTES(PP_FileChooserMode_Dev, 4);
/**
 * @}
 */

/**
 * @addtogroup Interfaces
 * @{
 */
struct PPB_FileChooser_Dev_0_5 {
  /**
   * This function creates a file chooser dialog resource.  The chooser is
   * associated with a particular instance, so that it may be positioned on the
   * screen relative to the tab containing the instance.
   *
   * @param[in] instance A <code>PP_Instance</code> identifying one instance
   * of a module.
   * @param[in] mode A <code>PP_FileChooserMode_Dev</code> value that controls
   * the behavior of the file chooser dialog.
   * @param[in] accept_mime_types A comma-separated list of MIME types such as
   * "audio/ *,text/plain" (note there should be no space between the '/' and
   * the '*', but one is added to avoid confusing C++ comments).  The dialog
   * may restrict selectable files to the specified MIME types. An empty string
   * or an undefined var may be given to indicate that all types should be
   * accepted.
   *
   * @return A <code>PP_Resource</code> containing the file chooser if
   * successful or 0 if it could not be created.
   */
  PP_Resource (*Create)(PP_Instance instance,
                        PP_FileChooserMode_Dev mode,
                        struct PP_Var accept_mime_types);
  /**
   * Determines if the provided resource is a file chooser.
   *
   * @param[in] resource A <code>PP_Resource</code> corresponding to a generic
   * resource.
   *
   * @return A <code>PP_Bool</code> that is <code>PP_TRUE</code> if the given
   * resource is a file chooser resource, otherwise <code>PP_FALSE</code>.
   */
  PP_Bool (*IsFileChooser)(PP_Resource resource);
  /**
   * This function displays a previously created file chooser resource as a
   * dialog box, prompting the user to choose a file or files. This function
   * must be called in response to a user gesture, such as a mouse click or
   * touch event. The callback is called with PP_OK on successful completion
   * with a file (or files) selected, PP_ERROR_USERCANCEL if the user selected
   * no file, or another error code from pp_errors.h on failure.
   *
   * @param[in] chooser The file chooser resource.
   * @param[in] callback A <code>CompletionCallback</code> to be called after
   * the user has closed the file chooser dialog.
   *
   * @return PP_OK_COMPLETIONPENDING if request to show the dialog was
   * successful, another error code from pp_errors.h on failure.
   */
  int32_t (*Show)(PP_Resource chooser, struct PP_CompletionCallback callback);
  /**
   * After a successful completion callback call from Show, this method may be
   * used to query the chosen files.  It should be called in a loop until it
   * returns 0.  Their file system type will be PP_FileSystemType_External.  If
   * the user chose no files or canceled the dialog, then this method will
   * simply return 0 the first time it is called.
   *
   * @param[in] chooser The file chooser resource.
   *
   * @return A <code>PP_Resource</code> containing the next file chosen by the
   * user, or 0 if there are no more files.
   */
  PP_Resource (*GetNextChosenFile)(PP_Resource chooser);
};

typedef struct PPB_FileChooser_Dev_0_5 PPB_FileChooser_Dev;
/**
 * @}
 */

#endif  /* PPAPI_C_DEV_PPB_FILE_CHOOSER_DEV_H_ */

