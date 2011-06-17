// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_MIME_UTIL_H__
#define NET_BASE_MIME_UTIL_H__
#pragma once

#include <string>
#include <vector>

#include "base/file_path.h"

namespace net {

// Get the mime type (if any) that is associated with the given file extension.
// Returns true if a corresponding mime type exists.
bool GetMimeTypeFromExtension(const FilePath::StringType& ext,
                              std::string* mime_type);

// Get the mime type (if any) that is associated with the given file.  Returns
// true if a corresponding mime type exists.
bool GetMimeTypeFromFile(const FilePath& file_path, std::string* mime_type);

// Get the preferred extension (if any) associated with the given mime type.
// Returns true if a corresponding file extension exists.  The extension is
// returned without a prefixed dot, ex "html".
bool GetPreferredExtensionForMimeType(const std::string& mime_type,
                                      FilePath::StringType* extension);

// Check to see if a particular MIME type is in our list.
bool IsSupportedImageMimeType(const char* mime_type);
bool IsSupportedMediaMimeType(const char* mime_type);
bool IsSupportedNonImageMimeType(const char* mime_type);
bool IsSupportedJavascriptMimeType(const char* mime_type);

// Get whether this mime type should be displayed in view-source mode.
// (For example, XML.)
bool IsViewSourceMimeType(const char* mime_type);

// Convenience function.
bool IsSupportedMimeType(const std::string& mime_type);

// Returns true if this the mime_type_pattern matches a given mime-type.
// Checks for absolute matching and wildcards.  mime-types should be in
// lower case.
bool MatchesMimeType(const std::string &mime_type_pattern,
                     const std::string &mime_type);

// Returns true if and only if all codecs are supported, false otherwise.
bool AreSupportedMediaCodecs(const std::vector<std::string>& codecs);

// Parses a codec string, populating |codecs_out| with the prefix of each codec
// in the string |codecs_in|. For example, passed "aaa.b.c,dd.eee", if
// |strip| == true |codecs_out| will contain {"aaa", "dd"}, if |strip| == false
// |codecs_out| will contain {"aaa.b.c", "dd.eee"}.
// See http://www.ietf.org/rfc/rfc4281.txt.
void ParseCodecString(const std::string& codecs,
                      std::vector<std::string>* codecs_out,
                      bool strip);

// Check to see if a particular MIME type is in our list which only supports a
// certain subset of codecs.
bool IsStrictMediaMimeType(const std::string& mime_type);

// Check to see if a particular MIME type is in our list which only supports a
// certain subset of codecs. Returns true if and only if all codecs are
// supported for that specific MIME type, false otherwise. If this returns
// false you will still need to check if the media MIME tpyes and codecs are
// supported.
bool IsSupportedStrictMediaMimeType(const std::string& mime_type,
    const std::vector<std::string>& codecs);

// Get the extensions for images files.
// Note that we do not erase the existing elements in the the provided vector.
// Instead, we append the result to it.
void GetImageExtensions(std::vector<FilePath::StringType>* extensions);

// Get the extensions for audio files.
// Note that we do not erase the existing elements in the the provided vector.
// Instead, we append the result to it.
void GetAudioExtensions(std::vector<FilePath::StringType>* extensions);

// Get the extensions for video files.
// Note that we do not erase the existing elements in the the provided vector.
// Instead, we append the result to it.
void GetVideoExtensions(std::vector<FilePath::StringType>* extensions);

// Get the extensions associated with the given mime type.
// There could be multiple extensions for a given mime type, like "html,htm"
// for "text/html".
// Note that we do not erase the existing elements in the the provided vector.
// Instead, we append the result to it.
void GetExtensionsForMimeType(const std::string& mime_type,
                              std::vector<FilePath::StringType>* extensions);

}  // namespace net

#endif  // NET_BASE_MIME_UTIL_H__
