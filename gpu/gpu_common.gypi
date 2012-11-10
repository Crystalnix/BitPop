# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'targets': [
    {
      # Library emulates GLES2 using command_buffers.
      'target_name': 'gles2_implementation',
      'type': '<(component)',
      'dependencies': [
        '../base/base.gyp:base',
        '../ui/gl/gl.gyp:gl',
        'command_buffer/command_buffer.gyp:gles2_utils',
        'gles2_cmd_helper',
      ],
      'all_dependent_settings': {
        'include_dirs': [
          # For GLES2/gl2.h
          '<(DEPTH)/third_party/khronos',
        ],
      },
      'defines': [
        'GLES2_IMPL_IMPLEMENTATION',
      ],
      'sources': [
        '<@(gles2_implementation_source_files)',
      ],
    },
    {
      # Library emulates GLES2 using command_buffers.
      'target_name': 'gles2_implementation_client_side_arrays',
      'type': '<(component)',
      'defines': [
        'GLES2_IMPL_IMPLEMENTATION',
        'GLES2_SUPPORT_CLIENT_SIDE_ARRAYS=1',
      ],
      'dependencies': [
        '../base/base.gyp:base',
        '../ui/gl/gl.gyp:gl',
        'command_buffer/command_buffer.gyp:gles2_utils',
        'gles2_cmd_helper',
      ],
      'all_dependent_settings': {
        'include_dirs': [
          # For GLES2/gl2.h
          '<(DEPTH)/third_party/khronos',
        ],
      },
      'sources': [
        '<@(gles2_implementation_source_files)',
      ],
    },
    {
      # Library emulates GLES2 using command_buffers.
      'target_name': 'gles2_implementation_client_side_arrays_no_check',
      'type': '<(component)',
      'defines': [
        'GLES2_IMPL_IMPLEMENTATION',
        'GLES2_SUPPORT_CLIENT_SIDE_ARRAYS=1',
        'GLES2_CONFORMANCE_TESTS=1',
      ],
      'dependencies': [
        '../base/base.gyp:base',
        'command_buffer/command_buffer.gyp:gles2_utils',
        'gles2_cmd_helper',
      ],
      'all_dependent_settings': {
        'include_dirs': [
          # For GLES2/gl2.h
          '<(DEPTH)/third_party/khronos',
        ],
      },
      'sources': [
        '<@(gles2_implementation_source_files)',
      ],
    },
    {
      # Stub to expose gles2_implemenation in C instead of C++.
      # so GLES2 C programs can work with no changes.
      'target_name': 'gles2_c_lib',
      'type': '<(component)',
      'dependencies': [
        '../base/base.gyp:base',
        '../base/third_party/dynamic_annotations/dynamic_annotations.gyp:dynamic_annotations',
        'command_buffer/command_buffer.gyp:gles2_utils',
        'command_buffer_client',
        'gles2_implementation',
      ],
      'defines': [
        'GLES2_C_LIB_IMPLEMENTATION',
      ],
      'sources': [
        '<@(gles2_c_lib_source_files)',
      ],
    },
    {
      # Same as gles2_c_lib except with no parameter checking. Required for
      # OpenGL ES 2.0 conformance tests.
      'target_name': 'gles2_c_lib_nocheck',
      'type': '<(component)',
      'defines': [
        'GLES2_C_LIB_IMPLEMENTATION',
        'GLES2_CONFORMANCE_TESTS=1',
      ],
      'dependencies': [
        '../base/base.gyp:base',
        '../base/third_party/dynamic_annotations/dynamic_annotations.gyp:dynamic_annotations',
        'command_buffer/command_buffer.gyp:gles2_utils',
        'command_buffer_client',
        'gles2_implementation_client_side_arrays_no_check',
      ],
      'sources': [
        '<@(gles2_c_lib_source_files)',
      ],
    },
    {
      'target_name': 'gpu_unittests',
      'type': '<(gtest_target_type)',
      'dependencies': [
        '../base/base.gyp:base',
        '../base/third_party/dynamic_annotations/dynamic_annotations.gyp:dynamic_annotations',
        '../testing/gmock.gyp:gmock',
        '../testing/gtest.gyp:gtest',
        '../third_party/angle/src/build_angle.gyp:translator_glsl',
        '../ui/gl/gl.gyp:gl',
        '../ui/ui.gyp:ui',
        'command_buffer/command_buffer.gyp:gles2_utils',
        'command_buffer_client',
        'command_buffer_common',
        'command_buffer_service',
        'gpu',
        'gpu_unittest_utils',
        'gles2_implementation_client_side_arrays',
        'gles2_cmd_helper',
      ],
      'defines': [
        'GLES2_C_LIB_IMPLEMENTATION',
      ],
      'sources': [
        '<@(gles2_c_lib_source_files)',
        'command_buffer/client/client_test_helper.cc',
        'command_buffer/client/client_test_helper.h',
        'command_buffer/client/cmd_buffer_helper_test.cc',
        'command_buffer/client/fenced_allocator_test.cc',
        'command_buffer/client/gles2_implementation_unittest.cc',
        'command_buffer/client/mapped_memory_unittest.cc',
        'command_buffer/client/query_tracker_unittest.cc',
        'command_buffer/client/program_info_manager_unittest.cc',
        'command_buffer/client/ring_buffer_test.cc',
        'command_buffer/client/transfer_buffer_unittest.cc',
        'command_buffer/common/bitfield_helpers_test.cc',
        'command_buffer/common/command_buffer_mock.cc',
        'command_buffer/common/command_buffer_mock.h',
        'command_buffer/common/command_buffer_shared_test.cc',
        'command_buffer/common/gles2_cmd_format_test.cc',
        'command_buffer/common/gles2_cmd_format_test_autogen.h',
        'command_buffer/common/gles2_cmd_utils_unittest.cc',
        'command_buffer/common/id_allocator_test.cc',
        'command_buffer/common/trace_event.h',
        'command_buffer/common/unittest_main.cc',
        'command_buffer/service/buffer_manager_unittest.cc',
        'command_buffer/service/cmd_parser_test.cc',
        'command_buffer/service/command_buffer_service_unittest.cc',
        'command_buffer/service/common_decoder_unittest.cc',
        'command_buffer/service/context_group_unittest.cc',
        'command_buffer/service/feature_info_unittest.cc',
        'command_buffer/service/framebuffer_manager_unittest.cc',
        'command_buffer/service/gles2_cmd_decoder_unittest.cc',
        'command_buffer/service/gles2_cmd_decoder_unittest_1.cc',
        'command_buffer/service/gles2_cmd_decoder_unittest_1_autogen.h',
        'command_buffer/service/gles2_cmd_decoder_unittest_2.cc',
        'command_buffer/service/gles2_cmd_decoder_unittest_2_autogen.h',
        'command_buffer/service/gles2_cmd_decoder_unittest_3.cc',
        'command_buffer/service/gles2_cmd_decoder_unittest_3_autogen.h',
        'command_buffer/service/gles2_cmd_decoder_unittest_base.cc',
        'command_buffer/service/gles2_cmd_decoder_unittest_base.h',
        'command_buffer/service/gl_surface_mock.cc',
        'command_buffer/service/gl_surface_mock.h',
        'command_buffer/service/gpu_scheduler_unittest.cc',
        'command_buffer/service/id_manager_unittest.cc',
        'command_buffer/service/memory_program_cache_unittest.cc',
        'command_buffer/service/mocks.cc',
        'command_buffer/service/mocks.h',
        'command_buffer/service/program_manager_unittest.cc',
        'command_buffer/service/query_manager_unittest.cc',
        'command_buffer/service/renderbuffer_manager_unittest.cc',
        'command_buffer/service/program_cache_lru_helper_unittest.cc',
        'command_buffer/service/program_cache_unittest.cc',
        'command_buffer/service/shader_manager_unittest.cc',
        'command_buffer/service/shader_translator_unittest.cc',
        'command_buffer/service/stream_texture_mock.cc',
        'command_buffer/service/stream_texture_mock.h',
        'command_buffer/service/stream_texture_manager_mock.cc',
        'command_buffer/service/stream_texture_manager_mock.h',
        'command_buffer/service/test_helper.cc',
        'command_buffer/service/test_helper.h',
        'command_buffer/service/texture_manager_unittest.cc',
        'command_buffer/service/transfer_buffer_manager_unittest.cc',
        'command_buffer/service/vertex_attrib_manager_unittest.cc',
      ],
      'conditions': [
        ['OS == "android" and gtest_target_type == "shared_library"', {
          'dependencies': [
            '../testing/android/native_test.gyp:native_test_native_code',
          ],
        }],
      ],
    },
    {
      'target_name': 'gl_tests',
      'type': 'executable',
      'dependencies': [
        '../base/base.gyp:base',
        '../base/third_party/dynamic_annotations/dynamic_annotations.gyp:dynamic_annotations',
        '../testing/gmock.gyp:gmock',
        '../testing/gtest.gyp:gtest',
        '../third_party/angle/src/build_angle.gyp:translator_glsl',
        '../ui/ui.gyp:ui',
        'command_buffer/command_buffer.gyp:gles2_utils',
        'command_buffer_client',
        'command_buffer_common',
        'command_buffer_service',
        'gpu',
        'gpu_unittest_utils',
        'gles2_implementation_client_side_arrays',
        'gles2_cmd_helper',
        #'gl_unittests',
      ],
      'defines': [
        'GLES2_C_LIB_IMPLEMENTATION',
        'GL_GLEXT_PROTOTYPES',
      ],
      'sources': [
        '<@(gles2_c_lib_source_files)',
        'command_buffer/tests/gl_bind_uniform_location_unittest.cc',
        'command_buffer/tests/gl_copy_texture_CHROMIUM_unittest.cc',
        'command_buffer/tests/gl_depth_texture_unittest.cc',
        'command_buffer/tests/gl_get_error_query_unittests.cc',
        'command_buffer/tests/gl_manager.cc',
        'command_buffer/tests/gl_manager.h',
        'command_buffer/tests/gl_pointcoord_unittest.cc',
        'command_buffer/tests/gl_tests_main.cc',
        'command_buffer/tests/gl_test_utils.cc',
        'command_buffer/tests/gl_test_utils.h',
        'command_buffer/tests/gl_texture_mailbox_unittests.cc',
        'command_buffer/tests/gl_unittests.cc',
        'command_buffer/tests/occlusion_query_unittests.cc',
      ],
    },
    {
      'target_name': 'gpu_unittest_utils',
      'type': 'static_library',
      'dependencies': [
        '../testing/gmock.gyp:gmock',
        '../testing/gtest.gyp:gtest',
        '../ui/gl/gl.gyp:gl',
      ],
      'include_dirs': [
        '..',
        '<(DEPTH)/third_party/khronos',
      ],
      'sources': [
        'command_buffer/common/gl_mock.h',
        'command_buffer/common/gl_mock.cc',
        'command_buffer/service/gles2_cmd_decoder_mock.cc',
        'command_buffer/service/gles2_cmd_decoder_mock.cc',
      ],
    },
  ],
  'conditions': [
    # Special target to wrap a gtest_target_type==shared_library
    # gpu_unittests into an android apk for execution.
    ['OS == "android" and gtest_target_type == "shared_library"', {
      'targets': [
        {
          'target_name': 'gpu_unittests_apk',
          'type': 'none',
          'dependencies': [
            '../base/base.gyp:base_java',
            'gpu_unittests',
          ],
          'variables': {
            'test_suite_name': 'gpu_unittests',
            'input_shlib_path': '<(SHARED_LIB_DIR)/<(SHARED_LIB_PREFIX)gpu_unittests<(SHARED_LIB_SUFFIX)',
            'input_jars_paths': [ '<(PRODUCT_DIR)/lib.java/chromium_base.jar', ],
          },
          'includes': [ '../build/apk_test.gypi' ],
        },
      ],
    }],
  ],
}
