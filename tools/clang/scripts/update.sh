#!/bin/bash
# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script will check out llvm and clang into third_party/llvm and build it.

CLANG_REVISION=129729

THIS_DIR="$(dirname "${0}")"
LLVM_DIR="${THIS_DIR}"/../../../third_party/llvm
CLANG_DIR="${LLVM_DIR}"/tools/clang

# Die if any command dies.
set -e

# Echo all commands.
set -x

# Check out.
svn co --force http://llvm.org/svn/llvm-project/llvm/trunk@"${CLANG_REVISION}" \
  "${LLVM_DIR}"
svn co --force http://llvm.org/svn/llvm-project/cfe/trunk@"${CLANG_REVISION}" \
  "${CLANG_DIR}"

# Build clang (in a separate directory).
# The clang bots have this path hardcoded in built/scripts/slave/compile.py,
# so if you change it you also need to change these links.
mkdir -p "${LLVM_DIR}"/../llvm-build
cd "${LLVM_DIR}"/../llvm-build
if [ ! -f ./config.status ]; then
  ../llvm/configure --enable-optimized
fi

NUM_JOBS=3
if [ "$(uname -s)" = "Linux" ]; then
  NUM_JOBS="$(grep -c "^processor" /proc/cpuinfo)"
elif [ "$(uname -s)" = "Darwin" ]; then
  NUM_JOBS="$(sysctl -n hw.ncpu)"
fi
make -j"${NUM_JOBS}"
cd -

# Build plugin.
# Copy it into the clang tree and use clang's build system to compile the
# plugin.
PLUGIN_SRC_DIR="${THIS_DIR}"/../plugins
PLUGIN_DST_DIR="${LLVM_DIR}"/../llvm/tools/clang/tools/chrome-plugin
PLUGIN_BUILD_DIR="${LLVM_DIR}"/../llvm-build/tools/clang/tools/chrome-plugin
rm -rf "${PLUGIN_DST_DIR}"
cp -R "${PLUGIN_SRC_DIR}" "${PLUGIN_DST_DIR}"
rm -rf "${PLUGIN_BUILD_DIR}"
mkdir -p "${PLUGIN_BUILD_DIR}"
cp "${PLUGIN_SRC_DIR}"/Makefile "${PLUGIN_BUILD_DIR}"
make -j"${NUM_JOBS}" -C "${PLUGIN_BUILD_DIR}"
