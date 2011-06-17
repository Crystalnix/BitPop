// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Multiply-included message file, so no include guard.

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/platform_file.h"
#include "base/values.h"
#include "chrome/common/extensions/update_manifest.h"
#include "content/common/common_param_traits.h"
#include "content/common/indexed_db_key.h"
#include "content/common/indexed_db_param_traits.h"
#include "content/common/serialized_script_value.h"
#include "ipc/ipc_message_macros.h"
#include "ipc/ipc_message_utils.h"
#include "printing/backend/print_backend.h"
#include "printing/page_range.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/rect.h"

#define IPC_MESSAGE_START UtilityMsgStart

IPC_STRUCT_TRAITS_BEGIN(printing::PageRange)
  IPC_STRUCT_TRAITS_MEMBER(from)
  IPC_STRUCT_TRAITS_MEMBER(to)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(printing::PrinterCapsAndDefaults)
  IPC_STRUCT_TRAITS_MEMBER(printer_capabilities)
  IPC_STRUCT_TRAITS_MEMBER(caps_mime_type)
  IPC_STRUCT_TRAITS_MEMBER(printer_defaults)
  IPC_STRUCT_TRAITS_MEMBER(defaults_mime_type)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(UpdateManifest::Result)
  IPC_STRUCT_TRAITS_MEMBER(extension_id)
  IPC_STRUCT_TRAITS_MEMBER(version)
  IPC_STRUCT_TRAITS_MEMBER(browser_min_version)
  IPC_STRUCT_TRAITS_MEMBER(package_hash)
  IPC_STRUCT_TRAITS_MEMBER(crx_url)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(UpdateManifest::Results)
  IPC_STRUCT_TRAITS_MEMBER(list)
  IPC_STRUCT_TRAITS_MEMBER(daystart_elapsed_seconds)
IPC_STRUCT_TRAITS_END()

//------------------------------------------------------------------------------
// Utility process messages:
// These are messages from the browser to the utility process.
// Tell the utility process to unpack the given extension file in its
// directory and verify that it is valid.
IPC_MESSAGE_CONTROL1(UtilityMsg_UnpackExtension,
                     FilePath /* extension_filename */)

// Tell the utility process to parse the given JSON data and verify its
// validity.
IPC_MESSAGE_CONTROL1(UtilityMsg_UnpackWebResource,
                     std::string /* JSON data */)

// Tell the utility process to parse the given xml document.
IPC_MESSAGE_CONTROL1(UtilityMsg_ParseUpdateManifest,
                     std::string /* xml document contents */)

// Tell the utility process to decode the given image data.
IPC_MESSAGE_CONTROL1(UtilityMsg_DecodeImage,
                     std::vector<unsigned char>)  // encoded image contents

// Tell the utility process to decode the given image data, which is base64
// encoded.
IPC_MESSAGE_CONTROL1(UtilityMsg_DecodeImageBase64,
                     std::string)  // base64 encoded image contents

// Tell the utility process to render the given PDF into a metafile.
IPC_MESSAGE_CONTROL5(UtilityMsg_RenderPDFPagesToMetafile,
                     base::PlatformFile,       // PDF file
                     FilePath,                 // Location for output metafile
                     gfx::Rect,                // Render Area
                     int,                      // DPI
                     std::vector<printing::PageRange>)

// Tell the utility process to extract the given IDBKeyPath from the
// SerializedScriptValue vector and reply with the corresponding IDBKeys.
IPC_MESSAGE_CONTROL3(UtilityMsg_IDBKeysFromValuesAndKeyPath,
                     int,     // id
                     std::vector<SerializedScriptValue>,
                     string16)  // IDBKeyPath

IPC_MESSAGE_CONTROL3(UtilityMsg_InjectIDBKey,
                     IndexedDBKey /* key */,
                     SerializedScriptValue /* value */,
                     string16 /* key path*/)

// Tell the utility process to parse a JSON string into a Value object.
IPC_MESSAGE_CONTROL1(UtilityMsg_ParseJSON,
                     std::string /* JSON to parse */)

// Tells the utility process that it's running in batch mode.
IPC_MESSAGE_CONTROL0(UtilityMsg_BatchMode_Started)

// Tells the utility process that it can shutdown.
IPC_MESSAGE_CONTROL0(UtilityMsg_BatchMode_Finished)

// Tells the utility process to get capabilities and defaults for the specified
// printer. Used on Windows to isolate the service process from printer driver
// crashes by executing this in a separate process. This does not run in a
// sandbox.
IPC_MESSAGE_CONTROL1(UtilityMsg_GetPrinterCapsAndDefaults,
                     std::string /* printer name */)

//------------------------------------------------------------------------------
// Utility process host messages:
// These are messages from the utility process to the browser.
// Reply when the utility process is done unpacking an extension.  |manifest|
// is the parsed manifest.json file.
// The unpacker should also have written out files containing the decoded
// images and message catalogs from the extension. See ExtensionUnpacker for
// details.
IPC_MESSAGE_CONTROL1(UtilityHostMsg_UnpackExtension_Succeeded,
                     DictionaryValue /* manifest */)

// Reply when the utility process has failed while unpacking an extension.
// |error_message| is a user-displayable explanation of what went wrong.
IPC_MESSAGE_CONTROL1(UtilityHostMsg_UnpackExtension_Failed,
                     std::string /* error_message, if any */)

// Reply when the utility process is done unpacking and parsing JSON data
// from a web resource.
IPC_MESSAGE_CONTROL1(UtilityHostMsg_UnpackWebResource_Succeeded,
                     DictionaryValue /* json data */)

// Reply when the utility process has failed while unpacking and parsing a
// web resource.  |error_message| is a user-readable explanation of what
// went wrong.
IPC_MESSAGE_CONTROL1(UtilityHostMsg_UnpackWebResource_Failed,
                     std::string /* error_message, if any */)

// Reply when the utility process has succeeded in parsing an update manifest
// xml document.
IPC_MESSAGE_CONTROL1(UtilityHostMsg_ParseUpdateManifest_Succeeded,
                     UpdateManifest::Results /* updates */)

// Reply when an error occured parsing the update manifest. |error_message|
// is a description of what went wrong suitable for logging.
IPC_MESSAGE_CONTROL1(UtilityHostMsg_ParseUpdateManifest_Failed,
                     std::string /* error_message, if any */)

// Reply when the utility process has succeeded in decoding the image.
IPC_MESSAGE_CONTROL1(UtilityHostMsg_DecodeImage_Succeeded,
                     SkBitmap)  // decoded image

// Reply when an error occured decoding the image.
IPC_MESSAGE_CONTROL0(UtilityHostMsg_DecodeImage_Failed)

// Reply when the utility process has succeeded in rendering the PDF.
IPC_MESSAGE_CONTROL1(UtilityHostMsg_RenderPDFPagesToMetafile_Succeeded,
                     int)       // Highest rendered page number

// Reply when an error occured rendering the PDF.
IPC_MESSAGE_CONTROL0(UtilityHostMsg_RenderPDFPagesToMetafile_Failed)

#if defined(OS_WIN)
// Request that the given font be loaded by the host so it's cached by the
// OS. Please see ChildProcessHost::PreCacheFont for details.
IPC_SYNC_MESSAGE_CONTROL1_0(UtilityHostMsg_PreCacheFont,
                            LOGFONT /* font data */)
#endif  // defined(OS_WIN)

// Reply when the utility process has succeeded in obtaining the value for
// IDBKeyPath.
IPC_MESSAGE_CONTROL2(UtilityHostMsg_IDBKeysFromValuesAndKeyPath_Succeeded,
                     int /* id */,
                     std::vector<IndexedDBKey> /* value */)

// Reply when the utility process has failed in obtaining the value for
// IDBKeyPath.
IPC_MESSAGE_CONTROL1(UtilityHostMsg_IDBKeysFromValuesAndKeyPath_Failed,
                     int /* id */)

// Reply when the utility process has finished injecting an IDBKey into
// a SerializedScriptValue.
IPC_MESSAGE_CONTROL1(UtilityHostMsg_InjectIDBKey_Finished,
                     SerializedScriptValue /* new value */)

// Reply when the utility process successfully parsed a JSON string.
//
// WARNING: The result can be of any Value subclass type, but we can't easily
// pass indeterminate value types by const object reference with our IPC macros,
// so we put the result Value into a ListValue. Handlers should examine the
// first (and only) element of the ListValue for the actual result.
IPC_MESSAGE_CONTROL1(UtilityHostMsg_ParseJSON_Succeeded,
                     ListValue)

// Reply when the utility process failed in parsing a JSON string.
IPC_MESSAGE_CONTROL1(UtilityHostMsg_ParseJSON_Failed,
                     std::string /* error message, if any*/)

// Reply when the utility process has succeeded in obtaining the printer
// capabilities and defaults.
IPC_MESSAGE_CONTROL2(UtilityHostMsg_GetPrinterCapsAndDefaults_Succeeded,
                     std::string /* printer name */,
                     printing::PrinterCapsAndDefaults)

// Reply when the utility process has failed to obtain the printer
// capabilities and defaults.
IPC_MESSAGE_CONTROL1(UtilityHostMsg_GetPrinterCapsAndDefaults_Failed,
                     std::string /* printer name */)
