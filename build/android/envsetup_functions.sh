#!/bin/bash

# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Defines functions for envsetup.sh which sets up environment for building
# Chromium on Android.  The build can be either use the Android NDK/SDK or
# android source tree.  Each has a unique init function which calls functions
# prefixed with "common_" that is common for both environment setups.

################################################################################
# Check to make sure the toolchain exists for the NDK version.
################################################################################
common_check_toolchain() {
  if [[ ! -d "${ANDROID_TOOLCHAIN}" ]]; then
    echo "Can not find Android toolchain in ${ANDROID_TOOLCHAIN}." >& 2
    echo "The NDK version might be wrong." >& 2
    return 1
  fi
}

################################################################################
# Exports environment variables common to both sdk and non-sdk build (e.g. PATH)
# based on CHROME_SRC and ANDROID_TOOLCHAIN, along with DEFINES for GYP_DEFINES.
################################################################################
common_vars_defines() {

  # Set toolchain path according to product architecture.
  toolchain_arch="arm"
  if [[ "${TARGET_PRODUCT}" =~ .*x86.* ]]; then
    toolchain_arch="x86"
  fi

  toolchain_target=$(basename \
    ${ANDROID_NDK_ROOT}/toolchains/${toolchain_arch}-*)
  toolchain_path="${ANDROID_NDK_ROOT}/toolchains/${toolchain_target}"\
"/prebuilt/${toolchain_dir}/bin/"

  # Set only if not already set.
  # Don't override ANDROID_TOOLCHAIN if set by Android configuration env.
  export ANDROID_TOOLCHAIN=${ANDROID_TOOLCHAIN:-${toolchain_path}}

  common_check_toolchain

  # Add Android SDK/NDK tools to system path.
  export PATH=$PATH:${ANDROID_NDK_ROOT}
  export PATH=$PATH:${ANDROID_SDK_ROOT}/tools
  export PATH=$PATH:${ANDROID_SDK_ROOT}/platform-tools
  # Must have tools like arm-linux-androideabi-gcc on the path for ninja
  export PATH=$PATH:${ANDROID_TOOLCHAIN}

  # Add Chromium Android development scripts to system path.
  # Must be after CHROME_SRC is set.
  export PATH=$PATH:${CHROME_SRC}/build/android

  export OBJCOPY=$(echo ${ANDROID_TOOLCHAIN}/*-objcopy)
  export STRIP=$(echo ${ANDROID_TOOLCHAIN}/*-strip)

  # The set of GYP_DEFINES to pass to gyp. Use 'readlink -e' on directories
  # to canonicalize them (remove double '/', remove trailing '/', etc).
  DEFINES="OS=android"
  DEFINES+=" host_os=${host_os}"

  if [ -n "$CHROME_ANDROID_OFFICIAL_BUILD" ]; then
    DEFINES+=" branding=Chrome"
    DEFINES+=" buildtype=Official"

    # These defines are used by various chrome build scripts to tag the binary's
    # version string as 'official' in linux builds (e.g. in
    # chrome/trunk/src/chrome/tools/build/version.py).
    export OFFICIAL_BUILD=1
    export CHROMIUM_BUILD="_google_chrome"
    export CHROME_BUILD_TYPE="_official"

    # Used by chrome_version_info_posix.cc to display the channel name.
    # Valid values: "unstable", "stable", "dev", "beta".
    export CHROME_VERSION_EXTRA="beta"
  fi

  # The order file specifies the order of symbols in the .text section of the
  # shared library, libchromeview.so.  The file is an order list of section
  # names and the library is linked with option
  # --section-ordering-file=<orderfile>. The order file is updated by profiling
  # startup after compiling with the order_profiling=1 GYP_DEFINES flag.
  ORDER_DEFINES="order_text_section=${CHROME_SRC}/orderfiles/orderfile.out"

  # The following defines will affect ARM code generation of both C/C++ compiler
  # and V8 mksnapshot.
  case "${TARGET_PRODUCT}" in
    "passion"|"soju"|"sojua"|"sojus"|"yakju"|"mysid"|"nakasi")
      DEFINES+=" target_arch=arm"
      DEFINES+=" arm_neon=1 armv7=1 arm_thumb=1"
      DEFINES+=" ${ORDER_DEFINES}"
      ;;
    "trygon"|"tervigon")
      DEFINES+=" target_arch=arm"
      DEFINES+=" arm_neon=0 armv7=1 arm_thumb=1 arm_fpu=vfpv3-d16"
      DEFINES+=" ${ORDER_DEFINES}"
      ;;
    "full")
      DEFINES+=" target_arch=arm"
      DEFINES+=" arm_neon=0 armv7=0 arm_thumb=1 arm_fpu=vfp"
      ;;
    *x86*)
    # TODO(tedbo): The ia32 build fails on ffmpeg, so we disable it here.
      DEFINES+=" target_arch=ia32 use_libffmpeg=0"

      host_arch=$(uname -m | sed -e \
        's/i.86/ia32/;s/x86_64/x64/;s/amd64/x64/;s/arm.*/arm/;s/i86pc/ia32/')
      DEFINES+=" host_arch=${host_arch}"
      ;;
    *)
      echo "TARGET_PRODUCT: ${TARGET_PRODUCT} is not supported." >& 2
      return 1
  esac
}


################################################################################
# Exports common GYP variables based on variable DEFINES and CHROME_SRC.
################################################################################
common_gyp_vars() {
  export GYP_DEFINES="${DEFINES}"
  export GYP_GENERATORS="make-android"

  # Use our All target as the default
  export GYP_GENERATOR_FLAGS="${GYP_GENERATOR_FLAGS} default_target=All"

  # We want to use our version of "all" targets.
  export CHROMIUM_GYP_FILE="${CHROME_SRC}/build/all_android.gyp"
}


################################################################################
# Initializes environment variables for NDK/SDK build. Only Android NDK Revision
# 7 on Linux or Mac is offically supported. To run this script, the system
# environment ANDROID_NDK_ROOT must be set to Android NDK's root path.  The
# ANDROID_SDK_ROOT only needs to be set to override the default SDK which is in
# the tree under $ROOT/src/third_party/android_tools/sdk.
# TODO(navabi): Add NDK to $ROOT/src/third_party/android_tools/ndk.
# To build Chromium for Android with NDK/SDK follow the steps below:
#  > export ANDROID_NDK_ROOT=<android ndk root>
#  > export ANDROID_SDK_ROOT=<android sdk root> # to override the default sdk
#  > . build/android/envsetup.sh --sdk
#  > make
################################################################################
sdk_build_init() {
  if [ ! -d "${ANDROID_NDK_ROOT}" ]; then
    echo "ANDROID_NDK_ROOT must be set to the path of Android NDK." >& 2
    echo "which could be installed by" >& 2
    echo "<chromium_tree>/src/build/install-build-deps-android-sdk.sh" >& 2
    return 1
  fi

  # If ANDROID_SDK_ROOT is set when envsetup is run, use the sdk pointed to by
  # the environment variable.  Otherwise, use the default sdk from the tree.
  if [ ! -d "${ANDROID_SDK_ROOT}" ]; then
    export ANDROID_SDK_ROOT="${CHROME_SRC}/third_party/android_tools/sdk/"
  fi

  # Makes sure ANDROID_BUILD_TOP is unset if build has option --sdk
  unset ANDROID_BUILD_TOP

  # Set default target.
  export TARGET_PRODUCT="${TARGET_PRODUCT:-trygon}"

  # Android sdk platform version to use
  export ANDROID_SDK_VERSION=15
  # Unset toolchain so that it can be set based on TARGET_PRODUCT.
  # This makes it easy to switch between architectures.
  unset ANDROID_TOOLCHAIN

  common_vars_defines

  DEFINES+=" sdk_build=1"
  # If we are building NDK/SDK, and in the upstream (open source) tree,
  # define a special variable for bringup purposes.
  case "${ANDROID_BUILD_TOP-undefined}" in
    "undefined")
      DEFINES+=" android_upstream_bringup=1"
      ;;
  esac

  # Sets android specific directories to NOT_SDK_COMPLIANT.  This will allow
  # android_gyp to generate make files, but will cause errors when (and only
  # when) building targets that depend on these directories.
  DEFINES+=" android_src='NOT_SDK_COMPLIANT'"
  DEFINES+=" android_product_out=${CHROME_SRC}/out/android"
  DEFINES+=" android_lib='NOT_SDK_COMPLIANT'"
  DEFINES+=" android_static_lib='NOT_SDK_COMPLIANT'"
  DEFINES+=\
" android_sdk=${ANDROID_SDK_ROOT}/platforms/android-${ANDROID_SDK_VERSION}"
  DEFINES+=" android_sdk_root=${ANDROID_SDK_ROOT}"
  DEFINES+=" android_sdk_tools=${ANDROID_SDK_ROOT}/platform-tools"
  DEFINES+=" android_sdk_version=${ANDROID_SDK_VERSION}"
  DEFINES+=" android_toolchain=${ANDROID_TOOLCHAIN}"

  common_gyp_vars

  if [ -n "$CHROME_ANDROID_BUILD_WEBVIEW" ]; then
    # Can not build WebView with NDK/SDK because it needs the Android build
    # system and build inside an Android source tree.
    echo "Can not build WebView with NDK/SDK.  Requires android source tree." \
        >& 2
    echo "Try . build/android/envsetup.sh instead." >& 2
    return 1
  fi

}


################################################################################
# Initializes environment variables for build with android source.  This expects
# android environment to be set up along with lunch.  To build:
#  > . build/envsetup.sh
#  >  lunch <lunch-type>
#  > . build/android/envsetup.sh
#  > make
#############################################################################
non_sdk_build_init() {
  # We export "TOP" here so that "mmm" can be run to build Java code without
  # having to cd to $ANDROID_BUILD_TOP.
  export TOP="$ANDROID_BUILD_TOP"

  # We export "ANDROID_NDK_ROOT" for building Chromium for Android by NDK.
  export ANDROID_NDK_ROOT=${ANDROID_BUILD_TOP}/prebuilts/ndk/android-ndk-r7

  # Android sdk platform version to use
  export ANDROID_SDK_VERSION=15

  # We export "ANDROID_SDK_ROOT" for building Java source with the SDK.
  export ANDROID_SDK_ROOT=${ANDROID_BUILD_TOP}/prebuilts/sdk/\
${ANDROID_SDK_VERSION}
  # Needed by android antfiles when creating apks.
  export ANDROID_SDK_HOME=${ANDROID_SDK_ROOT}

  common_vars_defines

  DEFINES+=" sdk_build=0"
  DEFINES+=" android_product_out=${ANDROID_PRODUCT_OUT}"

  if [ -n "$CHROME_ANDROID_BUILD_WEBVIEW" ]; then
    webview_build_init
    return
  fi

  # The non-SDK build currently requires the SDK path to build the framework
  # Java aidl files. TODO(steveblock): Investigate avoiding this requirement.
  DEFINES+=" android_sdk=${ANDROID_SDK_ROOT}"
  DEFINES+=" android_sdk_root=${ANDROID_SDK_ROOT}"
  DEFINES+=" android_sdk_tools=${ANDROID_SDK_ROOT}/../tools/linux"
  DEFINES+=" android_sdk_version=${ANDROID_SDK_VERSION}"
  DEFINES+=" android_toolchain=${ANDROID_TOOLCHAIN}"

  common_gyp_vars
}

################################################################################
# To build WebView, we use the Android build system and build inside an Android
# source tree. This method is called from non_sdk_build_init() and adds to the
# settings specified there.
#############################################################################
webview_build_init() {
  # We need to supply SDK paths relative to the top of the Android tree to make
  # sure the generated Android makefiles are portable, as they will be checked
  # into the Android tree.
  ANDROID_SDK=$(python -c \
      "import os.path; print os.path.relpath('${ANDROID_SDK_ROOT}', '${TOP}')")
  ANDROID_SDK_TOOLS=$(python -c \
      "import os.path; \
      print os.path.relpath('${ANDROID_SDK_ROOT}/../tools/linux', \
      '${TOP}')")
  DEFINES+=" android_build_type=1"
  DEFINES+=" android_upstream_bringup=1"
  DEFINES+=" android_sdk=\$(GYP_ABS_ANDROID_TOP_DIR)/${ANDROID_SDK}"
  DEFINES+=" android_sdk_root=${ANDROID_SDK_ROOT}"
  DEFINES+=" android_sdk_tools=\$(GYP_ABS_ANDROID_TOP_DIR)/${ANDROID_SDK_TOOLS}"
  DEFINES+=" android_sdk_version=${ANDROID_SDK_VERSION}"
  DEFINES+=" android_toolchain=${ANDROID_TOOLCHAIN}"
  export GYP_DEFINES="${DEFINES}"

  export GYP_GENERATORS="android"

  export GYP_GENERATOR_FLAGS="${GYP_GENERATOR_FLAGS} default_target=All"
  export GYP_GENERATOR_FLAGS="${GYP_GENERATOR_FLAGS} limit_to_target_all=1"
  export GYP_GENERATOR_FLAGS="${GYP_GENERATOR_FLAGS} auto_regeneration=0"

  # TODO(torne): This isn't upstream yet. Upstream it or remove this setting.
  export CHROMIUM_GYP_FILE="${CHROME_SRC}/build/all_android_webview.gyp"
}
