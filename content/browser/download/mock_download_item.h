// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_DOWNLOAD_MOCK_DOWNLOAD_ITEM_H_
#define CONTENT_BROWSER_DOWNLOAD_MOCK_DOWNLOAD_ITEM_H_

#include <string>
#include <vector>

#include "content/browser/download/download_persistent_store_info.h"
#include "content/browser/download/interrupt_reasons.h"
#include "content/public/browser/download_id.h"
#include "content/public/browser/download_item.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

class MockDownloadItem : public content::DownloadItem {
 public:
  MockDownloadItem();
  virtual ~MockDownloadItem();
  MOCK_METHOD1(AddObserver, void(content::DownloadItem::Observer*));
  MOCK_METHOD1(RemoveObserver, void(content::DownloadItem::Observer*));
  MOCK_METHOD0(UpdateObservers, void());
  MOCK_METHOD0(CanShowInFolder, bool());
  MOCK_METHOD0(CanOpenDownload, bool());
  MOCK_METHOD0(ShouldOpenFileBasedOnExtension, bool());
  MOCK_METHOD0(OpenDownload, void());
  MOCK_METHOD0(ShowDownloadInShell, void());
  MOCK_METHOD0(DangerousDownloadValidated, void());
  MOCK_METHOD3(UpdateProgress, void(int64, int64, const std::string&));
  MOCK_METHOD1(Cancel, void(bool));
  MOCK_METHOD0(MarkAsComplete, void());
  MOCK_METHOD0(DelayedDownloadOpened, void());
  MOCK_METHOD2(OnAllDataSaved, void(int64, const std::string&));
  MOCK_METHOD0(OnDownloadedFileRemoved, void());
  MOCK_METHOD0(MaybeCompleteDownload, void());
  MOCK_METHOD3(Interrupted, void(int64, const std::string&, InterruptReason));
  MOCK_METHOD1(Delete, void(DeleteReason));
  MOCK_METHOD0(Remove, void());
  MOCK_CONST_METHOD1(TimeRemaining, bool(base::TimeDelta*));
  MOCK_CONST_METHOD0(CurrentSpeed, int64());
  MOCK_CONST_METHOD0(PercentComplete, int());
  MOCK_METHOD1(OnPathDetermined, void(const FilePath&));
  MOCK_CONST_METHOD0(AllDataSaved, bool());
  MOCK_METHOD1(SetFileCheckResults, void(const DownloadStateInfo&));
  MOCK_METHOD1(Rename, void(const FilePath&));
  MOCK_METHOD0(TogglePause, void());
  MOCK_METHOD1(OnDownloadCompleting, void(DownloadFileManager*));
  MOCK_METHOD1(OnDownloadRenamedToFinalName, void(const FilePath&));
  MOCK_CONST_METHOD1(MatchesQuery, bool(const string16& query));
  MOCK_CONST_METHOD0(IsPartialDownload, bool());
  MOCK_CONST_METHOD0(IsInProgress, bool());
  MOCK_CONST_METHOD0(IsCancelled, bool());
  MOCK_CONST_METHOD0(IsInterrupted, bool());
  MOCK_CONST_METHOD0(IsComplete, bool());
  MOCK_CONST_METHOD0(GetState, DownloadState());
  MOCK_CONST_METHOD0(GetFullPath, const FilePath&());
  MOCK_METHOD1(SetPathUniquifier, void(int));
  MOCK_CONST_METHOD0(GetUrlChain, const std::vector<GURL>&());
  MOCK_METHOD1(SetTotalBytes, void(int64));
  MOCK_CONST_METHOD0(GetURL, const GURL&());
  MOCK_CONST_METHOD0(GetOriginalUrl, const GURL&());
  MOCK_CONST_METHOD0(GetReferrerUrl, const GURL&());
  MOCK_CONST_METHOD0(GetSuggestedFilename, std::string());
  MOCK_CONST_METHOD0(GetContentDisposition, std::string());
  MOCK_CONST_METHOD0(GetMimeType, std::string());
  MOCK_CONST_METHOD0(GetOriginalMimeType, std::string());
  MOCK_CONST_METHOD0(GetReferrerCharset, std::string());
  MOCK_CONST_METHOD0(GetRemoteAddress, std::string());
  MOCK_CONST_METHOD0(GetTotalBytes, int64());
  MOCK_CONST_METHOD0(GetReceivedBytes, int64());
  MOCK_CONST_METHOD0(GetHashState, const std::string&());
  MOCK_CONST_METHOD0(GetHash, const std::string&());
  MOCK_CONST_METHOD0(GetId, int32());
  MOCK_CONST_METHOD0(GetGlobalId, content::DownloadId());
  MOCK_CONST_METHOD0(GetStartTime, base::Time());
  MOCK_CONST_METHOD0(GetEndTime, base::Time());
  MOCK_METHOD1(SetDbHandle, void(int64));
  MOCK_CONST_METHOD0(GetDbHandle, int64());
  MOCK_METHOD0(GetDownloadManager, content::DownloadManager*());
  MOCK_CONST_METHOD0(IsPaused, bool());
  MOCK_CONST_METHOD0(GetOpenWhenComplete, bool());
  MOCK_METHOD1(SetOpenWhenComplete, void(bool));
  MOCK_CONST_METHOD0(GetFileExternallyRemoved, bool());
  MOCK_CONST_METHOD0(GetSafetyState, SafetyState());
  MOCK_CONST_METHOD0(GetDangerType, content::DownloadDangerType());
  MOCK_CONST_METHOD0(IsDangerous, bool());
  MOCK_METHOD0(MarkFileDangerous, void());
  MOCK_METHOD0(MarkUrlDangerous, void());
  MOCK_METHOD0(MarkContentDangerous, void());
  MOCK_METHOD0(GetAutoOpened, bool());
  MOCK_CONST_METHOD0(GetTargetName, const FilePath&());
  MOCK_CONST_METHOD0(PromptUserForSaveLocation, bool());
  MOCK_CONST_METHOD0(IsOtr, bool());
  MOCK_CONST_METHOD0(GetSuggestedPath, const FilePath&());
  MOCK_CONST_METHOD0(IsTemporary, bool());
  MOCK_METHOD1(SetOpened, void(bool));
  MOCK_CONST_METHOD0(GetOpened, bool());
  MOCK_CONST_METHOD0(GetLastModifiedTime, const std::string&());
  MOCK_CONST_METHOD0(GetETag, const std::string&());
  MOCK_CONST_METHOD0(GetLastReason, InterruptReason());
  MOCK_CONST_METHOD0(GetPersistentStoreInfo, DownloadPersistentStoreInfo());
  MOCK_CONST_METHOD0(GetStateInfo, DownloadStateInfo());
  MOCK_CONST_METHOD0(GetBrowserContext, content::BrowserContext*());
  MOCK_CONST_METHOD0(GetWebContents, content::WebContents*());
  MOCK_CONST_METHOD0(GetTargetFilePath, FilePath());
  MOCK_CONST_METHOD0(GetFileNameToReportUser, FilePath());
  MOCK_CONST_METHOD0(GetUserVerifiedFilePath, FilePath());
  MOCK_CONST_METHOD0(NeedsRename, bool());
  MOCK_METHOD1(OffThreadCancel, void(DownloadFileManager* file_manager));
  MOCK_CONST_METHOD1(DebugString, std::string(bool));
  MOCK_METHOD0(MockDownloadOpenForTesting, void());
  MOCK_METHOD1(GetExternalData, ExternalData*(const void*));
  MOCK_METHOD2(SetExternalData, void(const void*, ExternalData*));
};
#endif  // CONTENT_BROWSER_DOWNLOAD_MOCK_DOWNLOAD_ITEM_H_
