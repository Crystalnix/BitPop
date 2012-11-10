#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''Tool to determine inputs and outputs of a grit file.
'''

import optparse
import os
import posixpath
import sys

from grit import grd_reader
from grit import util

class WrongNumberOfArguments(Exception):
  pass


def Outputs(filename, defines):
  # TODO(joi@chromium.org): The first_ids_file can now be specified
  # via an attribute on the <grit> node.  Once a change lands in
  # WebKit to use this attribute, we can stop specifying the
  # first_ids_file parameter here and instead specify it in all grd
  # files.  For now, since Chrome is the only user of grit_info.py,
  # this is fine.
  grd = grd_reader.Parse(
      filename, defines=defines, tags_to_ignore=set(['messages']),
      first_ids_file='GRIT_DIR/../gritsettings/resource_ids')

  target = []
  lang_folders = {}
  # Add all explicitly-specified output files
  for output in grd.GetOutputFiles():
    path = output.GetFilename()
    target.append(path)

    if path.endswith('.h'):
      path, filename = os.path.split(path)
    if output.attrs['lang']:
      lang_folders[output.attrs['lang']] = os.path.dirname(path)

  # Add all generated files, once for each output language.
  for node in grd:
    if node.name == 'structure':
      # TODO(joi) Should remove the "if sconsdep is true" thing as it is a
      # hack - see grit/node/structure.py
      if node.HasFileForLanguage() and node.attrs['sconsdep'] == 'true':
        for lang in lang_folders:
          path = node.FileForLanguage(lang, lang_folders[lang],
                                      create_file=False,
                                      return_if_not_generated=False)
          if path:
            target.append(path)

  return [t.replace('\\', '/') for t in target]


def GritSourceFiles():
  files = []
  grit_root_dir = os.path.relpath(os.path.dirname(__file__), os.getcwd())
  for root, dirs, filenames in os.walk(grit_root_dir):
    grit_src = [os.path.join(root, f) for f in filenames
                if f.endswith('.py')]
    files.extend(grit_src)
  return files


def Inputs(filename, defines):
  # TODO(joi@chromium.org): The first_ids_file can now be specified
  # via an attribute on the <grit> node.  Once a change lands in
  # WebKit to use this attribute, we can stop specifying the
  # first_ids_file parameter here and instead specify it in all grd
  # files.  For now, since Chrome is the only user of grit_info.py,
  # this is fine.
  grd = grd_reader.Parse(
      filename, debug=False, defines=defines, tags_to_ignore=set(['messages']),
      first_ids_file='GRIT_DIR/../gritsettings/resource_ids')
  files = set()
  contexts = set(output.GetContext() for output in grd.GetOutputFiles())
  for node in grd:
    if (node.name == 'structure' or node.name == 'skeleton' or
        (node.name == 'file' and node.parent and
         node.parent.name == 'translations')):
      # TODO(benrg): This is an awful hack. Do dependencies right.
      for context in contexts:
        grd.SetOutputContext(context)
        if node.SatisfiesOutputCondition():
          files.add(grd.ToRealPath(node.GetInputPath()))
      # If it's a flattened node, grab inlined resources too.
      if node.name == 'structure' and node.attrs['flattenhtml'] == 'true':
        node.RunGatherers(recursive = True)
        files.update(node.GetHtmlResourceFilenames())
    elif node.name == 'grit':
      first_ids_file = node.GetFirstIdsFile()
      if first_ids_file:
        files.add(first_ids_file)
    elif node.name == 'include':
      # Only include files that we actually plan on using.
      if node.SatisfiesOutputCondition():
        files.add(grd.ToRealPath(node.GetInputPath()))
        # If it's a flattened node, grab inlined resources too.
        if node.attrs['flattenhtml'] == 'true':
          files.update(node.GetHtmlResourceFilenames())

  cwd = os.getcwd()
  return [os.path.relpath(f, cwd) for f in sorted(files)]


def PrintUsage():
  print 'USAGE: ./grit_info.py --inputs [-D foo] <grd-file>'
  print '       ./grit_info.py --outputs [-D foo] <out-prefix> <grd-file>'


def DoMain(argv):
  parser = optparse.OptionParser()
  parser.add_option("--inputs", action="store_true", dest="inputs")
  parser.add_option("--outputs", action="store_true", dest="outputs")
  parser.add_option("-D", action="append", dest="defines", default=[])
  # grit build also supports '-E KEY=VALUE', support that to share command
  # line flags.
  parser.add_option("-E", action="append", dest="build_env", default=[])
  parser.add_option("-w", action="append", dest="whitelist_files", default=[])

  options, args = parser.parse_args(argv)

  defines = {}
  for define in options.defines:
    name, val = util.ParseDefine(define)
    defines[name] = val

  if options.inputs:
    if len(args) > 1:
      raise WrongNumberOfArguments("Expected 0 or 1 arguments for --inputs.")

    inputs = []
    if len(args) == 1:
      filename = args[0]
      inputs = Inputs(filename, defines)

    # Add in the grit source files.  If one of these change, we want to re-run
    # grit.
    inputs.extend(GritSourceFiles())
    inputs = [f.replace('\\', '/') for f in inputs]

    if len(args) == 1:
      # Include grd file as second input (works around gyp expecting it).
      inputs = [inputs[0], args[0]] + inputs[1:]
    if options.whitelist_files:
      inputs.extend(options.whitelist_files)
    return '\n'.join(inputs)
  elif options.outputs:
    if len(args) != 2:
      raise WrongNumberOfArguments("Expected exactly 2 arguments for --ouputs.")

    prefix, filename = args
    outputs = [posixpath.join(prefix, f) for f in Outputs(filename, defines)]
    return '\n'.join(outputs)
  else:
    raise WrongNumberOfArguments("Expected --inputs or --outputs.")


def main(argv):
  if sys.version_info < (2, 6):
    print "GRIT requires Python 2.6 or later."
    return 1

  try:
    result = DoMain(argv[1:])
  except WrongNumberOfArguments, e:
    PrintUsage()
    print e
    return 1
  print result
  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv))
