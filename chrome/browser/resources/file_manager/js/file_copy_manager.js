// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @constructor
 * @param {DirectoryEntry} root Root directory entry.
 */
function FileCopyManager(root) {
  this.copyTasks_ = [];
  this.cancelObservers_ = [];
  this.cancelRequested_ = false;
  this.root_ = root;
  this.unloadTimeout_ = null;
}

var fileCopyManagerInstance = null;

/**
 * Get FileCopyManager instance. In case is hasn't been initialized, a new
 * instance is created.
 * @param {DirectoryEntry} root Root entry.
 * @return {FileCopyManager} A FileCopyManager instance.
 */
FileCopyManager.getInstance = function(root) {
  if (fileCopyManagerInstance === null) {
    fileCopyManagerInstance = new FileCopyManager(root);
  }
  return fileCopyManagerInstance;
};

/**
 * A record of a queued copy operation.
 *
 * Multiple copy operations may be queued at any given time.  Additional
 * Tasks may be added while the queue is being serviced.  Though a
 * cancel operation cancels everything in the queue.
 *
 * @param {DirectoryEntry} sourceDirEntry Source directory.
 * @param {DirectoryEntry} targetDirEntry Target directory.
 */
FileCopyManager.Task = function(sourceDirEntry, targetDirEntry) {
  this.sourceDirEntry = sourceDirEntry;
  this.targetDirEntry = targetDirEntry;
  this.originalEntries = null;

  this.pendingDirectories = [];
  this.pendingFiles = [];
  this.pendingBytes = 0;

  this.completedDirectories = [];
  this.completedFiles = [];
  this.completedBytes = 0;

  this.deleteAfterCopy = false;
  this.move = false;
  this.sourceOnGData = false;
  this.targetOnGData = false;

  // If directory already exists, we try to make a copy named 'dir (X)',
  // where X is a number. When we do this, all subsequent copies from
  // inside the subtree should be mapped to the new directory name.
  // For example, if 'dir' was copied as 'dir (1)', then 'dir\file.txt' should
  // become 'dir (1)\file.txt'.
  this.renamedDirectories_ = [];
};

/**
 * @param {Array.<Entry>} entries Entries.
 * @param {Function} callback When entries resolved.
 */
FileCopyManager.Task.prototype.setEntries = function(entries, callback) {
  var self = this;

  function onEntriesRecursed(result) {
    self.pendingDirectories = result.dirEntries;
    self.pendingFiles = result.fileEntries;
    self.pendingBytes = result.fileBytes;
    callback();
  }

  this.originalEntries = entries;
  // When moving directories, FileEntry.moveTo() is used if both source
  // and target are on GData. There is no need to recurse into directories.
  var recurse = !this.move;
  util.recurseAndResolveEntries(entries, recurse, onEntriesRecursed);
};

/**
 * @return {Entry} Next entry.
 */
FileCopyManager.Task.prototype.getNextEntry = function() {
  // We should keep the file in pending list and remove it after complete.
  // Otherwise, if we try to get status in the middle of copying. The returned
  // status is wrong (miss count the pasting item in totalItems).
  if (this.pendingDirectories.length) {
    this.pendingDirectories[0].inProgress = true;
    return this.pendingDirectories[0];
  }

  if (this.pendingFiles.length) {
    this.pendingFiles[0].inProgress = true;
    return this.pendingFiles[0];
  }

  return null;
};

/**
 * @param {Entry} entry Entry.
 * @param {number} size Bytes completed.
 */
FileCopyManager.Task.prototype.markEntryComplete = function(entry, size) {
  // It is probably not safe to directly remove the first entry in pending list.
  // We need to check if the removed entry (srcEntry) corresponding to the added
  // entry (target entry).
  if (entry.isDirectory && this.pendingDirectories &&
      this.pendingDirectories[0].inProgress) {
    this.completedDirectories.push(entry);
    this.pendingDirectories.shift();
  } else if (this.pendingFiles && this.pendingFiles[0].inProgress) {
    this.completedFiles.push(entry);
    this.completedBytes += size;
    this.pendingBytes -= size;
    this.pendingFiles.shift();
  } else {
    throw new Error('Try to remove a source entry which is not correspond to' +
                    ' the finished target entry');
  }
};

/**
 * Updates copy progress status for the entry.
 *
 * @param {Entry} entry Entry which is being coppied.
 * @param {number} size Number of bytes that has been copied since last update.
 */
FileCopyManager.Task.prototype.updateFileCopyProgress = function(entry, size) {
  if (entry.isFile && this.pendingFiles && this.pendingFiles[0].inProgress) {
    this.completedBytes += size;
    this.pendingBytes -= size;
  }
};

/**
 * @param {string} fromName Old name.
 * @param {string} toName New name.
 */
FileCopyManager.Task.prototype.registerRename = function(fromName, toName) {
  this.renamedDirectories_.push({from: fromName + '/', to: toName + '/'});
};

/**
 * @param {string} path A path.
 * @return {string} Path after renames.
 */
FileCopyManager.Task.prototype.applyRenames = function(path) {
  // Directories are processed in pre-order, so we will store only the first
  // renaming point:
  // x   -> x (1)    -- new directory created.
  // x\y -> x (1)\y  -- no more renames inside the new directory, so
  //                    this one will not be stored.
  // x\y\a.txt       -- only one rename will be applied.
  for (var index = 0; index < this.renamedDirectories_.length; ++index) {
    var rename = this.renamedDirectories_[index];
    if (path.indexOf(rename.from) == 0) {
      path = rename.to + path.substr(rename.from.length);
    }
  }
  return path;
};

/**
 * Error class used to report problems with a copy operation.
 * @constructor
 * @param {string} reason Error type.
 * @param {Object} data Additional data.
 */
FileCopyManager.Error = function(reason, data) {
  this.reason = reason;
  this.code = FileCopyManager.Error[reason];
  this.data = data;
};

/** @const */
FileCopyManager.Error.CANCELLED = 0;
/** @const */
FileCopyManager.Error.UNEXPECTED_SOURCE_FILE = 1;
/** @const */
FileCopyManager.Error.TARGET_EXISTS = 2;
/** @const */
FileCopyManager.Error.FILESYSTEM_ERROR = 3;


// FileCopyManager methods.

/**
 * @return {Object} Status object.
 */
FileCopyManager.prototype.getStatus = function() {
  var rv = {
    pendingItems: 0,  // Files + Directories
    pendingFiles: 0,
    pendingDirectories: 0,
    pendingBytes: 0,

    completedItems: 0,  // Files + Directories
    completedFiles: 0,
    completedDirectories: 0,
    completedBytes: 0,

    percentage: NaN,
    pendingCopies: 0,
    pendingMoves: 0,
    filename: ''  // In case pendingItems == 1
  };

  var pendingFile = null;

  for (var i = 0; i < this.copyTasks_.length; i++) {
    var task = this.copyTasks_[i];
    var pendingFiles = task.pendingFiles.length;
    var pendingDirectories = task.pendingDirectories.length;
    rv.pendingFiles += pendingFiles;
    rv.pendingDirectories += pendingDirectories;
    rv.pendingBytes += task.pendingBytes;

    rv.completedFiles += task.completedFiles.length;
    rv.completedDirectories += task.completedDirectories.length;
    rv.completedBytes += task.completedBytes;

    if (task.move || task.deleteAfterCopy) {
      rv.pendingMoves += pendingFiles + pendingDirectories;
    } else {
      rv.pendingCopies += pendingFiles + pendingDirectories;
    }

    if (task.pendingFiles.length === 1)
      pendingFile = task.pendingFiles[0];

    if (task.pendingDirectories.length === 1)
      pendingFile = task.pendingDirectories[0];

  }
  rv.pendingItems = rv.pendingFiles + rv.pendingDirectories;
  rv.completedItems = rv.completedFiles + rv.completedDirectories;

  rv.totalFiles = rv.pendingFiles + rv.completedFiles;
  rv.totalDirectories = rv.pendingDirectories + rv.completedDirectories;
  rv.totalItems = rv.pendingItems + rv.completedItems;
  rv.totalBytes = rv.pendingBytes + rv.completedBytes;

  rv.percentage = rv.completedBytes / rv.totalBytes;
  if (rv.pendingItems === 1)
    rv.filename = pendingFile.name;

  return rv;
};

/**
 * Send an event to all the FileManager windows.
 * @private
 * @param {string} eventName Event name.
 * @param {Object} eventArgs An object with arbitrary event parameters.
 */
FileCopyManager.prototype.sendEvent_ = function(eventName, eventArgs) {
  var windows = chrome.extension.getViews();
  for (var i = 0; i < windows.length; i++) {
    var w = windows[i];
    if (w.fileCopyManagerWrapper)
      w.fileCopyManagerWrapper.onEvent(eventName, eventArgs);
  }

  if (this.copyTasks_.length === 0) {
    if (this.unloadTimeout_ === null)
      this.unloadTimeout_ = setTimeout(close, 5000);
  } else {
    this.unloadTimeout_ = null;
  }
};

/**
 * Write to console.log on all the active FileManager windows.
 * @private
 */
FileCopyManager.prototype.log_ = function() {
  var windows = chrome.extension.getViews();
  for (var i = 0; i < windows.length; i++) {
    windows[i].console.log.apply(windows[i].console, arguments);
  }
};

/**
 * Dispatch a simple copy-progress event with reason and optional err data.
 * @private
 * @param {string} reason Event type.
 * @param {FileCopyManager.Error} opt_err Error.
 */
FileCopyManager.prototype.sendProgressEvent_ = function(reason, opt_err) {
  var event = {};
  event.reason = reason;
  if (opt_err)
    event.error = opt_err;
  this.sendEvent_('copy-progress', event);
};

/**
 * Dispatch an event of file operation completion (allows to update the UI).
 * @private
 * @param {string} reason Completed file operation: 'movied|copied|deleted'.
 * @param {Array.<Entry>} affectedEntries deleted ot created entries.
 */
FileCopyManager.prototype.sendOperationEvent_ = function(reason,
                                                         affectedEntries) {
  var event = {};
  event.reason = reason;
  event.affectedEntries = affectedEntries;
  this.sendEvent_('copy-operation-complete', event);
};

/**
 * Completely clear out the copy queue, either because we encountered an error
 * or completed successfully.
 * @private
 */
FileCopyManager.prototype.resetQueue_ = function() {
  for (var i = 0; i < this.cancelObservers_.length; i++)
    this.cancelObservers_[i]();

  this.copyTasks_ = [];
  this.cancelObservers_ = [];
  this.cancelRequested_ = false;
};

/**
 * Request that the current copy queue be abandoned.
 * @param {Function} opt_callback On cancel.
 */
FileCopyManager.prototype.requestCancel = function(opt_callback) {
  this.cancelRequested_ = true;
  if (opt_callback)
    this.cancelObservers_.push(opt_callback);
};

/**
 * Perform the bookeeping required to cancel.
 * @private
 */
FileCopyManager.prototype.doCancel_ = function() {
  this.sendProgressEvent_('CANCELLED');
  this.resetQueue_();
};

/**
 * Used internally to check if a cancel has been requested, and handle
 * it if so.
 * @private
 * @return {boolean} If canceled.
 */
FileCopyManager.prototype.maybeCancel_ = function() {
  if (!this.cancelRequested_)
    return false;

  this.doCancel_();
  return true;
};

/**
 * Convert string in clipboard to entries and kick off pasting.
 * @param {Object} clipboard Clipboard contents.
 * @param {string} targetPath Target path.
 * @param {boolean} targetOnGData If target is on GDrive.
 */
FileCopyManager.prototype.paste = function(clipboard, targetPath,
                                           targetOnGData) {
  var self = this;
  var results = {
    sourceDirEntry: null,
    entries: [],
    isCut: false,
    isOnGData: false
  };

  function onPathError(err) {
    self.sendProgressEvent_('ERROR',
                            new FileCopyManager.Error('FILESYSTEM_ERROR', err));
  }

  function onSourceEntryFound(dirEntry) {
    function onTargetEntryFound(targetEntry) {
      self.queueCopy(results.sourceDirEntry,
            targetEntry,
            results.entries,
            results.isCut,
            results.isOnGData,
            targetOnGData);
    }

    function onComplete() {
      self.root_.getDirectory(targetPath, {},
                              onTargetEntryFound, onPathError);
    }

    function onEntryFound(entry) {
      // When getDirectories/getFiles finish, they call addEntry with null.
      // We dont want to add null to our entries.
      if (entry != null) {
        results.entries.push(entry);
        added++;
        if (added == total)
          onComplete();
      }
    }

    results.sourceDirEntry = dirEntry;
    var directories = [];
    var files = [];

    if (clipboard.directories) {
      directories = clipboard.directories.split('\n');
      directories = directories.filter(function(d) { return d != '' });
    }
    if (clipboard.files) {
      files = clipboard.files.split('\n');
      files = files.filter(function(f) { return f != '' });
    }

    var total = directories.length + files.length;
    var added = 0;

    results.isCut = (clipboard.isCut == 'true');
    results.isOnGData = (clipboard.isOnGData == 'true');

    util.getDirectories(self.root_, {create: false}, directories, onEntryFound,
                        onPathError);
    util.getFiles(self.root_, {create: false}, files, onEntryFound,
                  onPathError);
  }

  if (clipboard.sourceDir) {
    this.root_.getDirectory(clipboard.sourceDir,
                            {create: false},
                            onSourceEntryFound,
                            onPathError);
  } else {
    onSourceEntryFound(null);
  }
};

/**
 * Checks if source and target are on the same root.
 *
 * @param {DirectoryEntry} sourceEntry An entry from the source.
 * @param {DirectoryEntry} targetDirEntry Directory entry for the target.
 * @param {boolean} targetOnGData If target is on GDrive.
 * @return {boolean} Whether source and target dir are on the same root.
 */
FileCopyManager.prototype.isOnSameRoot = function(sourceEntry,
                                                  targetDirEntry,
                                                  targetOnGData) {
  return PathUtil.getRootPath(sourceEntry.fullPath) ==
         PathUtil.getRootPath(targetDirEntry.fullPath);
};

/**
 * Initiate a file copy.
 * @param {DirectoryEntry} sourceDirEntry Source directory.
 * @param {DirectoryEntry} targetDirEntry Target directory.
 * @param {Array.<Entry>} entries Entries to copy.
 * @param {boolean} deleteAfterCopy In case of move.
 * @param {boolean} sourceOnGData Source directory on GDrive.
 * @param {boolean} targetOnGData Target directory on GDrive.
 * @return {FileCopyManager.Task} Copy task.
 */
FileCopyManager.prototype.queueCopy = function(sourceDirEntry,
                                               targetDirEntry,
                                               entries,
                                               deleteAfterCopy,
                                               sourceOnGData,
                                               targetOnGData) {
  var self = this;
  var copyTask = new FileCopyManager.Task(sourceDirEntry, targetDirEntry);
  if (deleteAfterCopy) {
    // |sourecDirEntry| may be null, so let's check the root for the first of
    // the entries scheduled to be copied.
    if (this.isOnSameRoot(entries[0], targetDirEntry)) {
      copyTask.move = true;
    } else {
      copyTask.deleteAfterCopy = true;
    }
  }
  copyTask.sourceOnGData = sourceOnGData;
  copyTask.targetOnGData = targetOnGData;
  copyTask.setEntries(entries, function() {
    self.copyTasks_.push(copyTask);
    if (self.copyTasks_.length == 1) {
      // This moved us from 0 to 1 active tasks, let the servicing begin!
      self.serviceAllTasks_();
    } else {
      // Force to update the progress of butter bar when there are new tasks
      // coming while servicing current task.
      self.sendProgressEvent_('PROGRESS');
    }
  });

  return copyTask;
};

/**
 * Service all pending tasks, as well as any that might appear during the
 * copy.
 * @private
 */
FileCopyManager.prototype.serviceAllTasks_ = function() {
  var self = this;

  function onTaskError(err) {
    self.sendProgressEvent_('ERROR', err);
    self.resetQueue_();
  }

  function onTaskSuccess(task) {
    if (!self.copyTasks_.length) {
      // All tasks have been serviced, clean up and exit.
      self.sendProgressEvent_('SUCCESS');
      self.resetQueue_();
      return;
    }

    // We want to dispatch a PROGRESS event when there are more tasks to serve
    // right after one task finished in the queue. We treat all tasks as one
    // big task logically, so there is only one BEGIN/SUCCESS event pair for
    // these continuous tasks.
    self.sendProgressEvent_('PROGRESS');

    self.serviceNextTask_(onTaskSuccess, onTaskError);
  }

  // If the queue size is 1 after pushing our task, it was empty before,
  // so we need to kick off queue processing and dispatch BEGIN event.

  this.sendProgressEvent_('BEGIN');
  this.serviceNextTask_(onTaskSuccess, onTaskError);
};

/**
 * Service all entries in the next copy task.
 * @private
 * @param {Function} successCallback On success.
 * @param {Function} errorCallback On error.
 */
FileCopyManager.prototype.serviceNextTask_ = function(
    successCallback, errorCallback) {
  if (this.maybeCancel_())
    return;

  var self = this;
  var task = this.copyTasks_[0];

  function onFilesystemError(err) {
    errorCallback(new FileCopyManager.Error('FILESYSTEM_ERROR', err));
  }

  function onTaskComplete() {
    self.copyTasks_.shift();
    successCallback(task);
  }

  function deleteOriginals() {
    var count = task.originalEntries.length;

    function onEntryDeleted(entry) {
      self.sendOperationEvent_('deleted', [entry]);
      count--;
      if (!count)
        onTaskComplete();
    }

    for (var i = 0; i < task.originalEntries.length; i++) {
      var entry = task.originalEntries[i];
      util.removeFileOrDirectory(
          entry, onEntryDeleted.bind(self, entry), onFilesystemError);
    }
  }

  function onEntryServiced(targetEntry, size) {
    // We should not dispatch a PROGRESS event when there is no pending items
    // in the task.
    if (task.pendingDirectories.length + task.pendingFiles.length == 0) {
      if (task.deleteAfterCopy) {
        deleteOriginals();
      } else {
        onTaskComplete();
      }
      return;
    }

    self.sendProgressEvent_('PROGRESS');

    // We yield a few ms between copies to give the browser a chance to service
    // events (like perhaps the user clicking to cancel the copy, for example).
    setTimeout(function() {
      self.serviceNextTaskEntry_(task, onEntryServiced, errorCallback);
    }, 10);
  }

  this.serviceNextTaskEntry_(task, onEntryServiced, errorCallback);
};

/**
 * Service the next entry in a given task.
 * @private
 * @param {FileManager.Task} task A task.
 * @param {Function} successCallback On success.
 * @param {Function} errorCallback On error.
 */
FileCopyManager.prototype.serviceNextTaskEntry_ = function(
    task, successCallback, errorCallback) {
  if (this.maybeCancel_())
    return;

  var self = this;
  var sourceEntry = task.getNextEntry();

  if (!sourceEntry) {
    // All entries in this task have been copied.
    successCallback(null);
    return;
  }

  // |sourceEntry.originalSourcePath| is set in util.recurseAndResolveEntries.
  var sourcePath = sourceEntry.originalSourcePath;
  if (sourceEntry.fullPath.substr(0, sourcePath.length) != sourcePath) {
    // We found an entry in the list that is not relative to the base source
    // path, something is wrong.
    onError('UNEXPECTED_SOURCE_FILE', sourceEntry.fullPath);
    return;
  }

  var targetDirEntry = task.targetDirEntry;
  var originalPath = sourceEntry.fullPath.substr(sourcePath.length + 1);

  originalPath = task.applyRenames(originalPath);

  var targetRelativePrefix = originalPath;
  var targetExt = '';
  var index = targetRelativePrefix.lastIndexOf('.');
  if (index != -1) {
    targetExt = targetRelativePrefix.substr(index);
    targetRelativePrefix = targetRelativePrefix.substr(0, index);
  }

  // If file already exists, we try to make a copy named 'file (1).ext'.
  // If file is already named 'file (X).ext', we go with 'file (X+1).ext'.
  // If new name is still occupied, we increase the number up to 10 times.
  var copyNumber = 0;
  var match = /^(.*?)(?: \((\d+)\))?$/.exec(targetRelativePrefix);
  if (match && match[2]) {
    copyNumber = parseInt(match[2], 10);
    targetRelativePrefix = match[1];
  }

  var targetRelativePath = '';
  var renameTries = 0;
  var firstExistingEntry = null;

  function onCopyCompleteBase(entry, size) {
    task.markEntryComplete(entry, size);
    successCallback(entry, size);
  }

  function onCopyComplete(entry, size) {
    self.sendOperationEvent_('copied', [entry]);
    onCopyCompleteBase(entry, size);
  }

  function onCopyProgress(entry, size) {
    task.updateFileCopyProgress(entry, size);
    self.sendProgressEvent_('PROGRESS');
  }

  function onError(reason, data) {
    this.log_('serviceNextTaskEntry error: ' + reason + ':', data);
    errorCallback(new FileCopyManager.Error(reason, data));
  }

  function onFilesystemCopyComplete(sourceEntry, targetEntry) {
    // TODO(benchan): We currently do not know the size of data being
    // copied by FileEntry.copyTo(), so task.completedBytes will not be
    // increased. We will address this issue once we need to use
    // task.completedBytes to track the progress.
    self.sendOperationEvent_('copied', [sourceEntry, targetEntry]);
    onCopyCompleteBase(targetEntry, 0);
  }

  function onFilesystemMoveComplete(sourceEntry, targetEntry) {
    self.sendOperationEvent_('moved', [sourceEntry, targetEntry]);
    onCopyCompleteBase(targetEntry, 0);
  }

  function onFilesystemError(err) {
    onError('FILESYSTEM_ERROR', err);
  }

  function onTargetExists(existingEntry) {
    if (!firstExistingEntry)
      firstExistingEntry = existingEntry;
    renameTries++;
    if (renameTries < 10) {
      copyNumber++;
      tryNextCopy();
    } else {
      onError('TARGET_EXISTS', firstExistingEntry);
    }
  }

  /**
   * Resolves the immediate parent directory entry and the file name of a
   * given path, where the path is specified by a directory (not necessarily
   * the immediate parent) and a path (not necessarily the file name) related
   * to that directory. For instance,
   *   Given:
   *     |dirEntry| = DirectoryEntry('/root/dir1')
   *     |relativePath| = 'dir2/file'
   *
   *   Return:
   *     |parentDirEntry| = DirectoryEntry('/root/dir1/dir2')
   *     |fileName| = 'file'
   *
   * @param {DirectoryEntry} dirEntry A directory entry.
   * @param {string} relativePath A path relative to |dirEntry|.
   * @param {function(Entry,string)} successCallback A callback for returning
   *     the |parentDirEntry| and |fileName| upon success.
   * @param {function(FileError)} errorCallback An error callback when there is
   *     an error getting |parentDirEntry|.
   */
  function resolveDirAndBaseName(dirEntry, relativePath,
                                 successCallback, errorCallback) {
    // |intermediatePath| contains the intermediate path components
    // that are appended to |dirEntry| to form |parentDirEntry|.
    var intermediatePath = '';
    var fileName = relativePath;

    // Extract the file name component from |relativePath|.
    var index = relativePath.lastIndexOf('/');
    if (index != -1) {
      intermediatePath = relativePath.substr(0, index);
      fileName = relativePath.substr(index + 1);
    }

    if (intermediatePath == '') {
      successCallback(dirEntry, fileName);
    } else {
      dirEntry.getDirectory(intermediatePath,
                            {create: false},
                            function(entry) {
                              successCallback(entry, fileName);
                            },
                            errorCallback);
    }
  }

  function onTargetNotResolved(err) {
    // We expect to be unable to resolve the target file, since we're going
    // to create it during the copy.  However, if the resolve fails with
    // anything other than NOT_FOUND, that's trouble.
    if (err.code != FileError.NOT_FOUND_ERR)
      return onError('FILESYSTEM_ERROR', err);

    if (task.move) {
      resolveDirAndBaseName(
          targetDirEntry, targetRelativePath,
          function(dirEntry, fileName) {
            sourceEntry.moveTo(dirEntry, fileName,
                               onFilesystemMoveComplete.bind(self, sourceEntry),
                               onFilesystemError);
          },
          onFilesystemError);
      return;
    }

    if (task.sourceOnGData && task.targetOnGData) {
      // TODO(benchan): GDataFileSystem has not implemented directory copy,
      // and thus we only call FileEntry.copyTo() for files. Revisit this
      // code when GDataFileSystem supports directory copy.
      if (!sourceEntry.isDirectory) {
        resolveDirAndBaseName(
            targetDirEntry, targetRelativePath,
            function(dirEntry, fileName) {
              sourceEntry.copyTo(dirEntry, fileName,
                  onFilesystemCopyComplete.bind(self, sourceEntry),
                  onFilesystemError);
            },
            onFilesystemError);
        return;
      }
    }

    // TODO(benchan): Until GDataFileSystem supports FileWriter, we use the
    // transferFile API to copy files into or out from a gdata file system.
    if (sourceEntry.isFile && (task.sourceOnGData || task.targetOnGData)) {
      var sourceFileUrl = sourceEntry.toURL();
      var targetFileUrl = targetDirEntry.toURL() + '/' +
                          encodeURIComponent(targetRelativePath);
      var transferedBytes = 0;
      function onFileTransfersUpdated(statusList) {
        for (var i = 0; i < statusList.length; i++) {
          var s = statusList[i];
          if ((s.fileUrl == sourceFileUrl || s.fileUrl == targetFileUrl) &&
              s.processed > transferedBytes) {
            onCopyProgress(sourceEntry, s.processed - transferedBytes);
            transferedBytes = s.processed;
          }
        }
      }
      chrome.fileBrowserPrivate.onFileTransfersUpdated.addListener(
          onFileTransfersUpdated);
      chrome.fileBrowserPrivate.transferFile(
        sourceFileUrl, targetFileUrl,
        function() {
          chrome.fileBrowserPrivate.onFileTransfersUpdated.removeListener(
              onFileTransfersUpdated);
          if (chrome.extension.lastError) {
            this.log_(
                'Error copying ' + sourceFileUrl + ' to ' + targetFileUrl);
            onFilesystemError({
              code: chrome.extension.lastError.message,
              toGDrive: task.targetOnGData,
              sourceFileUrl: sourceFileUrl
            });
          } else {
            targetDirEntry.getFile(targetRelativePath, {},
                function(targetEntry) {
                  targetEntry.getMetadata(function(metadata) {
                    if (metadata.size > transferedBytes)
                      onCopyProgress(sourceEntry,
                                     metadata.size - transferedBytes);
                    onFilesystemCopyComplete(sourceEntry, targetEntry);
                  });
                },
                onFilesystemError);
          }
        });
      return;
    }

    if (sourceEntry.isDirectory) {
      targetDirEntry.getDirectory(
          targetRelativePath,
          {create: true, exclusive: true},
          function(targetEntry) {
            if (targetRelativePath != originalPath) {
              task.registerRename(originalPath, targetRelativePath);
            }
            onCopyComplete(targetEntry);
          },
          util.flog('Error getting dir: ' + targetRelativePath,
                    onFilesystemError));
    } else {
      targetDirEntry.getFile(
          targetRelativePath,
          {create: true, exclusive: true},
          function(targetEntry) {
            self.copyEntry_(sourceEntry, targetEntry,
                            onCopyProgress, onCopyComplete, onError);
          },
          util.flog('Error getting file: ' + targetRelativePath,
                    onFilesystemError));
    }
  }

  function tryNextCopy() {
    targetRelativePath = targetRelativePrefix;
    if (copyNumber > 0) {
      targetRelativePath += ' (' + copyNumber + ')';
    }
    targetRelativePath += targetExt;

    // Check to see if the target exists.  This kicks off the rest of the copy
    // if the target is not found, or raises an error if it does.
    util.resolvePath(targetDirEntry, targetRelativePath, onTargetExists,
                     onTargetNotResolved);
  }

  tryNextCopy();
};

/**
 * Copy the contents of sourceEntry into targetEntry.
 *
 * @private
 * @param {Entry} sourceEntry entry that will be copied.
 * @param {Entry} targetEntry entry to which sourceEntry will be copied.
 * @param {function(Entry, number)} progressCallback function that will be
 *     called when a part of the source entry is copied. It takes |targetEntry|
 *     and size of the last copied chunk as parameters.
 * @param {function(Entry, number)} successCallback function that will be called
 *     the copy operation finishes. It takes |targetEntry| and size of the last
 *     (not previously reported) copied chunk as parameters.
 * @param {function{string, object}} errorCallback function that will be called
 *     if an error is encountered. Takes error type and additional error data
 *     as parameters.
 */
FileCopyManager.prototype.copyEntry_ = function(sourceEntry,
                                                targetEntry,
                                                progressCallback,
                                                successCallback,
                                                errorCallback) {
  if (this.maybeCancel_())
    return;

  self = this;
  function onSourceFileFound(file) {
    function onWriterCreated(writer) {
      var reportedProgress = 0;
      writer.onerror = function(progress) {
        errorCallback('FILESYSTEM_ERROR', writer.error);
      };

      writer.onprogress = function(progress) {
        if (self.maybeCancel_()) {
          // If the copy was canelled, we should abort the operation.
          writer.abort();
          return;
        }
        // |progress.loaded| will contain total amount of data copied by now.
        // |progressCallback| expects data amount delta from the last progress
        // update.
        progressCallback(targetEntry, progress.loaded - reportedProgress);
        reportedProgress = progress.loaded;
      };

      writer.onwriteend = function() {
        successCallback(targetEntry, file.size - reportedProgress);
      };

      writer.write(file);
    }

    targetEntry.createWriter(onWriterCreated, errorCallback);
  }

  sourceEntry.file(onSourceFileFound, errorCallback);
};

