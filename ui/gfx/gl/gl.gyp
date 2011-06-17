# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'target_defaults': {
    'sources/': [
      ['exclude', '/(cocoa|gtk|win)/'],
      ['exclude', '_(cocoa|gtk|linux|mac|posix|win|x)\\.(cc|mm?)$'],
      ['exclude', '/(gtk|win|x11)_[^/]*\\.cc$'],
    ],
    'conditions': [
      ['OS=="linux" or OS=="freebsd" or OS=="openbsd"', {'sources/': [
        ['include', '/gtk/'],
        ['include', '_(gtk|linux|posix|skia|x)\\.cc$'],
        ['include', '/(gtk|x11)_[^/]*\\.cc$'],
      ]}],
      ['OS=="mac"', {'sources/': [
        ['include', '/cocoa/'],
        ['include', '_(cocoa|mac|posix)\\.(cc|mm?)$'],
      ]}, { # else: OS != "mac"
        'sources/': [
          ['exclude', '\\.mm?$'],
        ],
      }],
      ['OS=="win"',
        {'sources/': [
          ['include', '_(win)\\.cc$'],
          ['include', '/win/'],
          ['include', '/win_[^/]*\\.cc$'],
      ]}],
    ],
  },
  'targets': [
    {
      'target_name': 'gl',
      'type': '<(library)',
      'dependencies': [
        '<(DEPTH)/app/app.gyp:app_base',
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/skia/skia.gyp:skia',
        '<(DEPTH)/ui/ui.gyp:ui_gfx',
      ],
      'variables': {
        'gl_binding_output_dir': '<(SHARED_INTERMEDIATE_DIR)/ui/gfx/gl',
      },
      'include_dirs': [
        '<(DEPTH)/third_party/mesa/MesaLib/include',
        '<(gl_binding_output_dir)',
      ],
      'direct_dependent_settings': {
        'include_dirs': [
          '<(DEPTH)/third_party/mesa/MesaLib/include',
          '<(gl_binding_output_dir)',
        ],
      },
     'sources': [
        'gl_bindings.h',
        'gl_bindings_skia_in_process.cc',
        'gl_bindings_skia_in_process.h',
        'gl_context.cc',
        'gl_context.h',
        'gl_context_linux.cc',
        'gl_context_mac.cc',
        'gl_context_osmesa.cc',
        'gl_context_osmesa.h',
        'gl_context_stub.cc',
        'gl_context_stub.h',
        'gl_context_win.cc',
        'gl_implementation.cc',
        'gl_implementation.h',
        'gl_implementation_linux.cc',
        'gl_implementation_mac.cc',
        'gl_implementation_win.cc',
        'gl_interface.cc',
        'gl_interface.h',
        'gl_surface.cc',
        'gl_surface.h',
        'gl_switches.cc',
        'gl_switches.h',
        '<(gl_binding_output_dir)/gl_bindings_autogen_gl.cc',
        '<(gl_binding_output_dir)/gl_bindings_autogen_gl.h',
        '<(gl_binding_output_dir)/gl_bindings_autogen_mock.cc',
        '<(gl_binding_output_dir)/gl_bindings_autogen_osmesa.cc',
        '<(gl_binding_output_dir)/gl_bindings_autogen_osmesa.h',
      ],
      # hard_dependency is necessary for this target because it has actions
      # that generate header files included by dependent targets. The header
      # files must be generated before the dependents are compiled. The usual
      # semantics are to allow the two targets to build concurrently.
      'hard_dependency': 1,
      'actions': [
        {
          'action_name': 'generate_gl_bindings',
          'inputs': [
            'generate_bindings.py',
          ],
          'outputs': [
            '<(gl_binding_output_dir)/gl_bindings_autogen_egl.cc',
            '<(gl_binding_output_dir)/gl_bindings_autogen_egl.h',
            '<(gl_binding_output_dir)/gl_bindings_autogen_gl.cc',
            '<(gl_binding_output_dir)/gl_bindings_autogen_gl.h',
            '<(gl_binding_output_dir)/gl_bindings_autogen_glx.cc',
            '<(gl_binding_output_dir)/gl_bindings_autogen_glx.h',
            '<(gl_binding_output_dir)/gl_bindings_autogen_mock.cc',
            '<(gl_binding_output_dir)/gl_bindings_autogen_osmesa.cc',
            '<(gl_binding_output_dir)/gl_bindings_autogen_osmesa.h',
            '<(gl_binding_output_dir)/gl_bindings_autogen_wgl.cc',
            '<(gl_binding_output_dir)/gl_bindings_autogen_wgl.h',
          ],
          'action': [
            'python',
            'generate_bindings.py',
            '<(gl_binding_output_dir)',
          ],
        },
      ],
      'conditions': [
        ['OS=="linux" or OS=="freebsd" or OS=="openbsd" or OS=="win"', {
          'sources': [
            'egl_util.cc',
            'egl_util.h',
            'gl_context_egl.cc',
            'gl_context_egl.h',
            'gl_surface_egl.cc',
            'gl_surface_egl.h',
            '<(gl_binding_output_dir)/gl_bindings_autogen_egl.cc',
            '<(gl_binding_output_dir)/gl_bindings_autogen_egl.h',
          ],
          'include_dirs': [
            '<(DEPTH)/third_party/angle/include',
          ],
        }],
        ['OS=="linux" or OS=="freebsd" or OS=="openbsd"', {
          'sources': [
            '<(gl_binding_output_dir)/gl_bindings_autogen_glx.cc',
            '<(gl_binding_output_dir)/gl_bindings_autogen_glx.h',
          ],
          'all_dependent_settings': {
            'defines': [
              'GL_GLEXT_PROTOTYPES',
            ],
          },
        }],
        ['OS=="win"', {
          'sources': [
            'gl_context_wgl.cc',
            'gl_context_wgl.h',
            'gl_surface_wgl.cc',
            'gl_surface_wgl.h',
            '<(gl_binding_output_dir)/gl_bindings_autogen_wgl.cc',
            '<(gl_binding_output_dir)/gl_bindings_autogen_wgl.h',
          ],
        }],
        ['OS=="mac"', {
          'link_settings': {
            'libraries': [
              '$(SDKROOT)/System/Library/Frameworks/OpenGL.framework',
            ],
          },
        }],
      ],
    },
  ],
}