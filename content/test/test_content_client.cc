// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/test/test_content_client.h"

#include "base/base_paths.h"
#include "base/file_path.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/string_piece.h"

TestContentClient::TestContentClient()
    : data_pack_(ui::SCALE_FACTOR_100P) {
  FilePath content_resources_pack_path;
  PathService::Get(base::DIR_MODULE, &content_resources_pack_path);
  content_resources_pack_path = content_resources_pack_path.Append(
      FILE_PATH_LITERAL("content_resources.pak"));
  data_pack_.LoadFromPath(content_resources_pack_path);
}

TestContentClient::~TestContentClient() {
}

std::string TestContentClient::GetUserAgent() const {
  return std::string("TestContentClient");
}

base::StringPiece TestContentClient::GetDataResource(
    int resource_id,
    ui::ScaleFactor scale_factor) const {
  base::StringPiece resource;
  data_pack_.GetStringPiece(resource_id, &resource);
  return resource;
}
