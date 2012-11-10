#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import idl_schema
import unittest

def getFunction(schema, name):
  for item in schema['functions']:
    if item['name'] == name:
      return item
  raise KeyError('Missing function %s' % name)

def getParams(schema, name):
  function = getFunction(schema, name)
  return function['parameters']

def getType(schema, id):
  for item in schema['types']:
    if item['id'] == id:
      return item

class IdlSchemaTest(unittest.TestCase):
  def setUp(self):
    loaded = idl_schema.Load('test/idl_basics.idl')
    self.assertEquals(1, len(loaded))
    self.assertEquals('idl_basics', loaded[0]['namespace'])
    self.idl_basics = loaded[0]

  def testSimpleCallbacks(self):
    schema = self.idl_basics
    expected = [{'type':'function', 'name':'Callback1', 'parameters':[]}]
    self.assertEquals(expected, getParams(schema, 'function4'))

    expected = [{'type':'function', 'name':'Callback2',
                 'parameters':[{'name':'x', 'type':'integer'}]}]
    self.assertEquals(expected, getParams(schema, 'function5'))

    expected = [{'type':'function', 'name':'Callback3',
                 'parameters':[{'name':'arg', '$ref':'idl_basics.MyType1'}]}]
    self.assertEquals(expected, getParams(schema, 'function6'))

  def testCallbackWithArrayArgument(self):
    schema = self.idl_basics
    expected = [{'type':'function', 'name':'Callback4',
                 'parameters':[{'name':'arg', 'type':'array',
                                'items':{'$ref':'idl_basics.MyType2'}}]}]
    self.assertEquals(expected, getParams(schema, 'function12'))


  def testArrayOfCallbacks(self):
    schema = idl_schema.Load('test/idl_callback_arrays.idl')[0]
    expected = [{'type':'array', 'name':'callbacks',
                 'items':{'type':'function', 'name':'MyCallback',
                          'parameters':[{'type':'integer', 'name':'x'}]}}]
    self.assertEquals(expected, getParams(schema, 'whatever'))

  def testLegalValues(self):
    self.assertEquals({
        'x': {'name': 'x', 'type': 'integer', 'enum': [1,2],
              'description': 'This comment tests \\\"double-quotes\\\".'},
        'y': {'name': 'y', 'type': 'string'}},
      getType(self.idl_basics, 'idl_basics.MyType1')['properties'])

  def testEnum(self):
    schema = self.idl_basics
    expected = {'enum': ['name1', 'name2'], 'description': 'Enum description',
                'type': 'string', 'id': 'idl_basics.EnumType'}
    self.assertEquals(expected, getType(schema, expected['id']))

    expected = [{'name':'type', '$ref':'idl_basics.EnumType'},
                {'type':'function', 'name':'Callback5',
                  'parameters':[{'name':'type', '$ref':'idl_basics.EnumType'}]}]
    self.assertEquals(expected, getParams(schema, 'function13'))

    expected = [{'items': {'$ref': 'idl_basics.EnumType'}, 'name': 'types',
                 'type': 'array'}]
    self.assertEquals(expected, getParams(schema, 'function14'))

  def testNoCompile(self):
    schema = self.idl_basics
    func = getFunction(schema, 'function15')
    self.assertTrue(func is not None)
    self.assertTrue(func['nocompile'])

  def testInternalNamespace(self):
    idl_basics  = self.idl_basics
    self.assertEquals('idl_basics', idl_basics['namespace'])
    self.assertTrue(idl_basics['internal'])
    self.assertFalse(idl_basics['nodoc'])

  def testFunctionComment(self):
    schema = self.idl_basics
    func = getFunction(schema, 'function3')
    self.assertEquals(('This comment should appear in the documentation, '
                       'despite occupying multiple lines.'),
                      func['description'])
    self.assertEquals(
        [{'description': ('So should this comment about the argument. '
                          '<em>HTML</em> is fine too.'),
          'name': 'arg',
          '$ref': 'idl_basics.MyType1'}],
        func['parameters'])
    func = getFunction(schema, 'function4')
    self.assertEquals(('This tests if \\\"double-quotes\\\" are escaped '
                       'correctly.'),
                      func['description'])

if __name__ == '__main__':
  unittest.main()
