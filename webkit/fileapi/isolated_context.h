// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_FILEAPI_ISOLATED_CONTEXT_H_
#define WEBKIT_FILEAPI_ISOLATED_CONTEXT_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/memory/singleton.h"
#include "base/synchronization/lock.h"
#include "base/lazy_instance.h"
#include "webkit/fileapi/file_system_types.h"
#include "webkit/fileapi/fileapi_export.h"

namespace fileapi {

// Manages isolated filename namespaces.  A namespace is simply defined as a
// set of file paths and corresponding filesystem ID.  This context class is
// a singleton and access to the context is thread-safe (protected with a
// lock).
// Some methods of this class are virtual just for mocking.
class FILEAPI_EXPORT IsolatedContext {
 public:
  struct FILEAPI_EXPORT FileInfo {
    FileInfo();
    FileInfo(const std::string& name, const FilePath& path);

    // The name to be used to register the file. The registered file can
    // be referred by a virtual path /<filesystem_id>/<name>.
    // The name should NOT contain a path separator '/'.
    std::string name;

    // The path of the file.
    FilePath path;

    // For STL operation.
    bool operator<(const FileInfo& that) const { return name < that.name; }
  };

  class FILEAPI_EXPORT FileInfoSet {
   public:
    FileInfoSet();
    ~FileInfoSet();

    // Add the given |path| to the set and populates |registered_name| with
    // the registered name assigned for the path. |path| needs to be
    // absolute and should not contain parent references.
    // Return false if the |path| is not valid and could not be added.
    bool AddPath(const FilePath& path, std::string* registered_name);

    // Add the given |path| with the |name|.
    // Return false if the |name| is already registered in the set or
    // is not valid and could not be added.
    bool AddPathWithName(const FilePath& path, const std::string& name);

    const std::set<FileInfo>& fileset() const { return fileset_; }

   private:
    std::set<FileInfo> fileset_;
  };

  // The instance is lazily created per browser process.
  static IsolatedContext* GetInstance();

  // Registers a new isolated filesystem with the given FileInfoSet |files|
  // and returns the new filesystem_id.  The files are registered with their
  // register_name as their keys so that later we can resolve the full paths
  // for the given name.  We only expose the name and the ID for the
  // newly created filesystem to the renderer for the sake of security.
  //
  // The renderer will be sending filesystem requests with a virtual path like
  // '/<filesystem_id>/<registered_name>/<relative_path_from_the_dropped_path>'
  // for which we could crack in the browser process by calling
  // CrackIsolatedPath to get the full path.
  //
  // For example: if a dropped file has a path like '/a/b/foo' and we register
  // the path with the name 'foo' in the newly created filesystem.
  // Later if the context is asked to crack a virtual path like '/<fsid>/foo'
  // it can properly return the original path '/a/b/foo' by looking up the
  // internal mapping.  Similarly if a dropped entry is a directory and its
  // path is like '/a/b/dir' a virtual path like '/<fsid>/dir/foo' can be
  // cracked into '/a/b/dir/foo'.
  //
  // Note that the path in |fileset| that contains '..' or is not an
  // absolute path is skipped and is not registered.
  std::string RegisterDraggedFileSystem(const FileInfoSet& files);

  // Registers a new isolated filesystem for a given |path| of filesystem
  // |type| filesystem and returns a new filesystem ID.
  // |path| must be an absolute path which has no parent references ('..').
  // If |register_name| is non-null and has non-empty string the path is
  // registered as the given |register_name|, otherwise it is populated
  // with the name internally assigned to the path.
  std::string RegisterFileSystemForPath(FileSystemType type,
                                        const FilePath& path,
                                        std::string* register_name);

  // Revokes all filesystem(s) registered for the given path.
  // This is assumed to be called when the registered path becomes
  // globally invalid, e.g. when a device for the path is detached.
  //
  // Note that this revokes the filesystem no matter how many references it has.
  // It is ok to call this for the path that has no associated filesystems.
  // Note that this only works for the filesystems registered by
  // |RegisterFileSystemForPath|.
  void RevokeFileSystemByPath(const FilePath& path);

  // Adds a reference to a filesystem specified by the given filesystem_id.
  void AddReference(const std::string& filesystem_id);

  // Removes a reference to a filesystem specified by the given filesystem_id.
  // If the reference count reaches 0 the isolated context gets destroyed.
  // It is ok to call this on the filesystem that has been already deleted
  // (e.g. by RevokeFileSystemByPath).
  void RemoveReference(const std::string& filesystem_id);

  // Cracks the given |virtual_path| (which should look like
  // "/<filesystem_id>/<registered_name>/<relative_path>") and populates
  // the |filesystem_id| and |path| if the embedded <filesystem_id>
  // is registered to this context.  |root_path| is also populated to have
  // the registered root (toplevel) file info for the |virtual_path|.
  //
  // Returns false if the given virtual_path or the cracked filesystem_id
  // is not valid.
  //
  // Note that |path| is set to empty paths if |virtual_path| has no
  // <relative_path> part (i.e. pointing to the virtual root).
  bool CrackIsolatedPath(const FilePath& virtual_path,
                         std::string* filesystem_id,
                         FileSystemType* type,
                         FilePath* path) const;

  // Returns a set of dragged FileInfo's registered for the |filesystem_id|.
  // The filesystem_id must be pointing to a dragged file system
  // (i.e. must be the one registered by RegisterDraggedFileSystem).
  // Returns false if the |filesystem_id| is not valid.
  bool GetDraggedFileInfo(const std::string& filesystem_id,
                          std::vector<FileInfo>* files) const;

  // Returns the file path registered for the |filesystem_id|.
  // The filesystem_id must NOT be pointing to a dragged file system
  // (i.e. must be the one registered by RegisterFileSystemForPath).
  // Returns false if the |filesystem_id| is not valid.
  bool GetRegisteredPath(const std::string& filesystem_id,
                         FilePath* path) const;

  // Returns the virtual root path that looks like /<filesystem_id>.
  FilePath CreateVirtualRootPath(const std::string& filesystem_id) const;

 private:
  friend struct base::DefaultLazyInstanceTraits<IsolatedContext>;

  // Represents each isolated file system instance.
  class Instance {
   public:
    // For a single-path file system, which could be registered by
    // IsolatedContext::RegisterFileSystemForPath().
    // Most of isolated file system contexts should be of this type.
    Instance(FileSystemType type, const FileInfo& file_info);

    // For a multi-paths file system.  As of writing only file system
    // type which could have multi-paths is Dragged file system, and
    // could be registered by IsolatedContext::RegisterDraggedFileSystem().
    Instance(FileSystemType type, const std::set<FileInfo>& files);

    ~Instance();

    FileSystemType type() const { return type_; }
    const FileInfo& file_info() const { return file_info_; }
    const std::set<FileInfo>& files() const { return files_; }
    int ref_counts() const { return ref_counts_; }

    void AddRef() { ++ref_counts_; }
    void RemoveRef() { --ref_counts_; }

    bool ResolvePathForName(const std::string& name, FilePath* path) const;

    // Returns true if the instance is a single-path instance.
    bool IsSinglePathInstance() const;

   private:
    const FileSystemType type_;

    // For single-path instance.
    const FileInfo file_info_;

    // For multiple-path instance (e.g. dragged file system).
    const std::set<FileInfo> files_;

    // Reference counts. Note that an isolated filesystem is created with ref==0
    // and will get deleted when the ref count reaches <=0.
    int ref_counts_;

    DISALLOW_COPY_AND_ASSIGN(Instance);
  };

  typedef std::map<std::string, Instance*> IDToInstance;

  // Reverse map from registered path to IDs.
  typedef std::map<FilePath, std::set<std::string> > PathToID;

  // Obtain an instance of this class via GetInstance().
  IsolatedContext();
  ~IsolatedContext();

  // Returns a new filesystem_id.  Called with lock.
  std::string GetNewFileSystemId() const;

  // This lock needs to be obtained when accessing the instance_map_.
  mutable base::Lock lock_;

  IDToInstance instance_map_;
  PathToID path_to_id_map_;

  DISALLOW_COPY_AND_ASSIGN(IsolatedContext);
};

}  // namespace fileapi

#endif  // WEBKIT_FILEAPI_ISOLATED_CONTEXT_H_
