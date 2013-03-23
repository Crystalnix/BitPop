// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Multiply-included file, no traditional include guard.
#include <string>
#include <vector>

#include "content/public/common/common_param_traits.h"
#include "content/shell/shell_webpreferences.h"
#include "ipc/ipc_message_macros.h"
#include "third_party/skia/include/core/SkBitmap.h"

#define IPC_MESSAGE_START ShellMsgStart

IPC_STRUCT_TRAITS_BEGIN(content::ShellWebPreferences)
  IPC_STRUCT_TRAITS_MEMBER(allowUniversalAccessFromFileURLs)
  IPC_STRUCT_TRAITS_MEMBER(DOMPasteAllowed)
  IPC_STRUCT_TRAITS_MEMBER(javaScriptCanAccessClipboard)
  IPC_STRUCT_TRAITS_MEMBER(XSSAuditorEnabled)
IPC_STRUCT_TRAITS_END()

// Sets the current working directory to use for layout tests.
IPC_MESSAGE_ROUTED1(ShellViewMsg_SetCurrentWorkingDirectory,
                    FilePath /* current_working_directory */)

// Tells the render view to capture a text dump of the page. The render view
// responds with a ShellViewHostMsg_TextDump.
IPC_MESSAGE_ROUTED3(ShellViewMsg_CaptureTextDump,
                    bool /* as_text */,
                    bool /* printing */,
                    bool /* recursive */)

// Tells the render view to capture an image of the page. The render view
// responds with a ShellViewHostMsg_ImageDump.
IPC_MESSAGE_ROUTED1(ShellViewMsg_CaptureImageDump,
                    std::string /* expected pixel hash */)

// Tells the renderer to reset all test runners.
IPC_MESSAGE_CONTROL0(ShellViewMsg_ResetAll)

// Sets the path to the WebKit checkout.
IPC_MESSAGE_CONTROL1(ShellViewMsg_SetWebKitSourceDir,
                     FilePath /* webkit source dir */)

// Send a text dump of the WebContents to the render host.
IPC_MESSAGE_ROUTED1(ShellViewHostMsg_TextDump,
                    std::string /* dump */)

// Send an image dump of the WebContents to the render host.
IPC_MESSAGE_ROUTED2(ShellViewHostMsg_ImageDump,
                    std::string /* actual pixel hash */,
                    SkBitmap /* image */)

// The main frame of the render view finished loading.
IPC_MESSAGE_ROUTED0(ShellViewHostMsg_DidFinishLoad)

// Print a message from a layout test runner.
IPC_MESSAGE_ROUTED1(ShellViewHostMsg_PrintMessage,
                    std::string /* message */)

// Read a file and returns its contents.
IPC_SYNC_MESSAGE_ROUTED1_1(ShellViewHostMsg_ReadFileToString,
                           FilePath /* local path */,
                           std::string /* contents */)

// The following messages correspond to methods of the testRunner.
IPC_MESSAGE_ROUTED0(ShellViewHostMsg_NotifyDone)
IPC_MESSAGE_ROUTED0(ShellViewHostMsg_DumpAsText)
IPC_MESSAGE_ROUTED0(ShellViewHostMsg_DumpChildFramesAsText)
IPC_MESSAGE_ROUTED0(ShellViewHostMsg_SetPrinting)
IPC_MESSAGE_ROUTED1(
    ShellViewHostMsg_SetShouldStayOnPageAfterHandlingBeforeUnload,
    bool /* should_stay_on_page */)
IPC_MESSAGE_ROUTED0(ShellViewHostMsg_WaitUntilDone)
IPC_MESSAGE_ROUTED1(ShellViewHostMsg_OverridePreferences,
                    content::ShellWebPreferences /* preferences */)
IPC_MESSAGE_ROUTED0(ShellViewHostMsg_CanOpenWindows)
IPC_MESSAGE_ROUTED0(ShellViewHostMsg_ShowWebInspector)
IPC_MESSAGE_ROUTED0(ShellViewHostMsg_CloseWebInspector)
IPC_SYNC_MESSAGE_ROUTED1_1(ShellViewHostMsg_RegisterIsolatedFileSystem,
                           std::vector<FilePath> /* absolute_filenames */,
                           std::string /* filesystem_id */)

IPC_MESSAGE_ROUTED2(ShellViewHostMsg_NotImplemented,
                    std::string /* object_name */,
                    std::string /* property_name */)
