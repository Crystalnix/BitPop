# Copyright (c) 2010 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys

SCRIPT_PATH = os.path.dirname(sys.argv[0])
if SCRIPT_PATH == "":
  SCRIPT_PATH = os.getcwd()

PATHS_TO_TRY = [
  '\\..\\..\\build\\Debug\\chromoting_host_keygen.exe',
  '\\..\\..\\build\\Release\\chromoting_host_keygen.exe',
  '\\..\\Debug\\chromoting_host_keygen.exe',
  '\\..\\Release\\chromoting_host_keygen.exe',
  '/../../xcodebuild/Debug/chromoting_host_keygen',
  '/../../xcodebuild/Release/chromoting_host_keygen',
  '/../../out/Debug/chromoting_host_keygen',
  '/../../out/Release/chromoting_host_keygen']

KEYGEN_PATH = None
for path in PATHS_TO_TRY:
  if os.path.exists(SCRIPT_PATH + path):
    KEYGEN_PATH = SCRIPT_PATH + path
    break

if not KEYGEN_PATH:
  raise Exception("Unable to find chromoting_host_keygen. Please build it " +
                  "and try again")

def generateRSAKeyPair():
  """Returns (priv, pub) keypair where priv is a new private key and
  pub is the corresponding public key.  Both keys are BASE64 encoded."""
  pipe = os.popen(KEYGEN_PATH)
  out = pipe.readlines()
  if len(out) != 2:
    raise Exception("chromoting_host_keygen failed.")
  return (out[0].strip(), out[1].strip())
