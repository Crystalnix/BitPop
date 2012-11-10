// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_GPU_BLACKLIST_H_
#define CHROME_BROWSER_GPU_BLACKLIST_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "base/values.h"
#include "content/public/browser/gpu_data_manager_observer.h"
#include "content/public/common/gpu_feature_type.h"

class Version;

namespace content {
struct GPUInfo;
}

class GpuBlacklist : public content::GpuDataManagerObserver {
 public:
  enum OsType {
    kOsLinux,
    kOsMacosx,
    kOsWin,
    kOsChromeOS,
    kOsAny,
    kOsUnknown
  };

  enum OsFilter {
    // In loading, ignore all entries that belong to other OS.
    kCurrentOsOnly,
    // In loading, keep all entries. This is for testing only.
    kAllOs
  };

  // Getter for the singleton. This will return NULL on failure.
  static GpuBlacklist* GetInstance();

  virtual ~GpuBlacklist();

  // Loads blacklist information from a json file.
  // If failed, the current GpuBlacklist is un-touched.
  bool LoadGpuBlacklist(const std::string& json_context,
                        OsFilter os_filter);

  // Collects system information and combines them with gpu_info and blacklist
  // information to determine gpu feature flags.
  // If os is kOsAny, use the current OS; if os_version is null, use the
  // current OS version.
  content::GpuFeatureType DetermineGpuFeatureType(
      OsType os, Version* os_version, const content::GPUInfo& gpu_info);

  // Helper function that calls DetermineGpuFeatureType and sets the updated
  // features on GpuDataManager.
  void UpdateGpuDataManager();

  // Collects the active entries that set the "feature" flag from the last
  // DetermineGpuFeatureType() call.  This tells which entries are responsible
  // for raising a certain flag, i.e, for blacklisting a certain feature.
  // Examples of "feature":
  //   GPU_FEATURE_TYPE_ALL - any of the supported features;
  //   GPU_FEATURE_TYPE_WEBGL - a single feature;
  //   GPU_FEATURE_TYPE_WEBGL | GPU_FEATURE_TYPE_ACCELERATED_COMPOSITING
  //       - two features.
  // If disabled set to true, return entries that are disabled; otherwise,
  // return enabled entries.
  void GetGpuFeatureTypeEntries(content::GpuFeatureType feature,
                                std::vector<uint32>& entry_ids,
                                bool disabled) const;

  // Returns the description and bugs from active entries from the last
  // DetermineGpuFeatureType() call.
  //
  // Each problems has:
  // {
  //    "description": "Your GPU is too old",
  //    "crBugs": [1234],
  //    "webkitBugs": []
  // }
  void GetBlacklistReasons(ListValue* problem_list) const;

  // Return the largest entry id.  This is used for histogramming.
  uint32 max_entry_id() const;

  // Returns the version of the current blacklist.
  std::string GetVersion() const;

 private:
  friend class GpuBlacklistTest;
  friend struct DefaultSingletonTraits<GpuBlacklist>;
  FRIEND_TEST_ALL_PREFIXES(GpuBlacklistTest, ChromeVersionEntry);
  FRIEND_TEST_ALL_PREFIXES(GpuBlacklistTest, CurrentBlacklistValidation);
  FRIEND_TEST_ALL_PREFIXES(GpuBlacklistTest, UnknownField);
  FRIEND_TEST_ALL_PREFIXES(GpuBlacklistTest, UnknownExceptionField);
  FRIEND_TEST_ALL_PREFIXES(GpuBlacklistTest, UnknownFeature);

  enum BrowserVersionSupport {
    kSupported,
    kUnsupported,
    kMalformed
  };

  enum NumericOp {
    kBetween,  // <= * <=
    kEQ,  // =
    kLT,  // <
    kLE,  // <=
    kGT,  // >
    kGE,  // >=
    kAny,
    kUnknown  // Indicates the data is invalid.
  };

  class VersionInfo {
   public:
    // If version_style is empty, it defaults to kNumerical.
    VersionInfo(const std::string& version_op,
                const std::string& version_style,
                const std::string& version_string,
                const std::string& version_string2);
    ~VersionInfo();

    // Determines if a given version is included in the VersionInfo range.
    bool Contains(const Version& version) const;

    // Determine if the version_style is lexical.
    bool IsLexical() const;

    // Determines if the VersionInfo contains valid information.
    bool IsValid() const;

   private:
    enum VersionStyle {
      kVersionStyleNumerical,
      kVersionStyleLexical,
      kVersionStyleUnknown
    };

    static VersionStyle StringToVersionStyle(const std::string& version_style);

    NumericOp op_;
    VersionStyle version_style_;
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

  class FloatInfo {
   public:
    FloatInfo(const std::string& float_op,
              const std::string& float_value,
              const std::string& float_value2);

    // Determines if a given float is included in the FloatInfo.
    bool Contains(float value) const;

    // Determines if the FloatInfo contains valid information.
    bool IsValid() const;

   private:
    NumericOp op_;
    float value_;
    float value2_;
  };

  class GpuBlacklistEntry;
  typedef scoped_refptr<GpuBlacklistEntry> ScopedGpuBlacklistEntry;

  class GpuBlacklistEntry : public base::RefCounted<GpuBlacklistEntry> {
   public:
    // Constructs GpuBlacklistEntry from DictionaryValue loaded from json.
    // Top-level entry must have an id number.  Others are exceptions.
    static ScopedGpuBlacklistEntry GetGpuBlacklistEntryFromValue(
        const base::DictionaryValue* value, bool top_level);

    // Determines if a given os/gc/driver is included in the Entry set.
    bool Contains(OsType os_type,
                  const Version& os_version,
                  const content::GPUInfo& gpu_info) const;

    // Returns the OsType.
    OsType GetOsType() const;

    // Returns the entry's unique id.  0 is reserved.
    uint32 id() const;

    // Returns whether the entry is disabled.
    bool disabled() const;

    // Returns the description of the entry
    const std::string& description() const { return description_; }

    // Returns a list of Chromium and Webkit bugs applicable to this entry
    const std::vector<int>& cr_bugs() const { return cr_bugs_; }
    const std::vector<int>& webkit_bugs() const { return webkit_bugs_; }

    // Returns the GpuFeatureType.
    content::GpuFeatureType GetGpuFeatureType() const;

    // Returns true if an unknown field is encountered.
    bool contains_unknown_fields() const {
      return contains_unknown_fields_;
    }
    // Returns true if an unknown blacklist feature is encountered.
    bool contains_unknown_features() const {
      return contains_unknown_features_;
    }

   private:
    friend class base::RefCounted<GpuBlacklistEntry>;

    enum MultiGpuStyle {
      kMultiGpuStyleOptimus,
      kMultiGpuStyleAMDSwitchable,
      kMultiGpuStyleNone
    };

    enum MultiGpuCategory {
      kMultiGpuCategoryPrimary,
      kMultiGpuCategorySecondary,
      kMultiGpuCategoryAny,
      kMultiGpuCategoryNone
    };

    GpuBlacklistEntry();
    ~GpuBlacklistEntry() { }

    bool SetId(uint32 id);

    void SetDisabled(bool disabled);

    bool SetOsInfo(const std::string& os,
                   const std::string& version_op,
                   const std::string& version_string,
                   const std::string& version_string2);

    bool SetVendorId(const std::string& vendor_id_string);

    bool AddDeviceId(const std::string& device_id_string);

    bool SetMultiGpuStyle(const std::string& multi_gpu_style_string);

    bool SetMultiGpuCategory(const std::string& multi_gpu_category_string);

    bool SetDriverVendorInfo(const std::string& vendor_op,
                             const std::string& vendor_value);

    bool SetDriverVersionInfo(const std::string& version_op,
                              const std::string& version_style,
                              const std::string& version_string,
                              const std::string& version_string2);

    bool SetDriverDateInfo(const std::string& date_op,
                           const std::string& date_string,
                           const std::string& date_string2);

    bool SetGLVendorInfo(const std::string& vendor_op,
                         const std::string& vendor_value);

    bool SetGLRendererInfo(const std::string& renderer_op,
                           const std::string& renderer_value);

    bool SetPerfGraphicsInfo(const std::string& op,
                             const std::string& float_string,
                             const std::string& float_string2);

    bool SetPerfGamingInfo(const std::string& op,
                           const std::string& float_string,
                           const std::string& float_string2);

    bool SetPerfOverallInfo(const std::string& op,
                            const std::string& float_string,
                            const std::string& float_string2);

    bool SetBlacklistedFeatures(
        const std::vector<std::string>& blacklisted_features);

    void AddException(ScopedGpuBlacklistEntry exception);

    static MultiGpuStyle StringToMultiGpuStyle(const std::string& style);

    static MultiGpuCategory StringToMultiGpuCategory(
        const std::string& category);

    uint32 id_;
    bool disabled_;
    std::string description_;
    std::vector<int> cr_bugs_;
    std::vector<int> webkit_bugs_;
    scoped_ptr<OsInfo> os_info_;
    uint32 vendor_id_;
    std::vector<uint32> device_id_list_;
    MultiGpuStyle multi_gpu_style_;
    MultiGpuCategory multi_gpu_category_;
    scoped_ptr<StringInfo> driver_vendor_info_;
    scoped_ptr<VersionInfo> driver_version_info_;
    scoped_ptr<VersionInfo> driver_date_info_;
    scoped_ptr<StringInfo> gl_vendor_info_;
    scoped_ptr<StringInfo> gl_renderer_info_;
    scoped_ptr<FloatInfo> perf_graphics_info_;
    scoped_ptr<FloatInfo> perf_gaming_info_;
    scoped_ptr<FloatInfo> perf_overall_info_;
    content::GpuFeatureType feature_type_;
    std::vector<ScopedGpuBlacklistEntry> exceptions_;
    bool contains_unknown_fields_;
    bool contains_unknown_features_;
  };

  // Gets the current OS type.
  static OsType GetOsType();

  GpuBlacklist();

  bool LoadGpuBlacklist(const std::string& browser_version_string,
                        const std::string& json_context,
                        OsFilter os_filter);

  bool LoadGpuBlacklist(const base::DictionaryValue& parsed_json,
                        OsFilter os_filter);

  void Clear();

  // Check if the entry is supported by the current version of browser.
  // By default, if there is no browser version information in the entry,
  // return kSupported;
  BrowserVersionSupport IsEntrySupportedByCurrentBrowserVersion(
      const base::DictionaryValue* value);

  // GpuDataManager::Observer implementation.
  virtual void OnGpuInfoUpdate() OVERRIDE;

  // Returns the number of entries.  This is only for tests.
  size_t num_entries() const;

  // Check if any entries contain unknown fields.  This is only for tests.
  bool contains_unknown_fields() const { return contains_unknown_fields_; }

  static NumericOp StringToNumericOp(const std::string& op);

  scoped_ptr<Version> version_;
  std::vector<ScopedGpuBlacklistEntry> blacklist_;

  scoped_ptr<Version> browser_version_;

  // This records all the blacklist entries that are appliable to the current
  // user machine.  It is updated everytime DetermineGpuFeatureType() is
  // called and is used later by GetGpuFeatureTypeEntries().
  std::vector<ScopedGpuBlacklistEntry> active_entries_;

  uint32 max_entry_id_;

  bool contains_unknown_fields_;

  DISALLOW_COPY_AND_ASSIGN(GpuBlacklist);
};

#endif  // CHROME_BROWSER_GPU_BLACKLIST_H_
