// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_GDATA_DRIVE_TASK_EXECUTOR_H_
#define CHROME_BROWSER_CHROMEOS_GDATA_DRIVE_TASK_EXECUTOR_H_

#include "chrome/browser/chromeos/extensions/file_handler_util.h"
#include "chrome/browser/chromeos/gdata/gdata_errorcode.h"

namespace gdata {

class GDataEntryProto;

// This class implements an "executor" class that will execute tasks for
// third party Drive apps that store data in Drive itself.  To do that, it
// needs to find the file resource IDs and pass them to a server-side function
// that will authorize the app to open the given document and return a URL
// for opening the document in that app directly.
class DriveTaskExecutor : public file_handler_util::FileTaskExecutor {
 public:
  // FileTaskExecutor overrides
  virtual bool ExecuteAndNotify(
      const std::vector<GURL>& file_urls,
      const file_handler_util::FileTaskFinishedCallback& done) OVERRIDE;

 private:
  // FileTaskExecutor is the only class allowed to create one.
  friend class file_handler_util::FileTaskExecutor;

  DriveTaskExecutor(Profile* profile,
                    const std::string& app_id,
                    const std::string& action_id);
  virtual ~DriveTaskExecutor();

  void OnFileEntryFetched(GDataFileError error,
                          scoped_ptr<GDataEntryProto> entry_proto);
  void OnAppAuthorized(const std::string& resource_id,
                       GDataErrorCode error,
                       scoped_ptr<base::Value> feed_data);

  // Calls |done_| with |success| status.
  void Done(bool success);

  const GURL source_url_;
  std::string app_id_;
  const std::string action_id_;
  int current_index_;
  file_handler_util::FileTaskFinishedCallback done_;
};

}  // namespace gdata

#endif  // CHROME_BROWSER_CHROMEOS_GDATA_DRIVE_TASK_EXECUTOR_H_
