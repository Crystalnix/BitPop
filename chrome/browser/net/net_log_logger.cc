// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/net/net_log_logger.h"

#include <stdio.h>

#include "base/file_util.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/ui/webui/net_internals/net_internals_ui.h"

NetLogLogger::NetLogLogger(const FilePath &log_path) {
  if (!log_path.empty()) {
    base::ThreadRestrictions::ScopedAllowIO allow_io;
    file_.Set(file_util::OpenFile(log_path, "w"));

    // Write constants to the output file.  This allows loading files that have
    // different source and event types, as they may be added and removed
    // between Chrome versions.
    scoped_ptr<Value> value(NetInternalsUI::GetConstants());
    std::string json;
    base::JSONWriter::Write(value.get(), &json);
    fprintf(file_.get(), "{\"constants\": %s,\n", json.c_str());
    fprintf(file_.get(), "\"events\": [\n");
  }
}

NetLogLogger::~NetLogLogger() {
}

void NetLogLogger::StartObserving(net::NetLog* net_log) {
  net_log->AddThreadSafeObserver(this, net::NetLog::LOG_ALL_BUT_BYTES);
}

void NetLogLogger::OnAddEntry(const net::NetLog::Entry& entry) {
  scoped_ptr<Value> value(entry.ToValue());
  // Don't pretty print, so each JSON value occupies a single line, with no
  // breaks (Line breaks in any text field will be escaped).  Using strings
  // instead of integer identifiers allows logs from older versions to be
  // loaded, though a little extra parsing has to be done when loading a log.
  std::string json;
  base::JSONWriter::Write(value.get(), &json);
  if (!file_.get()) {
    VLOG(1) << json;
  } else {
    fprintf(file_.get(), "%s,\n", json.c_str());
  }
}
