// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_JSON_HOST_CONFIG_H_
#define REMOTING_HOST_JSON_HOST_CONFIG_H_

#include <string>

#include "base/file_path.h"
#include "remoting/host/in_memory_host_config.h"

namespace base {
class MessageLoopProxy;
}  // namespace base

namespace remoting {

// JsonHostConfig implements MutableHostConfig for JSON file.
class JsonHostConfig : public InMemoryHostConfig {
 public:
  JsonHostConfig(const FilePath& filename);
  virtual ~JsonHostConfig();

  virtual bool Read();

  // MutableHostConfig interface.
  virtual bool Save() OVERRIDE;

  std::string GetSerializedData();

 private:
  FilePath filename_;

  DISALLOW_COPY_AND_ASSIGN(JsonHostConfig);
};

}  // namespace remoting

#endif  // REMOTING_HOST_JSON_HOST_CONFIG_H_
