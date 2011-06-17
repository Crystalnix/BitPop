{
  'variables': {
    'version_py': '../../chrome/tools/build/version.py',
    'version_path': '../../chrome/VERSION',
    'lastchange_path': '<(SHARED_INTERMEDIATE_DIR)/build/LASTCHANGE',
    # 'branding_dir' is set in the 'conditions' section at the bottom.
    'msvs_use_common_release': 0,
    'msvs_use_common_linker_extras': 0,
  },
  'conditions': [
    ['OS=="win"', {
      'target_defaults': {
        'dependencies': [
          '../chrome.gyp:chrome',
          '../chrome.gyp:chrome_nacl_win64',
          '../chrome.gyp:chrome_dll',
          '../chrome.gyp:default_extensions',
          '../chrome.gyp:setup',
        ],
        'include_dirs': [
          '../..',
          '<(PRODUCT_DIR)',
          '<(INTERMEDIATE_DIR)',
          '<(SHARED_INTERMEDIATE_DIR)/chrome',
        ],
        'sources': [
          'mini_installer/appid.h',
          'mini_installer/decompress.cc',
          'mini_installer/decompress.h',
          'mini_installer/mini_installer.cc',
          'mini_installer/mini_installer.h',
          'mini_installer/mini_installer.ico',
          'mini_installer/mini_installer.rc',
          'mini_installer/mini_installer_exe_version.rc.version',
          'mini_installer/mini_installer_resource.h',
          'mini_installer/mini_string.cc',
          'mini_installer/mini_string.h',
          'mini_installer/pe_resource.cc',
          'mini_installer/pe_resource.h',
        ],
        'msvs_settings': {
          'VCCLCompilerTool': {
            'EnableIntrinsicFunctions': 'true',
            'BufferSecurityCheck': 'false',
            'BasicRuntimeChecks': '0',
            'ExceptionHandling': '0',
          },
          'VCLinkerTool': {
            'RandomizedBaseAddress': '1',
            'DataExecutionPrevention': '0',
            'AdditionalLibraryDirectories': [
              '<(DEPTH)/third_party/platformsdk_win7/files/Lib',
              '<(PRODUCT_DIR)/lib'
            ],
            'DelayLoadDLLs': [],
            'EntryPointSymbol': 'MainEntryPoint',
            'GenerateMapFile': 'true',
            'IgnoreAllDefaultLibraries': 'true',
            'OptimizeForWindows98': '1',
            'SubSystem': '2',     # Set /SUBSYSTEM:WINDOWS
            'AdditionalDependencies': [
              'shlwapi.lib',
              'setupapi.lib',
            ],
            'conditions': [
              ['MSVS_VERSION=="2005e"', {
                'AdditionalDependencies': [ # Must explicitly link in VC2005E
                  'advapi32.lib',
                  'shell32.lib',
                ],
              }],
            ],
          },
          'VCManifestTool': {
            'AdditionalManifestFiles': [
              '$(ProjectDir)\\mini_installer\\mini_installer.exe.manifest',
            ],
          },
        },
        'configurations': {
          'Debug_Base': {
            'msvs_settings': {
              'VCCLCompilerTool': {
                'BasicRuntimeChecks': '0',
                'BufferSecurityCheck': 'false',
                'ExceptionHandling': '0',
              },
              'VCLinkerTool': {
                'SubSystem': '2',     # Set /SUBSYSTEM:WINDOWS
                'AdditionalOptions': [
                  '/safeseh:no',
                  '/dynamicbase:no',
                  '/ignore:4199',
                  '/ignore:4221',
                  '/nxcompat',
                ],
              },
            },
          },
          'Release_Base': {
            'includes': ['../../build/internal/release_defaults.gypi'],
            'msvs_settings': {
              'VCCLCompilerTool': {
                'EnableIntrinsicFunctions': 'true',
                'BasicRuntimeChecks': '0',
                'BufferSecurityCheck': 'false',
                'ExceptionHandling': '0',
              },
              'VCLinkerTool': {
                'SubSystem': '2',     # Set /SUBSYSTEM:WINDOWS
                'AdditionalOptions': [
                  '/SAFESEH:NO',
                  '/NXCOMPAT',
                  '/DYNAMICBASE:NO',
                  '/FIXED',
                ],
              },
            },
          },
        },
        'rules': [
          {
            'rule_name': 'mini_installer_version',
            'extension': 'version',
            'variables': {
              'template_input_path': 'mini_installer/mini_installer_exe_version.rc.version',
            },
            'inputs': [
              '<(template_input_path)',
              '<(version_path)',
              '<(lastchange_path)',
              '<(branding_dir)/BRANDING',
            ],
            'outputs': [
              '<(INTERMEDIATE_DIR)/mini_installer_exe_version.rc',
            ],
            'action': [
              'python', '<(version_py)',
              '-f', '<(version_path)',
              '-f', '<(lastchange_path)',
              '-f', '<(branding_dir)/BRANDING',
              '<(template_input_path)',
              '<@(_outputs)',
            ],
            'process_outputs_as_sources': 1,
            'message': 'Generating version information'
          },
        ],
        # TODO(mark):  <(branding_dir) should be defined by the
        # global condition block at the bottom of the file, but
        # this doesn't work due to the following issue:
        #
        #   http://code.google.com/p/gyp/issues/detail?id=22
        #
        # Remove this block once the above issue is fixed.
        'conditions': [
          [ 'branding == "Chrome"', {
            'variables': {
               'branding_dir': '../app/theme/google_chrome',
            },
          }, { # else branding!="Chrome"
            'variables': {
               'branding_dir': '../app/theme/chromium',
            },
          }],
        ],
      },
      'targets': [
        {
          'target_name': 'mini_installer',
          'type': 'executable',
          'msvs_guid': '24A5AC7C-280B-4899-9153-6BA570A081E7',
          'sources': [
            'mini_installer/chrome.release',
            'mini_installer/chrome_appid.cc',
          ],
          'rules': [
            {
              'rule_name': 'installer_archive',
              'extension': 'release',
              'variables': {
                'create_installer_archive_py_path':
                  '../tools/build/win/create_installer_archive.py',
              },
              'inputs': [
                '<(create_installer_archive_py_path)',
                '<(PRODUCT_DIR)/chrome.exe',
                '<(PRODUCT_DIR)/chrome.dll',
                '<(PRODUCT_DIR)/nacl64.exe',
                '<(PRODUCT_DIR)/nacl64.dll',
                '<(PRODUCT_DIR)/ppGoogleNaClPluginChrome.dll',
                '<(PRODUCT_DIR)/locales/en-US.dll',
                '<(PRODUCT_DIR)/icudt.dll',
              ],
              'outputs': [
                'xxx.out',
                '<(PRODUCT_DIR)/<(RULE_INPUT_NAME).7z',
                '<(PRODUCT_DIR)/<(RULE_INPUT_NAME).packed.7z',
                '<(PRODUCT_DIR)/setup.ex_',
                '<(PRODUCT_DIR)/packed_files.txt',
              ],
              'action': [
                'python',
                '<(create_installer_archive_py_path)',
                '--output_dir=<(PRODUCT_DIR)',
                '--input_file=<(RULE_INPUT_PATH)',
                # TODO(sgk):  may just use environment variables
                #'--distribution=$(CHROMIUM_BUILD)',
                '--distribution=_google_chrome',
                # Optional arguments to generate diff installer
                #'--last_chrome_installer=C:/Temp/base',
                #'--setup_exe_format=DIFF',
                #'--diff_algorithm=COURGETTE',
              ],
              'message': 'Create installer archive'
            },
          ],
        },
      ],
    }],
    [ 'branding == "Chrome"', {
      'variables': {
         'branding_dir': '../app/theme/google_chrome',
      },
    }, { # else branding!="Chrome"
      'variables': {
         'branding_dir': '../app/theme/chromium',
      },
    }],
  ],
}

# Local Variables:
# tab-width:2
# indent-tabs-mode:nil
# End:
# vim: set expandtab tabstop=2 shiftwidth=2:
