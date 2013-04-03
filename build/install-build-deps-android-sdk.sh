#!/bin/bash -x

# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

# The script is to install Android SDK, NDK for build chromium on Android, and
# doesn't need to run as root.

# Using Android 4.0, API Level: 14 (ice cream sandwich). The SDK package is
# about 25M.
SDK_TARGET_VER=20

SDK_FILE_NAME="android-sdk_r${SDK_TARGET_VER}-linux.tgz"
SDK_DOWNLOAD_URL="http://dl.google.com/android/${SDK_FILE_NAME}"
SDK_MD5SUM="22a81cf1d4a951c62f71a8758290e9bb"

# Using "ANDROID_SDK_ROOT/tools/android list targets" to get the matching target
# id which will be loaded in simulator for testing.
# For example: the output of the listed the target could be below, and the
# 'android-13' is the SDK_TARGET_ID in this case.
# id: 9 or "android-13"
#     Name: Android 3.2
#     Type: Platform
#     API level: 13
#     Revision: 1
#     Skins: WXGA (default)
SDK_TARGET_ID=android-15

# Using NDK r7; The package is about 64M.
# *** DO NOT UPDATE THE NDK without updating the 64-bit linker changes ***
# *** at the end of this file ***
NDK_FILE_NAME="android-ndk-r7-linux-x86.tar.bz2"
NDK_DOWNLOAD_URL="http://dl.google.com/android/ndk/${NDK_FILE_NAME}"
NDK_MD5SUM="bf15e6b47bf50824c4b96849bf003ca3"

# The temporary directory used to store the downloaded file.
TEMPDIR=$(mktemp -d)
cleanup() {
  local status=${?}
  trap - EXIT
  rm -rf "${TEMPDIR}"
  exit ${status}
}
trap cleanup EXIT

##########################################################
# Download and install a tgz package by wget and tar -xvf.
# The current directory is changed in this function.
# Arguments:
#   local_file_name, the name of downloaded file.
#   download_url, the url to download the package.
#   md5, the package's md5 which could be found in download page.
#   install_path, where the package should be installed.
# Returns:
#   None
##########################################################
install_dev_kit() {
  local local_file_name="${1}"
  local download_url="${2}"
  local md5="${3}"
  local install_path="${4}"

  cd "${TEMPDIR}"
  wget "${download_url}"

  local computed_md5=$(md5sum "${local_file_name}" | cut -d' ' -f1)
  if [[ "${computed_md5}" != "${md5}" ]]; then
    echo "Downloaded ${local_file_name} has bad md5sum, which is expected" >& 2
    echo "to be ${md5} but was ${computed_md5}" >& 2
    exit 1
  fi

  echo "Install ${local_file_name}"
  mv "${local_file_name}" "${install_path}"
  cd "${install_path}"
  tar -xf "${local_file_name}"
}

if [[ -z "${ANDROID_SDK_ROOT}" ]]; then
  echo "Please set ANDROID_SDK_ROOT to where they should installed to." >& 2
  echo "For example: /usr/local/android-sdk-linux_x86" >& 2
  exit 1
fi

if [[ -z "${ANDROID_NDK_ROOT}" ]]; then
  echo "Please set ANDROID_NDK_ROOT to where they should installed to." >& 2
  echo "For example: /usr/local/android-ndk-r6b" >& 2
  exit 1
fi

# Install Android SDK if it doesn't exist.
if [[ ! -d "${ANDROID_SDK_ROOT}" ]]; then
  echo 'Install ANDROID SDK ...'
  (install_dev_kit "${SDK_FILE_NAME}" "${SDK_DOWNLOAD_URL}" "${SDK_MD5SUM}" \
                  $(dirname "${ANDROID_SDK_ROOT}"))
fi

# Check the installed SDK revision
SDK_VER=$(sed '/^\#/d' ${ANDROID_SDK_ROOT}/tools/source.properties | \
    grep 'Pkg.Revision' |tail -n 1 | cut -d "=" -f2- | \
    sed 's/^[[:space:]]*//;s/[[:space:]]*$//')

# Install the target if it doesn't exist. The package installed above contains
# no platform, platform-tool or tool, all those should be installed by
# ${ANDROID_SDK_ROOT}/tools/android.
# Also, if current SDK version is lower than ${SDK_TARGET_VER}, upgrade the SDK.
found=$("${ANDROID_SDK_ROOT}/tools/android" list targets \
        | grep "${SDK_TARGET_ID}" | wc -l)
if [[ "$found" = "0" || ${SDK_VER} -lt ${SDK_TARGET_VER} ]]; then
  # Updates the SDK by installing the necessary components.
  # From current configuration, all android platforms will be installed.
  # This will take a little bit long time.
  echo "Upgrade SDK and install platform, platform-tool and tool ..."

  if [[ ${SDK_VER} -lt 17 ]]; then
    update_flag=" -o "
  else
    update_flag=" --all "
  fi
  # Updates the SDK to latest version firstly.
    "${ANDROID_SDK_ROOT}"/tools/android update sdk ${update_flag} --no-ui \
        --filter platform-tool,tool,system-image,${SDK_TARGET_ID}
fi

# Create a Android Virtual Device named 'buildbot' with default hardware
# configuration and override the existing one, since there is no easy way to
# check whether current AVD has correct configuration and it takes almost no
# time to create a new one.  Create one ARM AVD and one x86 AVD.
export ANDROID_SDK_HOME=${ANDROID_SDK_ROOT}
"${ANDROID_SDK_ROOT}/tools/android" --silent create avd --name avd_armeabi \
  --abi armeabi-v7a --target ${SDK_TARGET_ID} -c 64M --force <<< "no"

"${ANDROID_SDK_ROOT}/tools/android" --silent create avd --name avd_x86 \
  --abi x86 --target ${SDK_TARGET_ID} -c 64M --force <<< "no"

# Install Android NDK if it doesn't exist.
if [[ ! -d "${ANDROID_NDK_ROOT}" ]]; then
  echo 'Install ANDROID NDK ...'
  (install_dev_kit "${NDK_FILE_NAME}" "${NDK_DOWNLOAD_URL}" "${NDK_MD5SUM}" \
                  $(dirname "${ANDROID_NDK_ROOT}"))
fi

# Install the 64-bit linker if needed.
ROOT=$(cd "$(dirname $0)/.."; pwd)
LINKER_DIR_PREFIX="$ANDROID_NDK_ROOT/toolchains/\
arm-linux-androideabi-4.4.3/prebuilt/linux-x86"
LINKER_DIRNAME_1="$LINKER_DIR_PREFIX/bin"
LINKER_BASENAME_1=arm-linux-androideabi-ld
LINKER_DIRNAME_2="$LINKER_DIR_PREFIX/arm-linux-androideabi/bin"
LINKER_BASENAME_2=ld
NEW_LINKER=arm-linux-androideabi-ld.e4df3e0a5bb640ccfa2f30ee67fe9b3146b152d6

# $1: destination directory
# $2: destination binary
function replace_linker {
  local linker_dirname=$1
  local linker_basename=$2
  if [[ -f "$ROOT/third_party/aosp/$NEW_LINKER" ]]; then
    if [[ -d "$linker_dirname" ]]; then
      if [[ ! -f "$linker_dirname/$NEW_LINKER" ]]; then
        echo "Installing linker in $linker_dirname"
        cp $ROOT/third_party/aosp/$NEW_LINKER "$linker_dirname/$NEW_LINKER"
        mv "$linker_dirname/$linker_basename" \
          "$linker_dirname/$linker_basename.orig"
        ( cd "$linker_dirname" ; ln -s "$NEW_LINKER" "$linker_basename" )
      fi
      if [[ ! -f "$linker_dirname/$NEW_LINKER" ]]; then
        echo "Could not copy linker"
        exit 1
      fi
    fi
  fi
}

replace_linker $LINKER_DIRNAME_1 $LINKER_BASENAME_1
replace_linker $LINKER_DIRNAME_2 $LINKER_BASENAME_2
