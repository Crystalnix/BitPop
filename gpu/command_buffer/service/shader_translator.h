// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_COMMAND_BUFFER_SERVICE_SHADER_TRANSLATOR_H_
#define GPU_COMMAND_BUFFER_SERVICE_SHADER_TRANSLATOR_H_

#include <string>

#include "base/basictypes.h"
#include "base/hash_tables.h"
#include "base/memory/scoped_ptr.h"
#include "third_party/angle/include/GLSLANG/ShaderLang.h"

namespace gpu {
namespace gles2 {

// Translates a GLSL ES 2.0 shader to desktop GLSL shader, or just
// validates GLSL ES 2.0 shaders on a true GLSL ES implementation.
class ShaderTranslatorInterface {
 public:
  virtual ~ShaderTranslatorInterface() { }

  // Initializes the translator.
  // Must be called once before using the translator object.
  virtual bool Init(
      ShShaderType shader_type,
      ShShaderSpec shader_spec,
      const ShBuiltInResources* resources,
      bool implementation_is_glsl_es) = 0;

  // Translates the given shader source.
  // Returns true if translation is successful, false otherwise.
  virtual bool Translate(const char* shader) = 0;

  // The following functions return results from the last translation.
  // The results are NULL/empty if the translation was unsuccessful.
  // A valid info-log is always returned irrespective of whether translation
  // was successful or not.
  virtual const char* translated_shader() const = 0;
  virtual const char* info_log() const = 0;

  struct VariableInfo {
    VariableInfo()
        : type(0),
          size(0) {
    }
    VariableInfo(int _type, int _size)
        : type(_type),
          size(_size) {
    }
    int type;
    int size;
  };
  // Mapping between variable name and info.
  typedef base::hash_map<std::string, VariableInfo> VariableMap;
  virtual const VariableMap& attrib_map() const = 0;
  virtual const VariableMap& uniform_map() const = 0;
};

// Implementation of ShaderTranslatorInterface
class ShaderTranslator : public ShaderTranslatorInterface {
 public:
  ShaderTranslator();
  virtual ~ShaderTranslator();

  // Overridden from ShaderTranslatorInterface.
  virtual bool Init(
      ShShaderType shader_type,
      ShShaderSpec shader_spec,
      const ShBuiltInResources* resources,
      bool implementation_is_glsl_es);

  // Overridden from ShaderTranslatorInterface.
  virtual bool Translate(const char* shader);

  // Overridden from ShaderTranslatorInterface.
  virtual const char* translated_shader() const;
  virtual const char* info_log() const;

  // Overridden from ShaderTranslatorInterface.
  virtual const VariableMap& attrib_map() const;
  virtual const VariableMap& uniform_map() const;

 private:
  void ClearResults();

  ShHandle compiler_;
  scoped_array<char> translated_shader_;
  scoped_array<char> info_log_;
  VariableMap attrib_map_;
  VariableMap uniform_map_;
  bool implementation_is_glsl_es_;

  DISALLOW_COPY_AND_ASSIGN(ShaderTranslator);
};

}  // namespace gles2
}  // namespace gpu

#endif  // GPU_COMMAND_BUFFER_SERVICE_SHADER_TRANSLATOR_H_

