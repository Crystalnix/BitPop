// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @constructor
 * @param {MetadataCache} metadataCache Metadata cache service.
 * @param {cr.ui.ArrayDataModel} fileList The file list.
 * @param {boolean} showHidden If files starting with '.' are shown.
 */
function FileListContext(metadataCache, fileList, showHidden) {
  /**
   * @type {MetadataCache}
   */
  this.metadataCache = metadataCache;
  /**
   * @type {cr.ui.ArrayDataModel}
   */
  this.fileList = fileList;
  /**
   * @type Object.<string, Function>
   * @private
   */
  this.filters_ = {};
  this.setFilterHidden(!showHidden);
}

/**
 * @param {string} name Filter identifier.
 * @param {Function(Entry)} callback A filter — a function receiving an Entry,
 *     and returning bool.
 */
FileListContext.prototype.addFilter = function(name, callback) {
  this.filters_[name] = callback;
};

/**
 * @param {string} name Filter identifier.
 */
FileListContext.prototype.removeFilter = function(name) {
  delete this.filters_[name];
};

/**
 * @param {bool} value If do not show hidden files.
 */
FileListContext.prototype.setFilterHidden = function(value) {
  if (value) {
    this.addFilter(
        'hidden',
        function(entry) {return entry.name.substr(0, 1) !== '.';}
    );
  } else {
    this.removeFilter('hidden');
  }
};

/**
 * @return {boolean} If the files with names starting with "." are not shown.
 */
FileListContext.prototype.isFilterHiddenOn = function() {
  return 'hidden' in this.filters_;
};

/**
 * @param {Entry} entry File entry.
 * @return {bool} True if the file should be shown, false otherwise.
 */
FileListContext.prototype.filter = function(entry) {
  for (var name in this.filters_) {
    if (!this.filters_[name](entry))
      return false;
  }
  return true;
};


/**
 * This class is responsible for scanning directory (or search results),
 * and filling the fileList. Different descendants handle various types of
 * directory contents shown: basic directory, gdata search results, local search
 * results.
 * @constructor
 * @param {FileListContext} context The file list context.
 */
function DirectoryContents(context) {
  this.context_ = context;
  this.fileList_ = context.fileList;
  this.scanCompletedCallback_ = null;
  this.scanFailedCallback_ = null;
  this.scanCancelled_ = false;
  this.filter_ = context.filter.bind(context);
  this.allChunksFetched_ = false;
  this.pendingMetadataRequests_ = 0;
  this.fileList_.prepareSort = this.prepareSort_.bind(this);
}

/**
 * DirectoryModel extends cr.EventTarget.
 */
DirectoryContents.prototype.__proto__ = cr.EventTarget.prototype;

/**
 * Create the copy of the object, but without scan started.
 * @return {DirectoryContents} Object copy.
 */
DirectoryContents.prototype.clone = function() {
  return new DirectoryContents(this.context_);
};

/**
 * Use a given fileList instead of the fileList from the context.
 * @param {Array|cr.ui.ArrayDataModel} fileList The new file list.
 */
DirectoryContents.prototype.setFileList = function(fileList) {
  this.fileList_ = fileList;
  this.fileList_.prepareSort = this.prepareSort_.bind(this);
};

/**
 * Use the filelist from the context and replace its contents with the entries
 * from the current fileList.
 */
DirectoryContents.prototype.replaceContextFileList = function() {
  if (this.context_.fileList !== this.fileList_) {
    var spliceArgs = [].slice.call(this.fileList_);
    var fileList = this.context_.fileList;
    spliceArgs.unshift(0, fileList.length);
    fileList.splice.apply(fileList, spliceArgs);
    this.fileList_ = fileList;
  }
};

/**
 * @return {string} The path.
 */
DirectoryContents.prototype.getPath = function() {
  throw 'Not implemented.';
};

/**
 * @return {boolean} If the scan is active.
 */
DirectoryContents.prototype.isScanning = function() {
  return !this.scanCancelled_ &&
         (!this.allChunksFetched_ || this.pendingMetadataRequests_ > 0);
};

/**
 * @return {boolean} True if search results (gdata or local).
 */
DirectoryContents.prototype.isSearch = function() {
  return false;
};

/**
 * @return {DirectoryEntry} A DirectoryEntry for current directory. In case of
 *     search -- the top directory from which search is run
 */
DirectoryContents.prototype.getDirectoryEntry = function() {
  throw 'Not implemented.';
};

/**
 * @param {Entry} entry File entry for a file in current DC results.
 * @return {string} Display name.
 */
DirectoryContents.prototype.getDisplayName = function(entry) {
  return entry.name;
};

/**
 * Start directory scan/search operation. Either 'scan-completed' or
 * 'scan-failed' event will be fired upon completion.
 */
DirectoryContents.prototype.scan = function() {
  throw 'Not implemented.';
};

/**
 * Read next chunk of results from DirectoryReader.
 * @protected
 */
DirectoryContents.prototype.readNextChunk = function() {
  throw 'Not implemented.';
};

/**
 * Cancel the running scan.
 */
DirectoryContents.prototype.cancelScan = function() {
  this.scanCancelled_ = true;
  cr.dispatchSimpleEvent(this, 'scan-cancelled');
};


/**
 * Called in case scan has failed. Should send the event.
 * @protected
 */
DirectoryContents.prototype.onError = function() {
  cr.dispatchSimpleEvent(this, 'scan-failed');
};

/**
 * Called in case scan has completed succesfully. Should send the event.
 * @protected
 */
DirectoryContents.prototype.lastChunkReceived = function() {
  this.allChunksFetched_ = true;
  if (!this.scanCancelled_ && this.pendingMetadataRequests_ === 0)
    cr.dispatchSimpleEvent(this, 'scan-completed');
};

/**
 * Cache necessary data before a sort happens.
 *
 * This is called by the table code before a sort happens, so that we can
 * go fetch data for the sort field that we may not have yet.
 * @private
 * @param {string} field Sort field.
 * @param {function} callback Called when done.
 */
DirectoryContents.prototype.prepareSort_ = function(field, callback) {
  this.prefetchMetadata(this.fileList_.slice(), callback);
};

/**
 * @param {Array.<Entry>} entries Files.
 * @param {function} callback Callback on done.
 */
DirectoryContents.prototype.prefetchMetadata = function(entries, callback) {
  this.context_.metadataCache.get(entries, 'filesystem', callback);
};

/**
 * @protected
 * @param {Array.<Entry>} entries File list.
 */
DirectoryContents.prototype.onNewEntries = function(entries) {
  if (this.scanCancelled_)
    return;

  var entriesFiltered = [].filter.call(entries, this.filter_);

  var onPrefetched = function() {
    this.pendingMetadataRequests_--;
    if (this.scanCancelled_)
      return;
    this.fileList_.push.apply(this.fileList_, entriesFiltered);

    if (this.pendingMetadataRequests === 0 && this.allChunksFetched_) {
      cr.dispatchSimpleEvent(this, 'scan-completed');
    }

    if (!this.allChunksFetched_)
      this.readNextChunk();
  };

  this.pendingMetadataRequests_++;
  this.prefetchMetadata(entriesFiltered, onPrefetched.bind(this));
};

/**
 * @param {string} name Directory name.
 * @param {function} successCallback Called on success.
 * @param {function} errorCallback On error.
 */
DirectoryContents.prototype.createDirectory = function(
    name, successCallback, errorCallback) {
  throw 'Not implemented.';
};


/**
 * @constructor
 * @extends {DirectoryContents}
 * @param {FileListContext} context File list context.
 * @param {DirectoryEntry} entry DirectoryEntry for current directory.
 */
function DirectoryContentsBasic(context, entry) {
  DirectoryContents.call(this, context);
  this.entry_ = entry;
}

/**
 * Extends DirectoryContents
 */
DirectoryContentsBasic.prototype.__proto__ = DirectoryContents.prototype;

/**
 * Create the copy of the object, but without scan started.
 * @return {DirectoryContentsBasic} Object copy.
 */
DirectoryContentsBasic.prototype.clone = function() {
  return new DirectoryContentsBasic(this.context_, this.entry_);
};

/**
 * @return {string} Current path.
 */
DirectoryContentsBasic.prototype.getPath = function() {
  return this.entry_.fullPath;
};

/**
 * @return {DirectoryEntry?} DirectoryEntry of the current directory.
 */
DirectoryContentsBasic.prototype.getDirectoryEntry = function() {
  return this.entry_;
};

/**
 * Start directory scan.
 */
DirectoryContentsBasic.prototype.scan = function() {
  if (this.entry_ === DirectoryModel.fakeGDataEntry_)
    return;

  metrics.startInterval('DirectoryScan');
  this.reader_ = this.entry_.createReader();
  this.readNextChunk();
};

/**
 * Read next chunk of results from DirectoryReader.
 * @protected
 */
DirectoryContentsBasic.prototype.readNextChunk = function() {
  this.reader_.readEntries(this.onChunkComplete_.bind(this),
                           this.onError.bind(this));
};

/**
 * @private
 * @param {Array.<Entry>} entries File list.
 */
DirectoryContentsBasic.prototype.onChunkComplete_ = function(entries) {
  if (this.scanCancelled_)
    return;

  if (entries.length == 0) {
    this.lastChunkReceived();
    this.recordMetrics_();
    return;
  }

  this.onNewEntries(entries);
};

/**
 * @private
 */
DirectoryContentsBasic.prototype.recordMetrics_ = function() {
  metrics.recordInterval('DirectoryScan');
  if (this.entry_.fullPath === RootDirectory.DOWNLOADS) {
    metrics.recordMediumCount('DownloadsCount', this.fileList_.length);
  }
};

/**
 * @param {string} name Directory name.
 * @param {function} successCallback Called on success.
 * @param {function} errorCallback On error.
 */
DirectoryContentsBasic.prototype.createDirectory = function(
    name, successCallback, errorCallback) {
  var onSuccess = function(newEntry) {
    this.prefetchMetadata([newEntry], function() {successCallback(newEntry);});
  }

  this.entry_.getDirectory(name, {create: true, exclusive: true},
                           onSuccess.bind(this), errorCallback);
};

/**
 * Delay to be used for gdata search scan.
 * The goal is to reduce the number of server requests when user is typing the
 * query.
 */
DirectoryContentsGDataSearch.SCAN_DELAY = 200;

/**
 * Number of results at which we stop the search.
 * Note that max number of shown results is MAX_RESULTS + search feed size.
 */
DirectoryContentsGDataSearch.MAX_RESULTS = 999;

/**
 * @constructor
 * @extends {DirectoryContents}
 * @param {FileListContext} context File list context.
 * @param {DirectoryEntry} dirEntry Current directory.
 * @param {string} query Search query.
 */
function DirectoryContentsGDataSearch(context, dirEntry, query) {
  DirectoryContents.call(this, context);
  this.query_ = query;
  this.directoryEntry_ = dirEntry;
  this.nextFeed_ = '';
  this.done_ = false;
  this.fetchedResultsNum_ = 0;
}

/**
 * Extends DirectoryContents.
 */
DirectoryContentsGDataSearch.prototype.__proto__ = DirectoryContents.prototype;

/**
 * Create the copy of the object, but without scan started.
 * @return {DirectoryContentsBasic} Object copy.
 */
DirectoryContentsGDataSearch.prototype.clone = function() {
  return new DirectoryContentsGDataSearch(
      this.context_, this.directoryEntry_, this.query_);
};

/**
 * @return {boolean} True if this is search results (yes).
 */
DirectoryContentsGDataSearch.prototype.isSearch = function() {
  return true;
};

/**
 * @return {DirectoryEntry} DirectoryEntry for current directory.
 */
DirectoryContentsGDataSearch.prototype.getDirectoryEntry = function() {
  return this.directoryEntry_;
};

/**
 * @return {string} The path.
 */
DirectoryContentsGDataSearch.prototype.getPath = function() {
  return this.directoryEntry_.fullPath;
};

/**
 * Start directory scan.
 */
DirectoryContentsGDataSearch.prototype.scan = function() {
  // Let's give another search a chance to cancel us before we begin.
  setTimeout(this.readNextChunk.bind(this),
             DirectoryContentsGDataSearch.SCAN_DELAY);
};

/**
 * All the results are read in one chunk, so when we try to read second chunk,
 * it means we're done.
 */
DirectoryContentsGDataSearch.prototype.readNextChunk = function() {
  if (this.scanCancelled_)
    return;

  if (this.done_) {
    this.lastChunkReceived();
    return;
  }

  var searchCallback = (function(entries, nextFeed) {
    // TODO(tbarzic): Improve error handling.
    if (!entries) {
      console.log('Drive search encountered an error');
      this.lastChunkReceived();
      return;
    }
    this.nextFeed_ = nextFeed;
    this.fetchedResultsNum_ += entries.length;
    if (this.fetchedResultsNum_ >= DirectoryContentsGDataSearch.MAX_RESULTS)
      this.nextFeed_ = '';

    this.done_ = (this.nextFeed_ == '');
    this.onNewEntries(entries);
  }).bind(this);

  chrome.fileBrowserPrivate.searchGData(this.query_,
                                        this.nextFeed_,
                                        searchCallback);
};


/**
 * @constructor
 * @extends {DirectoryContents}
 * @param {FileListContext} context File list context.
 * @param {DirectoryEntry} dirEntry Current directory.
 * @param {string} query Search query.
 */
function DirectoryContentsLocalSearch(context, dirEntry, query) {
  DirectoryContents.call(this, context);
  this.directoryEntry_ = dirEntry;
  this.query_ = query.toLowerCase();
}

/**
 * Extends DirectoryContents
 */
DirectoryContentsLocalSearch.prototype.__proto__ = DirectoryContents.prototype;

/**
 * Create the copy of the object, but without scan started.
 * @return {DirectoryContentsBasic} Object copy.
 */
DirectoryContentsLocalSearch.prototype.clone = function() {
  return new DirectoryContentsLocalSearch(
      this.context_, this.directoryEntry_, this.query_);
};

/**
 * @return {string} The path.
 */
DirectoryContentsLocalSearch.prototype.getPath = function() {
  return this.directoryEntry_.fullPath;
};

/**
 * @return {boolean} True if search results (gdata or local).
 */
DirectoryContentsLocalSearch.prototype.isSearch = function() {
  return true;
};

/**
 * @return {DirectoryEntry} A DirectoryEntry for current directory. In case of
 *     search -- the top directory from which search is run
 */
DirectoryContentsLocalSearch.prototype.getDirectoryEntry = function() {
  return this.directoryEntry_;
};

/**
 * @param {Entry} entry File entry for a file in current DC results.
 * @return {string} Display name.
 */
DirectoryContentsLocalSearch.prototype.getDisplayName = function(entry) {
  return entry.name;
};

/**
 * Start directory scan/search operation. Either 'scan-completed' or
 * 'scan-failed' event will be fired upon completion.
 */
DirectoryContentsLocalSearch.prototype.scan = function() {
  this.pendingScans_ = 0;
  this.scanDirectory_(this.directoryEntry_);
};

/**
 * Scan a directory.
 * @param {DirectoryEntry} entry A directory to scan.
 * @private
 */
DirectoryContentsLocalSearch.prototype.scanDirectory_ = function(entry) {
  this.pendingScans_++;
  var reader = entry.createReader();
  var found = [];

  var onChunkComplete = function(entries) {
    if (this.scanCancelled_)
      return;

    if (entries.length === 0) {
      if (found.length > 0)
        this.onNewEntries(found);
      this.pendingScans_--;
      if (this.pendingScans_ === 0)
        this.lastChunkReceived();
      return;
    }

    for (var i = 0; i < entries.length; i++) {
      if (entries[i].name.toLowerCase().indexOf(this.query_) != -1) {
        found.push(entries[i]);
      }

      if (entries[i].isDirectory)
        this.scanDirectory_(entries[i]);
    }

    getNextChunk();
  }.bind(this);

  var getNextChunk = function() {
    reader.readEntries(onChunkComplete, this.onError.bind(this));
  }.bind(this);

  getNextChunk();
};

/**
 * We get results for each directory in one go in scanDirectory_.
 */
DirectoryContentsLocalSearch.prototype.readNextChunk = function() {
};
