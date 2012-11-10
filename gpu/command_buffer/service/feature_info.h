// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_COMMAND_BUFFER_SERVICE_FEATURE_INFO_H_
#define GPU_COMMAND_BUFFER_SERVICE_FEATURE_INFO_H_

#include <string>
#include "base/hash_tables.h"
#include "base/memory/ref_counted.h"
#include "gpu/command_buffer/service/gles2_cmd_decoder.h"
#include "gpu/command_buffer/service/gles2_cmd_validation.h"
#include "gpu/gpu_export.h"

namespace gpu {
namespace gles2 {

// FeatureInfo records the features that are available for a ContextGroup.
class GPU_EXPORT FeatureInfo : public base::RefCounted<FeatureInfo> {
 public:
  typedef scoped_refptr<FeatureInfo> Ref;

  struct FeatureFlags {
    FeatureFlags()
        : chromium_framebuffer_multisample(false),
          oes_standard_derivatives(false),
          oes_egl_image_external(false),
          npot_ok(false),
          enable_texture_float_linear(false),
          enable_texture_half_float_linear(false),
          chromium_webglsl(false),
          chromium_stream_texture(false),
          angle_translated_shader_source(false),
          angle_pack_reverse_row_order(false),
          arb_texture_rectangle(false),
          angle_instanced_arrays(false),
          occlusion_query_boolean(false),
          use_arb_occlusion_query2_for_occlusion_query_boolean(false),
          use_arb_occlusion_query_for_occlusion_query_boolean(false),
          disable_workarounds(false),
          is_intel(false),
          is_nvidia(false),
          is_amd(false) {
    }

    bool chromium_framebuffer_multisample;
    bool oes_standard_derivatives;
    bool oes_egl_image_external;
    bool npot_ok;
    bool enable_texture_float_linear;
    bool enable_texture_half_float_linear;
    bool chromium_webglsl;
    bool chromium_stream_texture;
    bool angle_translated_shader_source;
    bool angle_pack_reverse_row_order;
    bool arb_texture_rectangle;
    bool angle_instanced_arrays;
    bool occlusion_query_boolean;
    bool use_arb_occlusion_query2_for_occlusion_query_boolean;
    bool use_arb_occlusion_query_for_occlusion_query_boolean;
    bool disable_workarounds;
    bool is_intel;
    bool is_nvidia;
    bool is_amd;
  };

  FeatureInfo();

  // If allowed features = NULL or "*", all features are allowed. Otherwise
  // only features that match the strings in allowed_features are allowed.
  bool Initialize(const char* allowed_features);
  bool Initialize(const DisallowedFeatures& disallowed_features,
                  const char* allowed_features);

  // Turns on certain features if they can be turned on. NULL turns on
  // all available features.
  void AddFeatures(const char* desired_features);

  const Validators* validators() const {
    return &validators_;
  }

  const ValueValidator<GLenum>& GetTextureFormatValidator(GLenum format) {
    return texture_format_validators_[format];
  }

  const std::string& extensions() const {
    return extensions_;
  }

  const FeatureFlags& feature_flags() const {
    return feature_flags_;
  }

 private:
  friend class base::RefCounted<FeatureInfo>;

  typedef base::hash_map<GLenum, ValueValidator<GLenum> > ValidatorMap;
  ValidatorMap texture_format_validators_;

  ~FeatureInfo();

  void AddExtensionString(const std::string& str);

  Validators validators_;

  DisallowedFeatures disallowed_features_;

  // The extensions string returned by glGetString(GL_EXTENSIONS);
  std::string extensions_;

  // Flags for some features
  FeatureFlags feature_flags_;

  DISALLOW_COPY_AND_ASSIGN(FeatureInfo);
};

}  // namespace gles2
}  // namespace gpu

#endif  // GPU_COMMAND_BUFFER_SERVICE_FEATURE_INFO_H_


