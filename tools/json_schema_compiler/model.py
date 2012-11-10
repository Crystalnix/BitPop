# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import copy
import os.path
import re

class ParseException(Exception):
  """Thrown when data in the model is invalid.
  """
  def __init__(self, parent, message):
    hierarchy = _GetModelHierarchy(parent)
    hierarchy.append(message)
    Exception.__init__(
        self, 'Model parse exception at:\n' + '\n'.join(hierarchy))

class Model(object):
  """Model of all namespaces that comprise an API.

  Properties:
  - |namespaces| a map of a namespace name to its model.Namespace
  """
  def __init__(self):
    self.namespaces = {}

  def AddNamespace(self, json, source_file):
    """Add a namespace's json to the model and returns the namespace.
    """
    namespace = Namespace(json, source_file)
    self.namespaces[namespace.name] = namespace
    return namespace

class Namespace(object):
  """An API namespace.

  Properties:
  - |name| the name of the namespace
  - |unix_name| the unix_name of the namespace
  - |source_file| the file that contained the namespace definition
  - |source_file_dir| the directory component of |source_file|
  - |source_file_filename| the filename component of |source_file|
  - |types| a map of type names to their model.Type
  - |functions| a map of function names to their model.Function
  - |events| a map of event names to their model.Function
  - |properties| a map of property names to their model.Property
  """
  def __init__(self, json, source_file):
    self.name = json['namespace']
    self.unix_name = UnixName(self.name)
    self.source_file = source_file
    self.source_file_dir, self.source_file_filename = os.path.split(source_file)
    self.parent = None
    _AddTypes(self, json)
    _AddFunctions(self, json)
    _AddEvents(self, json)
    _AddProperties(self, json)

class Type(object):
  """A Type defined in the json.

  Properties:
  - |name| the type name
  - |description| the description of the type (if provided)
  - |properties| a map of property unix_names to their model.Property
  - |functions| a map of function names to their model.Function
  - |events| a map of event names to their model.Event
  - |from_client| indicates that instances of the Type can originate from the
    users of generated code, such as top-level types and function results
  - |from_json| indicates that instances of the Type can originate from the
    JSON (as described by the schema), such as top-level types and function
    parameters
  - |type_| the PropertyType of this Type
  - |item_type| if this is an array, the type of items in the array
  """
  def __init__(self, parent, name, json):
    if json.get('type') == 'array':
      self.type_ = PropertyType.ARRAY
      self.item_type = Property(self, name + "Element", json['items'],
                                from_json=True,
                                from_client=True)
    elif json.get('type') == 'string':
      self.type_ = PropertyType.STRING
    else:
      if not (
          'properties' in json or
          'additionalProperties' in json or
          'functions' in json or
          'events' in json):
        raise ParseException(self, name + " has no properties or functions")
      self.type_ = PropertyType.OBJECT
    self.name = name
    self.description = json.get('description')
    self.from_json = True
    self.from_client = True
    self.parent = parent
    self.instance_of = json.get('isInstanceOf', None)
    _AddFunctions(self, json)
    _AddEvents(self, json)
    _AddProperties(self, json, from_json=True, from_client=True)

    additional_properties_key = 'additionalProperties'
    additional_properties = json.get(additional_properties_key)
    if additional_properties:
      self.properties[additional_properties_key] = Property(
          self,
          additional_properties_key,
          additional_properties,
          is_additional_properties=True)

class Function(object):
  """A Function defined in the API.

  Properties:
  - |name| the function name
  - |params| a list of parameters to the function (order matters). A separate
    parameter is used for each choice of a 'choices' parameter.
  - |description| a description of the function (if provided)
  - |callback| the callback parameter to the function. There should be exactly
    one
  - |optional| whether the Function is "optional"; this only makes sense to be
    present when the Function is representing a callback property.
  """
  def __init__(self, parent, json, from_json=False, from_client=False):
    self.name = json['name']
    self.params = []
    self.description = json.get('description')
    self.callback = None
    self.optional = json.get('optional', False)
    self.parent = parent
    self.nocompile = json.get('nocompile')

    callback_param = None
    for param in json.get('parameters', []):
      def GeneratePropertyFromParam(p):
        return Property(self,
                        p['name'], p,
                        from_json=from_json,
                        from_client=from_client)

      if param.get('type') == 'function':
        if callback_param:
          # No ParseException because the webstore has this.
          # Instead, pretend all intermediate callbacks are properties.
          self.params.append(GeneratePropertyFromParam(callback_param))
        callback_param = param
      else:
        self.params.append(GeneratePropertyFromParam(param))

    if callback_param:
      self.callback = Function(self, callback_param, from_client=True)

    self.returns = None
    if 'returns' in json:
      self.returns = Property(self, 'return', json['returns'])

class Property(object):
  """A property of a type OR a parameter to a function.

  Properties:
  - |name| name of the property as in the json. This shouldn't change since
    it is the key used to access DictionaryValues
  - |unix_name| the unix_style_name of the property. Used as variable name
  - |optional| a boolean representing whether the property is optional
  - |description| a description of the property (if provided)
  - |type_| the model.PropertyType of this property
  - |compiled_type| the model.PropertyType that this property should be
    compiled to from the JSON. Defaults to |type_|.
  - |ref_type| the type that the REF property is referencing. Can be used to
    map to its model.Type
  - |item_type| a model.Property representing the type of each element in an
    ARRAY
  - |properties| the properties of an OBJECT parameter
  - |from_client| indicates that instances of the Type can originate from the
    users of generated code, such as top-level types and function results
  - |from_json| indicates that instances of the Type can originate from the
    JSON (as described by the schema), such as top-level types and function
    parameters
  """

  def __init__(self, parent, name, json, is_additional_properties=False,
      from_json=False, from_client=False):
    self.name = name
    self._unix_name = UnixName(self.name)
    self._unix_name_used = False
    self.optional = json.get('optional', False)
    self.functions = {}
    self.has_value = False
    self.description = json.get('description')
    self.parent = parent
    self.from_json = from_json
    self.from_client = from_client
    self.instance_of = json.get('isInstanceOf', None)
    _AddProperties(self, json)
    if is_additional_properties:
      self.type_ = PropertyType.ADDITIONAL_PROPERTIES
    elif '$ref' in json:
      self.ref_type = json['$ref']
      self.type_ = PropertyType.REF
    elif 'enum' in json and json.get('type') == 'string':
      # Non-string enums (as in the case of [legalValues=(1,2)]) should fall
      # through to the next elif.
      self.enum_values = []
      for value in json['enum']:
        self.enum_values.append(value)
      self.type_ = PropertyType.ENUM
    elif 'type' in json:
      self.type_ = self._JsonTypeToPropertyType(json['type'])
      if self.type_ == PropertyType.ARRAY:
        self.item_type = Property(self, name + "Element", json['items'],
            from_json=from_json,
            from_client=from_client)
      elif self.type_ == PropertyType.OBJECT:
        # These members are read when this OBJECT Property is used as a Type
        type_ = Type(self, self.name, json)
        # self.properties will already have some value from |_AddProperties|.
        self.properties.update(type_.properties)
        self.functions = type_.functions
    elif 'choices' in json:
      if not json['choices'] or len(json['choices']) == 0:
        raise ParseException(self, 'Choices has no choices')
      self.choices = {}
      self.type_ = PropertyType.CHOICES
      self.compiled_type = self.type_
      for choice_json in json['choices']:
        choice = Property(self, self.name, choice_json,
            from_json=from_json,
            from_client=from_client)
        choice.unix_name = UnixName(self.name + choice.type_.name)
        # The existence of any single choice is optional
        choice.optional = True
        self.choices[choice.type_] = choice
    elif 'value' in json:
      self.has_value = True
      self.value = json['value']
      if type(self.value) == int:
        self.type_ = PropertyType.INTEGER
        self.compiled_type = self.type_
      else:
        # TODO(kalman): support more types as necessary.
        raise ParseException(
            self, '"%s" is not a supported type' % type(self.value))
    else:
      raise ParseException(
          self, 'Property has no type, $ref, choices, or value')
    if 'compiled_type' in json:
      if 'type' in json:
        self.compiled_type = self._JsonTypeToPropertyType(json['compiled_type'])
      else:
        raise ParseException(self, 'Property has compiled_type but no type')
    else:
      self.compiled_type = self.type_

  def _JsonTypeToPropertyType(self, json_type):
    try:
      return {
        'any': PropertyType.ANY,
        'array': PropertyType.ARRAY,
        'binary': PropertyType.BINARY,
        'boolean': PropertyType.BOOLEAN,
        'integer': PropertyType.INTEGER,
        'int64': PropertyType.INT64,
        'function': PropertyType.FUNCTION,
        'number': PropertyType.DOUBLE,
        'object': PropertyType.OBJECT,
        'string': PropertyType.STRING,
      }[json_type]
    except KeyError:
      raise NotImplementedError('Type %s not recognized' % json_type)

  def GetUnixName(self):
    """Gets the property's unix_name. Raises AttributeError if not set.
    """
    if not self._unix_name:
      raise AttributeError('No unix_name set on %s' % self.name)
    self._unix_name_used = True
    return self._unix_name

  def SetUnixName(self, unix_name):
    """Set the property's unix_name. Raises AttributeError if the unix_name has
    already been used (GetUnixName has been called).
    """
    if unix_name == self._unix_name:
      return
    if self._unix_name_used:
      raise AttributeError(
          'Cannot set the unix_name on %s; '
          'it is already used elsewhere as %s' %
          (self.name, self._unix_name))
    self._unix_name = unix_name

  def Copy(self):
    """Makes a copy of this model.Property object and allow the unix_name to be
    set again.
    """
    property_copy = copy.copy(self)
    property_copy._unix_name_used = False
    return property_copy

  unix_name = property(GetUnixName, SetUnixName)

class PropertyType(object):
  """Enum of different types of properties/parameters.
  """
  class _Info(object):
    def __init__(self, is_fundamental, name):
      self.is_fundamental = is_fundamental
      self.name = name

    def __repr__(self):
      return self.name

  INTEGER = _Info(True, "INTEGER")
  INT64 = _Info(True, "INT64")
  DOUBLE = _Info(True, "DOUBLE")
  BOOLEAN = _Info(True, "BOOLEAN")
  STRING = _Info(True, "STRING")
  ENUM = _Info(False, "ENUM")
  ARRAY = _Info(False, "ARRAY")
  REF = _Info(False, "REF")
  CHOICES = _Info(False, "CHOICES")
  OBJECT = _Info(False, "OBJECT")
  FUNCTION = _Info(False, "FUNCTION")
  BINARY = _Info(False, "BINARY")
  ANY = _Info(False, "ANY")
  ADDITIONAL_PROPERTIES = _Info(False, "ADDITIONAL_PROPERTIES")

def UnixName(name):
  """Returns the unix_style name for a given lowerCamelCase string.
  """
  # First replace any lowerUpper patterns with lower_Upper.
  s1 = re.sub('([a-z])([A-Z])', r'\1_\2', name)
  # Now replace any ACMEWidgets patterns with ACME_Widgets
  s2 = re.sub('([A-Z]+)([A-Z][a-z])', r'\1_\2', s1)
  # Finally, replace any remaining periods, and make lowercase.
  return s2.replace('.', '_').lower()

def _GetModelHierarchy(entity):
  """Returns the hierarchy of the given model entity."""
  hierarchy = []
  while entity:
    try:
      hierarchy.append(entity.name)
    except AttributeError:
      hierarchy.append(repr(entity))
    entity = entity.parent
  hierarchy.reverse()
  return hierarchy

def _AddTypes(model, json):
  """Adds Type objects to |model| contained in the 'types' field of |json|.
  """
  model.types = {}
  for type_json in json.get('types', []):
    type_ = Type(model, type_json['id'], type_json)
    model.types[type_.name] = type_

def _AddFunctions(model, json):
  """Adds Function objects to |model| contained in the 'functions' field of
  |json|.
  """
  model.functions = {}
  for function_json in json.get('functions', []):
    function = Function(model, function_json, from_json=True)
    model.functions[function.name] = function

def _AddEvents(model, json):
  """Adds Function objects to |model| contained in the 'events' field of |json|.
  """
  model.events = {}
  for event_json in json.get('events', []):
    event = Function(model, event_json, from_client=True)
    model.events[event.name] = event

def _AddProperties(model, json, from_json=False, from_client=False):
  """Adds model.Property objects to |model| contained in the 'properties' field
  of |json|.
  """
  model.properties = {}
  for name, property_json in json.get('properties', {}).items():
    model.properties[name] = Property(
        model,
        name,
        property_json,
        from_json=from_json,
        from_client=from_client)
