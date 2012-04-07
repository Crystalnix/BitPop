# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'chromium_code': 1,
    # These are defined here because we need to build this library twice. Once
    # with extra parameter checking. Once with no parameter checking to be 100%
    # OpenGL ES 2.0 compliant for the conformance tests.
    'gles2_c_lib_source_files': [
      'command_buffer/client/gles2_c_lib.cc',
      'command_buffer/client/gles2_c_lib_autogen.h',
      'command_buffer/client/gles2_c_lib_export.h',
      'command_buffer/client/gles2_lib.h',
      'command_buffer/client/gles2_lib.cc',
    ],
    # These are defined here because we need to build this library twice. Once
    # with without support for client side arrays and once with for pepper and
    # the OpenGL ES 2.0 compliant for the conformance tests.
    'gles2_implementation_source_files': [
      'command_buffer/client/gles2_implementation_autogen.h',
      'command_buffer/client/gles2_implementation.cc',
      'command_buffer/client/gles2_implementation.h',
      'command_buffer/client/program_info_manager.cc',
      'command_buffer/client/program_info_manager.h',
    ]
  },
  'targets': [
    {
      'target_name': 'command_buffer_common',
      'type': 'static_library',
      'include_dirs': [
        '<(DEPTH)/third_party/khronos',
      ],
      'all_dependent_settings': {
        'include_dirs': [
          '<(DEPTH)/third_party/khronos',
        ],
      },
      'dependencies': [
        '../base/base.gyp:base',
        'command_buffer/command_buffer.gyp:gles2_utils',
      ],
      'export_dependent_settings': [
        '../base/base.gyp:base',
      ],
      'sources': [
        'command_buffer/common/bitfield_helpers.h',
        'command_buffer/common/buffer.h',
        'command_buffer/common/cmd_buffer_common.h',
        'command_buffer/common/cmd_buffer_common.cc',
        'command_buffer/common/command_buffer.h',
        'command_buffer/common/compiler_specific.h',
        'command_buffer/common/constants.h',
        'command_buffer/common/gles2_cmd_ids_autogen.h',
        'command_buffer/common/gles2_cmd_ids.h',
        'command_buffer/common/gles2_cmd_format_autogen.h',
        'command_buffer/common/gles2_cmd_format.cc',
        'command_buffer/common/gles2_cmd_format.h',
        'command_buffer/common/id_allocator.cc',
        'command_buffer/common/id_allocator.h',
        'command_buffer/common/thread_local.h',
        'command_buffer/common/types.h',
      ],
    },
    {
      # Library helps make GLES2 command buffers.
      'target_name': 'gles2_cmd_helper',
      'type': 'static_library',
      'dependencies': [
        'command_buffer_client',
      ],
      'sources': [
        'command_buffer/client/gles2_cmd_helper.cc',
        'command_buffer/client/gles2_cmd_helper.h',
        'command_buffer/client/gles2_cmd_helper_autogen.h',
      ],
    },
    {
      # Library emulates GLES2 using command_buffers.
      'target_name': 'gles2_implementation',
      'type': 'static_library',
      'dependencies': [
        '../base/base.gyp:base',
        '../ui/gfx/gl/gl.gyp:gl',
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
      'target_name': 'gles2_implementation_client_side_arrays',
      'type': 'static_library',
      'defines': [
        'GLES2_SUPPORT_CLIENT_SIDE_ARRAYS=1',
      ],
      'dependencies': [
        '../base/base.gyp:base',
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
      'type': 'static_library',
      'defines': [
        'GLES2_SUPPORT_CLIENT_SIDE_ARRAYS=1',
        'GLES2_CONFORMANCE_TESTS=1',
      ],
      'dependencies': [
        '../base/base.gyp:base',
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
        'gles2_implementation_client_side_arrays_no_check',
      ],
      'sources': [
        '<@(gles2_c_lib_source_files)',
      ],
    },
    {
      'target_name': 'command_buffer_client',
      'type': 'static_library',
      'include_dirs': [
        '..',
        '<(DEPTH)/third_party/khronos',
      ],
      'all_dependent_settings': {
        'include_dirs': [
          '..',
          '<(DEPTH)/third_party/khronos',
        ],
      },
      'dependencies': [
        'command_buffer_common',
      ],
      'sources': [
        'command_buffer/client/cmd_buffer_helper.cc',
        'command_buffer/client/cmd_buffer_helper.h',
        'command_buffer/client/fenced_allocator.cc',
        'command_buffer/client/fenced_allocator.h',
        'command_buffer/client/mapped_memory.cc',
        'command_buffer/client/mapped_memory.h',
        'command_buffer/client/ring_buffer.cc',
        'command_buffer/client/ring_buffer.h',
        'command_buffer/client/transfer_buffer.cc',
        'command_buffer/client/transfer_buffer.h',
      ],
    },
    {
      'target_name': 'command_buffer_service',
      'type': 'static_library',
      'include_dirs': [
        '..',
        '<(DEPTH)/third_party/khronos',
      ],
      'all_dependent_settings': {
        'include_dirs': [
          '..',
          '<(DEPTH)/third_party/khronos',
        ],
      },
      'dependencies': [
        'command_buffer_common',
        '../base/base.gyp:base',
        '../ui/gfx/gl/gl.gyp:gl',
        '../ui/gfx/surface/surface.gyp:surface',
        '../ui/ui.gyp:ui',
        '../third_party/angle/src/build_angle.gyp:translator_glsl',
      ],
      'sources': [
        'command_buffer/service/buffer_manager.h',
        'command_buffer/service/buffer_manager.cc',
        'command_buffer/service/framebuffer_manager.h',
        'command_buffer/service/framebuffer_manager.cc',
        'command_buffer/service/cmd_buffer_engine.h',
        'command_buffer/service/cmd_parser.cc',
        'command_buffer/service/cmd_parser.h',
        'command_buffer/service/command_buffer_service.cc',
        'command_buffer/service/command_buffer_service.h',
        'command_buffer/service/common_decoder.cc',
        'command_buffer/service/common_decoder.h',
        'command_buffer/service/context_group.h',
        'command_buffer/service/context_group.cc',
        'command_buffer/service/feature_info.h',
        'command_buffer/service/feature_info.cc',
        'command_buffer/service/gles2_cmd_decoder.h',
        'command_buffer/service/gles2_cmd_decoder_autogen.h',
        'command_buffer/service/gles2_cmd_decoder.cc',
        'command_buffer/service/gles2_cmd_validation.h',
        'command_buffer/service/gles2_cmd_validation.cc',
        'command_buffer/service/gles2_cmd_validation_autogen.h',
        'command_buffer/service/gles2_cmd_validation_implementation_autogen.h',
        'command_buffer/service/gl_utils.h',
        'command_buffer/service/gpu_scheduler.h',
        'command_buffer/service/gpu_scheduler.cc',
        'command_buffer/service/gpu_scheduler_mock.h',
        'command_buffer/service/gpu_switches.h',
        'command_buffer/service/gpu_switches.cc',
        'command_buffer/service/id_manager.h',
        'command_buffer/service/id_manager.cc',
        'command_buffer/service/mocks.h',
        'command_buffer/service/program_manager.h',
        'command_buffer/service/program_manager.cc',
        'command_buffer/service/renderbuffer_manager.h',
        'command_buffer/service/renderbuffer_manager.cc',
        'command_buffer/service/shader_manager.h',
        'command_buffer/service/shader_manager.cc',
        'command_buffer/service/shader_translator.h',
        'command_buffer/service/shader_translator.cc',
        'command_buffer/service/stream_texture.h',
        'command_buffer/service/stream_texture_manager.h',
        'command_buffer/service/texture_manager.h',
        'command_buffer/service/texture_manager.cc',
        'command_buffer/service/vertex_attrib_manager.h',
        'command_buffer/service/vertex_attrib_manager.cc',
      ],
      'conditions': [
        ['toolkit_uses_gtk == 1', {
          'dependencies': [
            '../build/linux/system.gyp:gtk',
          ],
        }],
        ['ui_compositor_image_transport==1', {
          'include_dirs': [
            '<(DEPTH)/third_party/angle/include',
          ],
        }],
      ],
    },
    {
      'target_name': 'gpu_unittests',
      'type': 'executable',
      'dependencies': [
        '../base/third_party/dynamic_annotations/dynamic_annotations.gyp:dynamic_annotations',
        '../testing/gmock.gyp:gmock',
        '../testing/gmock.gyp:gmock_main',
        '../testing/gtest.gyp:gtest',
        '../ui/gfx/gl/gl.gyp:gl',
        'command_buffer_client',
        'command_buffer_common',
        'command_buffer_service',
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
        'command_buffer/client/program_info_manager_unittest.cc',
        'command_buffer/client/ring_buffer_test.cc',
        'command_buffer/client/transfer_buffer_unittest.cc',
        'command_buffer/common/bitfield_helpers_test.cc',
        'command_buffer/common/command_buffer_mock.cc',
        'command_buffer/common/command_buffer_mock.h',
        'command_buffer/common/gles2_cmd_format_test.cc',
        'command_buffer/common/gles2_cmd_format_test_autogen.h',
        'command_buffer/common/gles2_cmd_utils_unittest.cc',
        'command_buffer/common/id_allocator_test.cc',
        'command_buffer/common/trace_event.h',
        'command_buffer/common/unittest_main.cc',
        'command_buffer/service/buffer_manager_unittest.cc',
        'command_buffer/service/cmd_parser_test.cc',
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
        'command_buffer/service/gpu_scheduler_unittest.cc',
        'command_buffer/service/id_manager_unittest.cc',
        'command_buffer/service/mocks.cc',
        'command_buffer/service/mocks.h',
        'command_buffer/service/program_manager_unittest.cc',
        'command_buffer/service/renderbuffer_manager_unittest.cc',
        'command_buffer/service/shader_manager_unittest.cc',
        'command_buffer/service/shader_translator_unittest.cc',
        'command_buffer/service/stream_texture_mock.cc',
        'command_buffer/service/stream_texture_mock.h',
        'command_buffer/service/stream_texture_manager_mock.cc',
        'command_buffer/service/stream_texture_manager_mock.h',
        'command_buffer/service/test_helper.cc',
        'command_buffer/service/test_helper.h',
        'command_buffer/service/texture_manager_unittest.cc',
        'command_buffer/service/vertex_attrib_manager_unittest.cc',
      ],
    },
    {
      'target_name': 'gpu_unittest_utils',
      'type': 'static_library',
      'dependencies': [
        '../testing/gmock.gyp:gmock',
        '../testing/gtest.gyp:gtest',
        '../ui/gfx/gl/gl.gyp:gl',
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
    {
      'target_name': 'gpu_ipc',
      'type': 'static_library',
      'dependencies': [
        'command_buffer_client',
        'gles2_c_lib',
        '../base/base.gyp:base',
      ],
      'include_dirs': [
        '..',
        '<(DEPTH)/third_party/khronos',
      ],
      'sources': [
        'ipc/gpu_command_buffer_traits.cc',
        'ipc/gpu_command_buffer_traits.h',
      ],
    },
  ],
}
