// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/shader_translator.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace gpu {
namespace gles2 {

class ShaderTranslatorTest : public testing::Test {
 public:
  ShaderTranslatorTest() {
  }

  ~ShaderTranslatorTest() {
  }

 protected:
  virtual void SetUp() {
    ShBuiltInResources resources;
    ShInitBuiltInResources(&resources);

    ASSERT_TRUE(vertex_translator_.Init(
        SH_VERTEX_SHADER, SH_GLES2_SPEC, &resources, false));
    ASSERT_TRUE(fragment_translator_.Init(
        SH_FRAGMENT_SHADER, SH_GLES2_SPEC, &resources, false));
    // Post-init the results must be empty.
    // Vertex translator results.
    EXPECT_TRUE(vertex_translator_.translated_shader() == NULL);
    EXPECT_TRUE(vertex_translator_.info_log() == NULL);
    EXPECT_TRUE(vertex_translator_.attrib_map().empty());
    EXPECT_TRUE(vertex_translator_.uniform_map().empty());
    // Fragment translator results.
    EXPECT_TRUE(fragment_translator_.translated_shader() == NULL);
    EXPECT_TRUE(fragment_translator_.info_log() == NULL);
    EXPECT_TRUE(fragment_translator_.attrib_map().empty());
    EXPECT_TRUE(fragment_translator_.uniform_map().empty());
  }
  virtual void TearDown() {}

  ShaderTranslator vertex_translator_;
  ShaderTranslator fragment_translator_;
};

TEST_F(ShaderTranslatorTest, ValidVertexShader) {
  const char* shader =
      "void main() {\n"
      "  gl_Position = vec4(1.0);\n"
      "}";

  // A valid shader should be successfully translated.
  EXPECT_TRUE(vertex_translator_.Translate(shader));
  // Info log must be NULL.
  EXPECT_TRUE(vertex_translator_.info_log() == NULL);
  // Translated shader must be valid and non-empty.
  EXPECT_TRUE(vertex_translator_.translated_shader() != NULL);
  EXPECT_GT(strlen(vertex_translator_.translated_shader()), 0u);
  // There should be no attributes or uniforms.
  EXPECT_TRUE(vertex_translator_.attrib_map().empty());
  EXPECT_TRUE(vertex_translator_.uniform_map().empty());
}

TEST_F(ShaderTranslatorTest, InvalidVertexShader) {
  const char* shader = "foo-bar";

  // An invalid shader should fail.
  EXPECT_FALSE(vertex_translator_.Translate(shader));
  // Info log must be valid and non-empty.
  EXPECT_TRUE(vertex_translator_.info_log() != NULL);
  EXPECT_GT(strlen(vertex_translator_.info_log()), 0u);
  // Translated shader must be NULL.
  EXPECT_TRUE(vertex_translator_.translated_shader() == NULL);
  // There should be no attributes or uniforms.
  EXPECT_TRUE(vertex_translator_.attrib_map().empty());
  EXPECT_TRUE(vertex_translator_.uniform_map().empty());
}

TEST_F(ShaderTranslatorTest, ValidFragmentShader) {
  const char* shader =
      "void main() {\n"
      "  gl_FragColor = vec4(1.0);\n"
      "}";

  // A valid shader should be successfully translated.
  EXPECT_TRUE(fragment_translator_.Translate(shader));
  // Info log must be NULL.
  EXPECT_TRUE(fragment_translator_.info_log() == NULL);
  // Translated shader must be valid and non-empty.
  EXPECT_TRUE(fragment_translator_.translated_shader() != NULL);
  EXPECT_GT(strlen(fragment_translator_.translated_shader()), 0u);
  // There should be no attributes or uniforms.
  EXPECT_TRUE(fragment_translator_.attrib_map().empty());
  EXPECT_TRUE(fragment_translator_.uniform_map().empty());
}

TEST_F(ShaderTranslatorTest, InvalidFragmentShader) {
  const char* shader = "foo-bar";

  // An invalid shader should fail.
  EXPECT_FALSE(fragment_translator_.Translate(shader));
  // Info log must be valid and non-empty.
  EXPECT_TRUE(fragment_translator_.info_log() != NULL);
  EXPECT_GT(strlen(fragment_translator_.info_log()), 0u);
  // Translated shader must be NULL.
  EXPECT_TRUE(fragment_translator_.translated_shader() == NULL);
  // There should be no attributes or uniforms.
  EXPECT_TRUE(fragment_translator_.attrib_map().empty());
  EXPECT_TRUE(fragment_translator_.uniform_map().empty());
}

TEST_F(ShaderTranslatorTest, GetAttributes) {
  const char* shader =
      "attribute vec4 vPosition;\n"
      "void main() {\n"
      "  gl_Position = vPosition;\n"
      "}";

  EXPECT_TRUE(vertex_translator_.Translate(shader));
  // Info log must be NULL.
  EXPECT_TRUE(vertex_translator_.info_log() == NULL);
  // Translated shader must be valid and non-empty.
  EXPECT_TRUE(vertex_translator_.translated_shader() != NULL);
  EXPECT_GT(strlen(vertex_translator_.translated_shader()), 0u);
  // There should be no uniforms.
  EXPECT_TRUE(vertex_translator_.uniform_map().empty());
  // There should be one attribute with following characteristics:
  // name:vPosition type:SH_FLOAT_VEC4 size:1.
  const ShaderTranslator::VariableMap& attrib_map =
      vertex_translator_.attrib_map();
  EXPECT_EQ(1u, attrib_map.size());
  ShaderTranslator::VariableMap::const_iterator iter =
      attrib_map.find("vPosition");
  EXPECT_TRUE(iter != attrib_map.end());
  EXPECT_EQ(SH_FLOAT_VEC4, iter->second.type);
  EXPECT_EQ(1, iter->second.size);
}

TEST_F(ShaderTranslatorTest, GetUniforms) {
  const char* shader =
      "precision mediump float;\n"
      "struct Foo {\n"
      "  vec4 color[1];\n"
      "};\n"
      "struct Bar {\n"
      "  Foo foo;\n"
      "};\n"
      "uniform Bar bar[2];\n"
      "void main() {\n"
      "  gl_FragColor = bar[0].foo.color[0] + bar[1].foo.color[0];\n"
      "}";

  EXPECT_TRUE(fragment_translator_.Translate(shader));
  // Info log must be NULL.
  EXPECT_TRUE(fragment_translator_.info_log() == NULL);
  // Translated shader must be valid and non-empty.
  EXPECT_TRUE(fragment_translator_.translated_shader() != NULL);
  EXPECT_GT(strlen(fragment_translator_.translated_shader()), 0u);
  // There should be no attributes.
  EXPECT_TRUE(fragment_translator_.attrib_map().empty());
  // There should be two uniforms with following characteristics:
  // 1. name:bar[0].foo.color[0] type:SH_FLOAT_VEC4 size:1
  // 2. name:bar[1].foo.color[0] type:SH_FLOAT_VEC4 size:1
  const ShaderTranslator::VariableMap& uniform_map =
      fragment_translator_.uniform_map();
  EXPECT_EQ(2u, uniform_map.size());
  // First uniform.
  ShaderTranslator::VariableMap::const_iterator iter =
      uniform_map.find("bar[0].foo.color[0]");
  EXPECT_TRUE(iter != uniform_map.end());
  EXPECT_EQ(SH_FLOAT_VEC4, iter->second.type);
  EXPECT_EQ(1, iter->second.size);
  // Second uniform.
  iter = uniform_map.find("bar[1].foo.color[0]");
  EXPECT_TRUE(iter != uniform_map.end());
  EXPECT_EQ(SH_FLOAT_VEC4, iter->second.type);
  EXPECT_EQ(1, iter->second.size);
}

}  // namespace gles2
}  // namespace gpu

