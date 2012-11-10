#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''The <structure> element.
'''

import os

from grit import exception
from grit import util
from grit.node import base
from grit.node import variant

import grit.gather.admin_template
import grit.gather.chrome_html
import grit.gather.chrome_scaled_image
import grit.gather.igoogle_strings
import grit.gather.muppet_strings
import grit.gather.policy_json
import grit.gather.rc
import grit.gather.tr_html
import grit.gather.txt

import grit.format.rc
import grit.format.rc_header

# Type of the gatherer to use for each type attribute
_GATHERERS = {
  'accelerators'        : grit.gather.rc.Accelerators,
  'admin_template'      : grit.gather.admin_template.AdmGatherer,
  'chrome_html'         : grit.gather.chrome_html.ChromeHtml,
  'chrome_scaled_image' : grit.gather.chrome_scaled_image.ChromeScaledImage,
  'dialog'              : grit.gather.rc.Dialog,
  'igoogle'             : grit.gather.igoogle_strings.IgoogleStrings,
  'menu'                : grit.gather.rc.Menu,
  'muppet'              : grit.gather.muppet_strings.MuppetStrings,
  'rcdata'              : grit.gather.rc.RCData,
  'tr_html'             : grit.gather.tr_html.TrHtml,
  'txt'                 : grit.gather.txt.TxtFile,
  'version'             : grit.gather.rc.Version,
  'policy_template_metafile' : grit.gather.policy_json.PolicyJson,
}


# Formatter instance to use for each type attribute
# when formatting .rc files.
_RC_FORMATTERS = {
  'accelerators'        : grit.format.rc.RcSection(),
  'admin_template'      : grit.format.rc.RcInclude('ADM'),
  'chrome_html'         : grit.format.rc.RcInclude('BINDATA',
                                                   process_html=True),
  'chrome_scaled_image' : grit.format.rc.RcInclude('BINDATA'),
  'dialog'              : grit.format.rc.RcSection(),
  'igoogle'             : grit.format.rc.RcInclude('XML'),
  'menu'                : grit.format.rc.RcSection(),
  'muppet'              : grit.format.rc.RcInclude('XML'),
  'rcdata'              : grit.format.rc.RcSection(),
  'tr_html'             : grit.format.rc.RcInclude('HTML'),
  'txt'                 : grit.format.rc.RcInclude('TXT'),
  'version'             : grit.format.rc.RcSection(),
  'policy_template_metafile': None,
}


# TODO(joi) Print a warning if the 'variant_of_revision' attribute indicates
# that a skeleton variant is older than the original file.


class StructureNode(base.Node):
  '''A <structure> element.'''

  def __init__(self):
    super(StructureNode, self).__init__()

    # Keep track of the last filename we flattened to, so we can
    # avoid doing it more than once.
    self._last_flat_filename = None

  def _IsValidChild(self, child):
    return isinstance(child, variant.SkeletonNode)

  def EndParsing(self):
    super(StructureNode, self).EndParsing()

    # Now that we have attributes and children, instantiate the gatherers.
    gathertype = _GATHERERS[self.attrs['type']]

    self.gatherer = gathertype(self.attrs['file'],
                               self.attrs['name'],
                               self.attrs['encoding'])
    self.gatherer.SetGrdNode(self)
    self.gatherer.SetUberClique(self.UberClique())
    if hasattr(self.GetRoot(), 'defines'):
      self.gatherer.SetDefines(self.GetRoot().defines)
    self.gatherer.SetAttributes(self.attrs)

    self.skeletons = {}  # Maps expressions to skeleton gatherers
    for child in self.children:
      assert isinstance(child, variant.SkeletonNode)
      skel = gathertype(child.attrs['file'],
                        self.attrs['name'],
                        child.GetEncodingToUse(),
                        is_skeleton=True)
      skel.SetGrdNode(self)  # TODO(benrg): Or child? Only used for ToRealPath
      skel.SetUberClique(self.UberClique())
      self.skeletons[child.attrs['expr']] = skel

  def MandatoryAttributes(self):
    return ['type', 'name', 'file']

  def DefaultAttributes(self):
    return { 'encoding' : 'cp1252',
             'exclude_from_rc' : 'false',
             'line_end' : 'unix',
             'output_encoding' : 'utf-8',
             'generateid': 'true',
             'expand_variables' : 'false',
             'output_filename' : '',
             'fold_whitespace': 'false',
             'run_command' : '',
             'allowexternalscript': 'false',
             'flattenhtml': 'false',
             'fallback_to_low_resolution': 'default',
             # TODO(joi) this is a hack - should output all generated files
             # as SCons dependencies; however, for now there is a bug I can't
             # find where GRIT doesn't build the matching fileset, therefore
             # this hack so that only the files you really need are marked as
             # dependencies.
             'sconsdep' : 'false',
             }

  def IsExcludedFromRc(self):
    return self.attrs['exclude_from_rc'] == 'true'

  def Process(self, output_dir):
    """Writes the processed data to output_dir.  In the case of a chrome_html
    structure this will add references to other scale factors.  If flattening
    this will also write file references to be base64 encoded data URLs.  The
    name of the new file is returned."""
    filename = self.ToRealPath(self.GetInputPath())
    flat_filename = os.path.join(output_dir,
        self.attrs['name'] + '_' + os.path.basename(filename))

    if self._last_flat_filename == flat_filename:
      return

    with open(flat_filename, 'wb') as outfile:
      outfile.write(self.gatherer.GetData('', 'utf-8'))

    self._last_flat_filename = flat_filename
    return os.path.basename(flat_filename)

  def GetLineEnd(self):
    '''Returns the end-of-line character or characters for files output because
    of this node ('\r\n', '\n', or '\r' depending on the 'line_end' attribute).
    '''
    if self.attrs['line_end'] == 'unix':
      return '\n'
    elif self.attrs['line_end'] == 'windows':
      return '\r\n'
    elif self.attrs['line_end'] == 'mac':
      return '\r'
    else:
      raise exception.UnexpectedAttribute(
        "Attribute 'line_end' must be one of 'unix' (default), 'windows' or 'mac'")

  def GetCliques(self):
    return self.gatherer.GetCliques()

  def GetDataPackPair(self, lang, encoding):
    """Returns a (id, string|None) pair that represents the resource id and raw
    bytes of the data (or None if no resource is generated).  This is used to
    generate the data pack data file.
    """
    from grit.format import rc_header
    id_map = rc_header.Item.tids_
    id = id_map[self.GetTextualIds()[0]]
    data = self.gatherer.GetData(lang, encoding)
    return id, data

  def GetHtmlResourceFilenames(self):
    """Returns a set of all filenames inlined by this node."""
    return self.gatherer.GetHtmlResourceFilenames()

  def GetInputPath(self):
    return self.gatherer.GetInputPath()

  def GetTextualIds(self):
    if not hasattr(self, 'gatherer'):
      # This case is needed because this method is called by
      # GritNode.ValidateUniqueIds before RunGatherers has been called.
      # TODO(benrg): Fix this?
      return [self.attrs['name']]
    return self.gatherer.GetTextualIds()

  def ItemFormatter(self, t):
    if t == 'rc_header':
      return grit.format.rc_header.Item()
    elif (t in ['rc_all', 'rc_translateable', 'rc_nontranslateable'] and
          self.SatisfiesOutputCondition()):
      return _RC_FORMATTERS[self.attrs['type']]
    elif t == 'resource_map_source':
      from grit.format import resource_map
      return resource_map.SourceInclude()
    elif t == 'resource_file_map_source':
      from grit.format import resource_map
      return resource_map.SourceFileInclude()
    else:
      return super(StructureNode, self).ItemFormatter(t)

  def RunGatherers(self, recursive=False, debug=False):
    if debug:
      print 'Running gatherer %s for file %s' % (
          str(type(self.gatherer)), self.GetInputPath())

    # Note: Parse() is idempotent, therefore this method is also.
    self.gatherer.Parse()
    for skel in self.skeletons.values():
      skel.Parse()

  def GetSkeletonGatherer(self):
    '''Returns the gatherer for the alternate skeleton that should be used,
    based on the expressions for selecting skeletons, or None if the skeleton
    from the English version of the structure should be used.
    '''
    for expr in self.skeletons:
      if self.EvaluateCondition(expr):
        return self.skeletons[expr]
    return None

  def HasFileForLanguage(self):
    return self.attrs['type'] in ['tr_html', 'admin_template', 'txt',
                                  'muppet', 'igoogle', 'chrome_scaled_image',
                                  'chrome_html']

  def ExpandVariables(self):
    '''Variable expansion on structures is controlled by an XML attribute.

    However, old files assume that expansion is always on for Rc files.

    Returns:
      A boolean.
    '''
    attrs = self.GetRoot().attrs
    if 'grit_version' in attrs and attrs['grit_version'] > 1:
      return self.attrs['expand_variables'] == 'true'
    else:
      return (self.attrs['expand_variables'] == 'true' or
              self.attrs['file'].lower().endswith('.rc'))

  def FileForLanguage(self, lang, output_dir, create_file=True,
                      return_if_not_generated=True):
    '''Returns the filename of the file associated with this structure,
    for the specified language.

    Args:
      lang: 'fr'
      output_dir: 'c:\temp'
      create_file: True
    '''
    assert self.HasFileForLanguage()
    # If the source language is requested, and no extra changes are requested,
    # use the existing file.
    if ((not lang or lang == self.GetRoot().GetSourceLanguage()) and
        self.attrs['expand_variables'] != 'true' and
        not self.attrs['run_command']):
      if return_if_not_generated:
        return self.ToRealPath(self.GetInputPath())
      else:
        return None

    if self.attrs['output_filename'] != '':
      filename = self.attrs['output_filename']
    else:
      filename = os.path.basename(self.attrs['file'])
    assert len(filename)
    filename = '%s_%s' % (lang, filename)
    filename = os.path.join(output_dir, filename)

    # Only create the output if it was requested by the call.
    if create_file:
      text = self.gatherer.Translate(
          lang,
          pseudo_if_not_available=self.PseudoIsAllowed(),
          fallback_to_english=self.ShouldFallbackToEnglish(),
          skeleton_gatherer=self.GetSkeletonGatherer())

      file_contents = util.FixLineEnd(text, self.GetLineEnd())
      if self.ExpandVariables():
        # Note that we reapply substitution a second time here.
        # This is because a) we need to look inside placeholders
        # b) the substitution values are language-dependent
        file_contents = self.GetRoot().GetSubstituter().Substitute(file_contents)

      with open(filename, 'wb') as file_object:
        output_stream = util.WrapOutputStream(file_object,
                                              self.attrs['output_encoding'])
        output_stream.write(file_contents)

      if self.attrs['run_command']:
        # Run arbitrary commands after translation is complete so that it
        # doesn't interfere with what's in translation console.
        command = self.attrs['run_command'] % {'filename': filename}
        result = os.system(command)
        assert result == 0, '"%s" failed.' % command

    return filename

  @staticmethod
  def Construct(parent, name, type, file, encoding='cp1252'):
    '''Creates a new node which is a child of 'parent', with attributes set
    by parameters of the same name.
    '''
    node = StructureNode()
    node.StartParsing('structure', parent)
    node.HandleAttribute('name', name)
    node.HandleAttribute('type', type)
    node.HandleAttribute('file', file)
    node.HandleAttribute('encoding', encoding)
    node.EndParsing()
    return node

  def SubstituteMessages(self, substituter):
    '''Propagates substitution to gatherer.

    Args:
      substituter: a grit.util.Substituter object.
    '''
    assert hasattr(self, 'gatherer')
    if self.ExpandVariables():
      self.gatherer.SubstituteMessages(substituter)

