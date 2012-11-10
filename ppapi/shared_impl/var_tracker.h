// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_SHARED_IMPL_VAR_TRACKER_H_
#define PPAPI_SHARED_IMPL_VAR_TRACKER_H_

#include <vector>

#include "base/basictypes.h"
#include "base/hash_tables.h"
#include "base/memory/ref_counted.h"
#include "base/threading/non_thread_safe.h"
#include "ppapi/c/pp_instance.h"
#include "ppapi/c/pp_module.h"
#include "ppapi/c/pp_var.h"
#include "ppapi/shared_impl/ppapi_shared_export.h"

namespace ppapi {

class ArrayBufferVar;
class Var;

// Tracks non-POD (refcounted) var objects held by a plugin.
//
// The tricky part is the concept of a "tracked object". These are only
// necessary in the plugin side of the proxy when running out of process. A
// tracked object is one that the plugin is aware of, but doesn't hold a
// reference to. This will happen when the plugin is passed an object as an
// argument from the host (renderer) as an input argument to a sync function,
// but where ownership is not passed.
//
// This class maintains the "track_with_no_reference_count" but doesn't do
// anything with it other than call virtual functions. The interesting parts
// are added by the PluginObjectVar derived from this class.
class PPAPI_SHARED_EXPORT VarTracker
#ifdef ENABLE_PEPPER_THREADING
    : NON_EXPORTED_BASE(public base::NonThreadSafeDoNothing) {
#else
    // TODO(dmichael): Remove the thread checking when calls are allowed off the
    // main thread (crbug.com/92909).
    : NON_EXPORTED_BASE(public base::NonThreadSafe) {
#endif
 public:
  VarTracker();
  virtual ~VarTracker();

  // Called by the Var object to add a new var to the tracker.
  int32 AddVar(Var* var);

  // Looks up a given var and returns a reference to the Var if it exists.
  // Returns NULL if the var type is not an object we track (POD) or is
  // invalid.
  Var* GetVar(int32 var_id) const;
  Var* GetVar(const PP_Var& var) const;

  // Increases a previously-known Var ID's refcount, returning true on success,
  // false if the ID is invalid. The PP_Var version returns true and does
  // nothing for non-refcounted type vars.
  bool AddRefVar(int32 var_id);
  bool AddRefVar(const PP_Var& var);

  // Decreases the given Var ID's refcount, returning true on success, false if
  // the ID is invalid or if the refcount was already 0. The PP_Var version
  // returns true and does nothing for non-refcounted type vars. The var will
  // be deleted if there are no more refs to it.
  bool ReleaseVar(int32 var_id);
  bool ReleaseVar(const PP_Var& var);

  // Create a new array buffer of size |size_in_bytes|. Return a PP_Var that
  // that references it and has an initial reference-count of 1.
  PP_Var MakeArrayBufferPPVar(uint32 size_in_bytes);
  // Same as above, but copy the contents of |data| in to the new array buffer.
  PP_Var MakeArrayBufferPPVar(uint32 size_in_bytes, const void* data);

  // Return a vector containing all PP_Vars that are in the tracker. This is
  // to help implement PPB_Testing_Dev.GetLiveVars and should generally not be
  // used in production code. The PP_Vars are returned in no particular order,
  // and their reference counts are unaffected.
  std::vector<PP_Var> GetLiveVars();

  // Retrieves the internal reference counts for testing. Returns 0 if we
  // know about the object but the corresponding value is 0, or -1 if the
  // given object ID isn't in our map.
  int GetRefCountForObject(const PP_Var& object);
  int GetTrackedWithNoReferenceCountForObject(const PP_Var& object);

  // Called after an instance is deleted to do var cleanup.
  virtual void DidDeleteInstance(PP_Instance instance) = 0;

 protected:
  struct VarInfo {
    VarInfo();
    VarInfo(Var* v, int input_ref_count);

    scoped_refptr<Var> var;

    // Explicit reference count. This value is affected by the renderer calling
    // AddRef and Release. A nonzero value here is represented by a single
    // reference in the host on our behalf (this reduces IPC traffic).
    int ref_count;

    // Tracked object count (see class comment above).
    //
    // "TrackObjectWithNoReference" might be called recursively in rare cases.
    // For example, say the host calls a plugin function with an object as an
    // argument, and in response, the plugin calls a host function that then
    // calls another (or the same) plugin function with the same object.
    //
    // This value tracks the number of calls to TrackObjectWithNoReference so
    // we know when we can stop tracking this object.
    int track_with_no_reference_count;
  };
  typedef base::hash_map<int32, VarInfo> VarMap;

  // Specifies what should happen with the refcount when calling AddVarInternal.
  enum AddVarRefMode {
    ADD_VAR_TAKE_ONE_REFERENCE,
    ADD_VAR_CREATE_WITH_NO_REFERENCE
  };

  // Implementation of AddVar that allows the caller to specify whether the
  // initial refcount of the added object will be 0 or 1.
  //
  // Overridden in the plugin proxy to do additional object tracking.
  virtual int32 AddVarInternal(Var* var, AddVarRefMode mode);

  // Convenience functions for doing lookups into the live_vars_ map.
  VarMap::iterator GetLiveVar(int32 id);
  VarMap::iterator GetLiveVar(const PP_Var& var);
  VarMap::const_iterator GetLiveVar(const PP_Var& var) const;

  // Returns true if the given vartype is refcounted and has associated objects
  // (it's not POD).
  bool IsVarTypeRefcounted(PP_VarType type) const;

  // Called when AddRefVar increases a "tracked" ProxyObject's refcount from
  // zero to one. In the plugin side of the proxy, we need to send some
  // messages to the host. In the host side, this should never be called since
  // there are no proxy objects.
  virtual void TrackedObjectGettingOneRef(VarMap::const_iterator iter);

  // Called when ReleaseVar decreases a object's refcount from one to zero. It
  // may still be "tracked" (has a "track_with_no_reference_count") value. In
  // the plugin side of the proxy, we need to tell the host that we no longer
  // have a reference. In the host side, this should never be called since
  // there are no proxy objects.
  virtual void ObjectGettingZeroRef(VarMap::iterator iter);

  // Called when an object may have had its refcount or
  // track_with_no_reference_count value decreased. If the object has neither
  // refs anymore, this will remove it and return true. Returns false if it's
  // still alive.
  //
  // Overridden by the PluginVarTracker to also clean up the host info map.
  virtual bool DeleteObjectInfoIfNecessary(VarMap::iterator iter);

  VarMap live_vars_;

  // Last assigned var ID.
  int32 last_var_id_;

 private:
  // Create and return a new ArrayBufferVar size_in_bytes bytes long. This is
  // implemented by the Host and Plugin tracker separately, so that it can be
  // a real WebKit ArrayBuffer on the host side.
  virtual ArrayBufferVar* CreateArrayBuffer(uint32 size_in_bytes) = 0;

  DISALLOW_COPY_AND_ASSIGN(VarTracker);
};

}  // namespace ppapi

#endif  // PPAPI_SHARED_IMPL_VAR_TRACKER_H_
