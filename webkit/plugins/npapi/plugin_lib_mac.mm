// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Carbon/Carbon.h>

#include "webkit/plugins/npapi/plugin_lib.h"

#include "base/mac/scoped_cftyperef.h"
#include "base/memory/scoped_ptr.h"
#include "base/native_library.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "base/sys_string_conversions.h"
#include "base/utf_string_conversions.h"
#include "webkit/plugins/npapi/plugin_list.h"

using base::mac::ScopedCFTypeRef;

namespace webkit {
namespace npapi {

namespace {

const short kSTRTypeDefinitionResourceID = 128;
const short kSTRTypeDescriptionResourceID = 127;
const short kSTRPluginDescriptionResourceID = 126;

NSDictionary* GetMIMETypes(CFBundleRef bundle) {
  NSString* mime_filename =
      (NSString*)CFBundleGetValueForInfoDictionaryKey(bundle,
                     CFSTR("WebPluginMIMETypesFilename"));

  if (mime_filename) {

    // get the file

    NSString* mime_path =
        [NSString stringWithFormat:@"%@/Library/Preferences/%@",
         NSHomeDirectory(), mime_filename];
    NSDictionary* mime_file_dict =
        [NSDictionary dictionaryWithContentsOfFile:mime_path];

    // is it valid?

    bool valid_file = false;
    if (mime_file_dict) {
      NSString* l10n_name =
          [mime_file_dict objectForKey:@"WebPluginLocalizationName"];
      NSString* preferred_l10n = [[NSLocale currentLocale] localeIdentifier];
      if ([l10n_name isEqualToString:preferred_l10n])
        valid_file = true;
    }

    if (valid_file)
      return [mime_file_dict objectForKey:@"WebPluginMIMETypes"];

    // dammit, I didn't want to have to do this

    typedef void (*CreateMIMETypesPrefsPtr)(void);
    CreateMIMETypesPrefsPtr create_prefs_file =
        (CreateMIMETypesPrefsPtr)CFBundleGetFunctionPointerForName(
        bundle, CFSTR("BP_CreatePluginMIMETypesPreferences"));
    if (!create_prefs_file)
      return nil;
    create_prefs_file();

    // one more time

    mime_file_dict = [NSDictionary dictionaryWithContentsOfFile:mime_path];
    if (mime_file_dict)
      return [mime_file_dict objectForKey:@"WebPluginMIMETypes"];
    else
      return nil;

  } else {
    return (NSDictionary*)CFBundleGetValueForInfoDictionaryKey(bundle,
                              CFSTR("WebPluginMIMETypes"));
  }
}

bool ReadPlistPluginInfo(const FilePath& filename, CFBundleRef bundle,
                         WebPluginInfo* info) {
  NSDictionary* mime_types = GetMIMETypes(bundle);
  if (!mime_types)
    return false;  // no type info here; try elsewhere

  for (NSString* mime_type in [mime_types allKeys]) {
    NSDictionary* mime_dict = [mime_types objectForKey:mime_type];
    NSString* mime_desc = [mime_dict objectForKey:@"WebPluginTypeDescription"];
    NSArray* mime_exts = [mime_dict objectForKey:@"WebPluginExtensions"];

    WebPluginMimeType mime;
    mime.mime_type = base::SysNSStringToUTF8([mime_type lowercaseString]);
    // Remove PDF from the list of types handled by QuickTime, since it provides
    // a worse experience than just downloading the PDF.
    if (mime.mime_type == "application/pdf" &&
        StartsWithASCII(filename.BaseName().value(), "QuickTime", false)) {
      continue;
    }

    if (mime_desc)
      mime.description = base::SysNSStringToUTF16(mime_desc);
    for (NSString* ext in mime_exts)
      mime.file_extensions.push_back(
          base::SysNSStringToUTF8([ext lowercaseString]));

    info->mime_types.push_back(mime);
  }

  NSString* plugin_name =
      (NSString*)CFBundleGetValueForInfoDictionaryKey(bundle,
      CFSTR("WebPluginName"));
  NSString* plugin_vers =
      (NSString*)CFBundleGetValueForInfoDictionaryKey(bundle,
      CFSTR("CFBundleShortVersionString"));
  NSString* plugin_desc =
      (NSString*)CFBundleGetValueForInfoDictionaryKey(bundle,
      CFSTR("WebPluginDescription"));

  if (plugin_name)
    info->name = base::SysNSStringToUTF16(plugin_name);
  else
    info->name = UTF8ToUTF16(filename.BaseName().value());
  info->path = filename;
  if (plugin_vers)
    info->version = base::SysNSStringToUTF16(plugin_vers);
  if (plugin_desc)
    info->desc = base::SysNSStringToUTF16(plugin_desc);
  else
    info->desc = UTF8ToUTF16(filename.BaseName().value());
  info->enabled = WebPluginInfo::USER_ENABLED;

  return true;
}

class ScopedBundleResourceFile {
 public:
  ScopedBundleResourceFile(CFBundleRef bundle) : bundle_(bundle) {
    old_ref_num_ = CurResFile();
    bundle_ref_num_ = CFBundleOpenBundleResourceMap(bundle);
    UseResFile(bundle_ref_num_);
  }
  ~ScopedBundleResourceFile() {
    UseResFile(old_ref_num_);
    CFBundleCloseBundleResourceMap(bundle_, bundle_ref_num_);
  }

 private:
  CFBundleRef bundle_;
  CFBundleRefNum bundle_ref_num_;
  ResFileRefNum old_ref_num_;
};

bool GetSTRResource(CFBundleRef bundle, short res_id,
                    std::vector<std::string>* contents) {
  Handle res_handle = Get1Resource('STR#', res_id);
  if (!res_handle || !*res_handle)
    return false;

  char* pointer = *res_handle;
  short num_strings = *(short*)pointer;
  pointer += sizeof(short);
  for (short i = 0; i < num_strings; ++i) {
    // Despite being 8-bits wide, these are legacy encoded. Make a round trip.
    ScopedCFTypeRef<CFStringRef> str(CFStringCreateWithPascalStringNoCopy(
        kCFAllocatorDefault,
        (unsigned char*)pointer,
        GetApplicationTextEncoding(),  // is this right?
        kCFAllocatorNull));            // perhaps CFStringGetSystemEncoding?
    if (!str.get())
      return false;
    contents->push_back(base::SysCFStringRefToUTF8(str.get()));
    pointer += 1+*reinterpret_cast<unsigned char*>(pointer);
  }

  return true;
}

bool ReadSTRPluginInfo(const FilePath& filename, CFBundleRef bundle,
                       WebPluginInfo* info) {
  ScopedBundleResourceFile res_file(bundle);

  std::vector<std::string> type_strings;
  if (!GetSTRResource(bundle, kSTRTypeDefinitionResourceID, &type_strings))
    return false;

  std::vector<std::string> type_descs;
  bool have_type_descs = GetSTRResource(bundle,
                                        kSTRTypeDescriptionResourceID,
                                        &type_descs);

  std::vector<std::string> plugin_descs;
  bool have_plugin_descs = GetSTRResource(bundle,
                                          kSTRPluginDescriptionResourceID,
                                          &plugin_descs);

  size_t num_types = type_strings.size()/2;

  for (size_t i = 0; i < num_types; ++i) {
    WebPluginMimeType mime;
    mime.mime_type = StringToLowerASCII(type_strings[2*i]);
    if (have_type_descs && i < type_descs.size())
      mime.description = UTF8ToUTF16(type_descs[i]);
    base::SplitString(
        StringToLowerASCII(type_strings[2*i+1]), ',', &mime.file_extensions);

    info->mime_types.push_back(mime);
  }

  NSString* plugin_vers =
      (NSString*)CFBundleGetValueForInfoDictionaryKey(bundle,
      CFSTR("CFBundleShortVersionString"));

  if (have_plugin_descs && plugin_descs.size() > 1)
    info->name = UTF8ToUTF16(plugin_descs[1]);
  else
    info->name = UTF8ToUTF16(filename.BaseName().value());
  info->path = filename;
  if (plugin_vers)
    info->version = base::SysNSStringToUTF16(plugin_vers);
  if (have_plugin_descs && !plugin_descs.empty())
    info->desc = UTF8ToUTF16(plugin_descs[0]);
  else
    info->desc = UTF8ToUTF16(filename.BaseName().value());
  info->enabled = WebPluginInfo::USER_ENABLED;

  return true;
}

}  // anonymous namespace

bool PluginLib::ReadWebPluginInfo(const FilePath &filename,
                                  WebPluginInfo* info) {
  // There are two ways to get information about plugin capabilities. One is an
  // Info.plist set of keys, documented at
  // http://developer.apple.com/documentation/InternetWeb/Conceptual/WebKit_PluginProgTopic/Concepts/AboutPlugins.html .
  // The other is a set of STR# resources, documented at
  // https://developer.mozilla.org/En/Gecko_Plugin_API_Reference/Plug-in_Development_Overview .
  //
  // Historically, the data was maintained in the STR# resources. Apple, with
  // the introduction of WebKit, noted the weaknesses of resources and moved the
  // information into the Info.plist. Mozilla had always supported a
  // NP_GetMIMEDescription() entry point for Unix plugins and also supports it
  // on the Mac to supplement the STR# format. WebKit does not support
  // NP_GetMIMEDescription() and neither do we. (That entry point is documented
  // at https://developer.mozilla.org/en/NP_GetMIMEDescription .) We prefer the
  // Info.plist format because it's more extensible and has a defined encoding,
  // but will fall back to the STR# format of the data if it is not present in
  // the Info.plist.
  //
  // The parsing of the data lives in the two functions ReadSTRPluginInfo() and
  // ReadPlistPluginInfo(), but a summary of the formats follows.
  //
  // Each data type handled by a plugin has several properties:
  // - <<type0mimetype>>
  // - <<type0fileextension0>>..<<type0fileextensionk>>
  // - <<type0description>>
  //
  // Each plugin may have any number of types defined. In addition, the plugin
  // itself has properties:
  // - <<plugindescription>>
  // - <<pluginname>>
  //
  // For the Info.plist version, the data is formatted as follows (in text plist
  // format):
  //  {
  //    ... the usual plist keys ...
  //    WebPluginDescription = <<plugindescription>>;
  //    WebPluginMIMETypes = {
  //      <<type0mimetype>> = {
  //        WebPluginExtensions = (
  //                               <<type0fileextension0>>,
  //                               ...
  //                               <<type0fileextensionk>>,
  //                               );
  //        WebPluginTypeDescription = <<type0description>>;
  //      };
  //      <<type1mimetype>> = { ... };
  //      ...
  //      <<typenmimetype>> = { ... };
  //    };
  //    WebPluginName = <<pluginname>>;
  //  }
  //
  // Alternatively (and this is undocumented), rather than a WebPluginMIMETypes
  // key, there may be a WebPluginMIMETypesFilename key. If it is present, then
  // it is the name of a file in the user's preferences folder in which to find
  // the WebPluginMIMETypes key. If the key is present but the file doesn't
  // exist, we must load the plugin and call a specific function to have the
  // plugin create the file.
  //
  // If we do not find those keys in the Info.plist, we fall back to the STR#
  // resources. In them, the data is formatted as follows:
  // STR# 128
  // (1) <<type0mimetype>>
  // (2) <<type0fileextension0>>,...,<<type0fileextensionk>>
  // (3) <<type1mimetype>>
  // (4) <<type1fileextension0>>,...,<<type1fileextensionk>>
  // (...)
  // (2n+1) <<typenmimetype>>
  // (2n+2) <<typenfileextension0>>,...,<<typenfileextensionk>>
  // STR# 127
  // (1) <<type0description>>
  // (2) <<type1description>>
  // (...)
  // (n+1) <<typendescription>>
  // STR# 126
  // (1) <<plugindescription>>
  // (2) <<pluginname>>
  //
  // Strictly speaking, only STR# 128 is required.

  ScopedCFTypeRef<CFURLRef> bundle_url(CFURLCreateFromFileSystemRepresentation(
      kCFAllocatorDefault, (const UInt8*)filename.value().c_str(),
      filename.value().length(), true));
  if (!bundle_url)
    return false;
  ScopedCFTypeRef<CFBundleRef> bundle(CFBundleCreate(kCFAllocatorDefault,
                                                     bundle_url.get()));
  if (!bundle)
    return false;

  // preflight

  OSType type = 0;
  CFBundleGetPackageInfo(bundle.get(), &type, NULL);
  if (type != FOUR_CHAR_CODE('BRPL'))
    return false;

  CFErrorRef error;
  Boolean would_load = CFBundlePreflightExecutable(bundle.get(), &error);
  if (!would_load)
    return false;

  // get the info

  if (ReadPlistPluginInfo(filename, bundle.get(), info))
    return true;

  if (ReadSTRPluginInfo(filename, bundle.get(), info))
    return true;

  // ... or not

  return false;
}

}  // namespace npapi
}  // namespace webkit
