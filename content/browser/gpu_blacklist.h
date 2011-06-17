// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_GPU_BLACKLIST_H_
#define CONTENT_BROWSER_GPU_BLACKLIST_H_
#pragma once

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/values.h"
#include "content/common/gpu_feature_flags.h"

class DictionaryValue;
class Version;
struct GPUInfo;

class GpuBlacklist {
 public:
  enum OsType {
    kOsLinux,
    kOsMacosx,
    kOsWin,
    kOsChromeOS,
    kOsAny,
    kOsUnknown
  };

  explicit GpuBlacklist(const std::string& browser_version_string);
  ~GpuBlacklist();

  // Loads blacklist information from a json file.
  // current_os_only==true indicates all blacklist entries that don't belong to
  // the current OS are discarded; current_os_only==false should only be used
  // for testing purpose.
  // If failed, the current GpuBlacklist is un-touched.
  bool LoadGpuBlacklist(const std::string& json_context,
                        bool current_os_only);
  bool LoadGpuBlacklist(const DictionaryValue& parsed_json,
                        bool current_os_only);

  // Collects system information and combines them with gpu_info and blacklist
  // information to determine gpu feature flags.
  // If os is kOsAny, use the current OS; if os_version is null, use the
  // current OS version.
  GpuFeatureFlags DetermineGpuFeatureFlags(OsType os,
                                           Version* os_version,
                                           const GPUInfo& gpu_info);

  // Collects the entries that set the "feature" flag from the last
  // DetermineGpuFeatureFlags() call.  This tells which entries are responsible
  // for raising a certain flag, i.e, for blacklisting a certain feature.
  // Examples of "feature":
  //   kGpuFeatureAll - any of the supported features;
  //   kGpuFeatureWebgl - a single feature;
  //   kGpuFeatureWebgl | kGpuFeatureAcceleratedCompositing - two features.
  void GetGpuFeatureFlagEntries(GpuFeatureFlags::GpuFeatureType feature,
                                std::vector<uint32>& entry_ids) const;


  // Returns status information on the blacklist. This is two parted:
  // {
  //    featureStatus: []
  //    problems: []
  // }
  //
  // Each entry in feature_status has:
  // {
  //    name:  "name of feature"
  //    status: "enabled" | "unavailable_software" | "unavailable_off" |
  //            "software" | "disabled_off" | "disabled_softare";
  // }
  //
  // The features reported are not 1:1 with GpuFeatureType. Rather, they are:
  //    '2d_canvas'
  //    '3d_css'
  //    'composting',
  //    'webgl',
  //    'multisampling'
  //
  // Each problems has:
  // {
  //    "description": "Your GPU is too old",
  //    "crBugs": [1234],
  //    "webkitBugs": []
  // }
  Value* GetFeatureStatus(bool gpu_access_allowed,
                          bool disable_accelerated_compositing,
                          bool enable_accelerated_2D_canvas,
                          bool disable_experimental_webgl,
                          bool disable_multisampling) const;

  // Return the largest entry id.  This is used for histogramming.
  uint32 max_entry_id() const;

  // Collects the version of the current blacklist.  Returns false and sets
  // major and minor to 0 on failure.
  bool GetVersion(uint16* major, uint16* monir) const;

  // Collects the version of the current blacklist from a parsed json file.
  // Returns false and sets major and minor to 0 on failure.
  static bool GetVersion(
      const DictionaryValue& parsed_json, uint16* major, uint16* minor);

 private:
  class VersionInfo {
   public:
    VersionInfo(const std::string& version_op,
                const std::string& version_string,
                const std::string& version_string2);
    ~VersionInfo();

    // Determines if a given version is included in the VersionInfo range.
    bool Contains(const Version& version) const;

    // Determines if the VersionInfo contains valid information.
    bool IsValid() const;

   private:
    enum Op {
      kBetween,  // <= * <=
      kEQ,  // =
      kLT,  // <
      kLE,  // <=
      kGT,  // >
      kGE,  // >=
      kAny,
      kUnknown  // Indicates VersionInfo data is invalid.
    };

    // Maps string to Op; returns kUnknown if it's not a valid Op.
    static Op StringToOp(const std::string& version_op);

    Op op_;
    scoped_ptr<Version> version_;
    scoped_ptr<Version> version2_;
  };

  class OsInfo {
   public:
    OsInfo(const std::string& os,
           const std::string& version_op,
           const std::string& version_string,
           const std::string& version_string2);
    ~OsInfo();

    // Determines if a given os/version is included in the OsInfo set.
    bool Contains(OsType type, const Version& version) const;

    // Determines if the VersionInfo contains valid information.
    bool IsValid() const;

    OsType type() const;

    // Maps string to OsType; returns kOsUnknown if it's not a valid os.
    static OsType StringToOsType(const std::string& os);

   private:
    OsType type_;
    scoped_ptr<VersionInfo> version_info_;
  };

  class StringInfo {
   public:
    StringInfo(const std::string& string_op, const std::string& string_value);

    // Determines if a given string is included in the StringInfo.
    bool Contains(const std::string& value) const;

    // Determines if the StringInfo contains valid information.
    bool IsValid() const;

   private:
    enum Op {
      kContains,
      kBeginWith,
      kEndWith,
      kEQ,  // =
      kUnknown  // Indicates StringInfo data is invalid.
    };

    // Maps string to Op; returns kUnknown if it's not a valid Op.
    static Op StringToOp(const std::string& string_op);

    Op op_;
    std::string value_;
  };

  class GpuBlacklistEntry {
   public:
    // Constructs GpuBlacklistEntry from DictionaryValue loaded from json.
    // Top-level entry must have an id number.  Others are exceptions.
    static GpuBlacklistEntry* GetGpuBlacklistEntryFromValue(
        DictionaryValue* value, bool top_level);

    // Determines if a given os/gc/driver is included in the Entry set.
    bool Contains(OsType os_type,
                  const Version& os_version,
                  const GPUInfo& gpu_info) const;

    // Returns the OsType.
    OsType GetOsType() const;

    // Returns the entry's unique id.  0 is reserved.
    uint32 id() const;

    // Returns the description of the entry
    const std::string& description() const { return description_; }

    // Returs a list of Chromium and Webkit bugs applicable to this entry
    const std::vector<int>& cr_bugs() const { return cr_bugs_; }
    const std::vector<int>& webkit_bugs() const { return webkit_bugs_; }

    // Returns the GpuFeatureFlags.
    GpuFeatureFlags GetGpuFeatureFlags() const;

    ~GpuBlacklistEntry();

   private:
    GpuBlacklistEntry();

    bool SetId(uint32 id);

    bool SetOsInfo(const std::string& os,
                   const std::string& version_op,
                   const std::string& version_string,
                   const std::string& version_string2);

    bool SetVendorId(const std::string& vendor_id_string);

    bool AddDeviceId(const std::string& device_id_string);

    bool SetDriverVendorInfo(const std::string& vendor_op,
                             const std::string& vendor_value);

    bool SetDriverVersionInfo(const std::string& version_op,
                              const std::string& version_string,
                              const std::string& version_string2);

    bool SetDriverDateInfo(const std::string& date_op,
                           const std::string& date_string,
                           const std::string& date_string2);

    bool SetGLRendererInfo(const std::string& renderer_op,
                           const std::string& renderer_value);

    bool SetBlacklistedFeatures(
        const std::vector<std::string>& blacklisted_features);

    void AddException(GpuBlacklistEntry* exception);

    uint32 id_;
    std::string description_;
    std::vector<int> cr_bugs_;
    std::vector<int> webkit_bugs_;
    scoped_ptr<OsInfo> os_info_;
    uint32 vendor_id_;
    std::vector<uint32> device_id_list_;
    scoped_ptr<StringInfo> driver_vendor_info_;
    scoped_ptr<VersionInfo> driver_version_info_;
    scoped_ptr<VersionInfo> driver_date_info_;
    scoped_ptr<StringInfo> gl_renderer_info_;
    scoped_ptr<GpuFeatureFlags> feature_flags_;
    std::vector<GpuBlacklistEntry*> exceptions_;
  };

  enum BrowserVersionSupport {
    kSupported,
    kUnsupported,
    kMalformed
  };

  // Gets the current OS type.
  static OsType GetOsType();

  void Clear();

  bool IsFeatureBlacklisted(GpuFeatureFlags::GpuFeatureType feature) const;

  // Check if the entry is supported by the current version of browser.
  // By default, if there is no browser version information in the entry,
  // return kSupported;
  BrowserVersionSupport IsEntrySupportedByCurrentBrowserVersion(
      DictionaryValue* value);

  scoped_ptr<Version> version_;
  std::vector<GpuBlacklistEntry*> blacklist_;

  scoped_ptr<Version> browser_version_;

  // This records all the blacklist entries that are appliable to the current
  // user machine.  It is updated everytime DetermineGpuFeatureFlags() is
  // called and is used later by GetGpuFeatureFlagEntries().
  std::vector<GpuBlacklistEntry*> active_entries_;

  uint32 max_entry_id_;

  DISALLOW_COPY_AND_ASSIGN(GpuBlacklist);
};

#endif  // CONTENT_BROWSER_GPU_BLACKLIST_H_
