// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Type of a root directory.
 * @enum
 */
var RootType = {
  DOWNLOADS: 'downloads',
  ARCHIVE: 'archive',
  REMOVABLE: 'removable',
  GDATA: 'gdata'
};

/**
 * Top directory for each root type.
 * @type {Object.<RootType,string>}
 */
var RootDirectory = {
  DOWNLOADS: '/Downloads',
  ARCHIVE: '/archive',
  REMOVABLE: '/removable',
  GDATA: '/drive'
};

var PathUtil = {};

/**
 * @param {string} path Path starting with '/'.
 * @return {string} Root directory (starting with '/').
 */
PathUtil.getRootDirectory = function(path) {
  var i = path.indexOf('/', 1);
  return i === -1 ? path.substring(0) : path.substring(0, i);
};

/**
 * @param {string} path Any unix-style path (may start or not start from root).
 * @return {Array.<string>} path components
 */
PathUtil.split = function(path) {
  var fromRoot = false;
  if (path[0] === '/') {
    fromRoot = true;
    path = path.substring(1);
  }

  var components = path.split('/');
  if (fromRoot)
    components[0] = '/' + components[0];
  return components;
};

/**
 * Join path components into a single path. Can be called either with a list of
 * components as arguments, or with an array of components as the only argument.
 *
 * Examples:
 * Path.join('abc', 'def') -> 'abc/def'
 * Path.join('/', 'abc', 'def/ghi') -> '/abc/def/ghi'
 * Path.join(['/abc/def', 'ghi']) -> '/abc/def/ghi'
 *
 * @return {string} Resulting path.
 */
PathUtil.join = function() {
  var components;

  if (arguments.length === 1 && typeof(arguments[0]) === 'object') {
    components = arguments[0];
  } else {
    components = arguments;
  }

  var path = '';
  for (var i = 0; i < components.length; i++) {
    if (components[i][0] === '/') {
      path = components[i];
      continue;
    }
    if (path.length === 0 || path[path.length - 1] !== '/')
      path += '/';
    path += components[i];
  }
  return path;
};

/**
 * @param {string} path Path starting with '/'.
 * @return {RootType} RootType.DOWNLOADS, RootType.GDATA etc.
 */
PathUtil.getRootType = function(path) {
  var rootDir = PathUtil.getRootDirectory(path);
  for (var type in RootDirectory) {
    if (rootDir === RootDirectory[type])
      return RootType[type];
  }
};

/**
 * @param {string} path Any path.
 * @return {string} The root path.
 */
PathUtil.getRootPath = function(path) {
  var type = PathUtil.getRootType(path);

  if (type == RootType.DOWNLOADS || type == RootType.GDATA)
    return PathUtil.getRootDirectory(path);

  if (type == RootType.ARCHIVE || type == RootType.REMOVABLE) {
    var components = PathUtil.split(path);
    if (components.length > 1) {
      return PathUtil.join(components[0], components[1]);
    } else {
      return components[0];
    }
  }

  return '/';
};

/**
 * @param {string} path A path.
 * @return {boolean} True if it is a path to the root.
 */
PathUtil.isRootPath = function(path) {
  return PathUtil.getRootPath(path) === path;
};

/**
 * @param {string} parent_path The parent path.
 * @param {string} child_path The child path.
 * @return {boolean} True if |parent_path| is parent file path of |child_path|.
 */
PathUtil.isParentPath = function(parent_path, child_path) {
  if (!parent_path || parent_path.length == 0 ||
      !child_path || child_path.length == 0)
    return false;

  if (parent_path[parent_path.length - 1] != '/')
    parent_path += '/';

  if (child_path[child_path.length - 1] != '/')
    child_path += '/';

  return child_path.indexOf(parent_path) == 0;
};

/**
 * Return the localized name for the root.
 * @param {string} path The full path of the root (starting with slash).
 * @return {string} The localized name.
 */
PathUtil.getRootLabel = function(path) {
  function str(id) {
    return loadTimeData.getString(id);
  }

  if (path === RootDirectory.DOWNLOADS)
    return str('DOWNLOADS_DIRECTORY_LABEL');

  if (path === RootDirectory.ARCHIVE)
    return str('ARCHIVE_DIRECTORY_LABEL');
  if (PathUtil.isParentPath(RootDirectory.ARCHIVE, path))
    return path.substring(RootDirectory.ARCHIVE.length + 1);

  if (path === RootDirectory.REMOVABLE)
    return str('REMOVABLE_DIRECTORY_LABEL');
  if (PathUtil.isParentPath(RootDirectory.REMOVABLE, path))
    return path.substring(RootDirectory.REMOVABLE.length + 1);

  if (path === RootDirectory.GDATA)
    return str('DRIVE_DIRECTORY_LABEL');

  return path;
};


