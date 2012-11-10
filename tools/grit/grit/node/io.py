#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''The <output> and <file> elements.
'''

import grit.format.rc_header

from grit import xtb_reader
from grit.node import base


class FileNode(base.Node):
  '''A <file> element.'''

  def __init__(self):
    super(FileNode, self).__init__()
    self.re = None
    self.should_load_ = True

  def IsTranslation(self):
    return True

  def GetLang(self):
    return self.attrs['lang']

  def DisableLoading(self):
    self.should_load_ = False

  def MandatoryAttributes(self):
    return ['path', 'lang']

  def RunGatherers(self, recursive=False, debug=False):
    if not self.should_load_ or not self.SatisfiesOutputCondition():
      return

    root = self.GetRoot()
    defs = {}
    if hasattr(root, 'defines'):
      defs = root.defines

    xtb_file = open(self.ToRealPath(self.GetInputPath()))
    try:
      lang = xtb_reader.Parse(xtb_file,
                              self.UberClique().GenerateXtbParserCallback(
                                self.attrs['lang'], debug=debug),
                              defs=defs)
    except:
      print "Exception during parsing of %s" % self.GetInputPath()
      raise
    # We special case 'he' and 'iw' because the translation console uses 'iw'
    # and we use 'he'.
    assert (lang == self.attrs['lang'] or
            (lang == 'iw' and self.attrs['lang'] == 'he')), ('The XTB file you '
            'reference must contain messages in the language specified\n'
            'by the \'lang\' attribute.')

  def GetInputPath(self):
    return self.attrs['path']


class OutputNode(base.Node):
  '''An <output> element.'''

  def MandatoryAttributes(self):
    return ['filename', 'type']

  def DefaultAttributes(self):
    return {
      'lang' : '', # empty lang indicates all languages
      'language_section' : 'neutral', # defines a language neutral section
      'context' : '',
    }

  def GetType(self):
    if self.SatisfiesOutputCondition():
      return self.attrs['type']
    else:
      return 'output_condition_not_satisfied_%s' % self.attrs['type']

  def GetLanguage(self):
    '''Returns the language ID, default 'en'.'''
    return self.attrs['lang']

  def GetContext(self):
    return self.attrs['context']

  def GetFilename(self):
    return self.attrs['filename']

  def GetOutputFilename(self):
    if hasattr(self, 'output_filename'):
      return self.output_filename
    else:
      return self.attrs['filename']

  def _IsValidChild(self, child):
    return isinstance(child, EmitNode)

class EmitNode(base.ContentNode):
  ''' An <emit> element.'''

  def DefaultAttributes(self):
    return { 'emit_type' : 'prepend'}

  def GetEmitType(self):
    '''Returns the emit_type for this node. Default is 'append'.'''
    return self.attrs['emit_type']

  def ItemFormatter(self, t):
    if t == 'rc_header':
      return grit.format.rc_header.EmitAppender()
    else:
      return super(EmitNode, self).ItemFormatter(t)



