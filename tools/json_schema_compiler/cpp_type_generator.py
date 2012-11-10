# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from code import Code
from model import PropertyType
import any_helper
import cpp_util
import schema_util

class CppTypeGenerator(object):
  """Manages the types of properties and provides utilities for getting the
  C++ type out of a model.Property
  """
  def __init__(self, root_namespace, namespace=None, cpp_namespace=None):
    """Creates a cpp_type_generator. The given root_namespace should be of the
    format extensions::api::sub. The generator will generate code suitable for
    use in the given namespace.
    """
    self._type_namespaces = {}
    self._root_namespace = root_namespace.split('::')
    self._cpp_namespaces = {}
    if namespace and cpp_namespace:
      self._namespace = namespace
      self.AddNamespace(namespace, cpp_namespace)

  def AddNamespace(self, namespace, cpp_namespace):
    """Maps a model.Namespace to its C++ namespace name. All mappings are
    beneath the root namespace.
    """
    for type_ in namespace.types:
      if type_ in self._type_namespaces:
        raise ValueError('Type %s is declared in both %s and %s' %
            (type_, namespace.name, self._type_namespaces[type_].name))
      self._type_namespaces[type_] = namespace
    self._cpp_namespaces[namespace] = cpp_namespace

  def ExpandParams(self, params):
    """Returns the given parameters with PropertyType.CHOICES parameters
    expanded so that each choice is a separate parameter.
    """
    expanded = []
    for param in params:
      if param.type_ == PropertyType.CHOICES:
        for choice in param.choices.values():
          expanded.append(choice)
      else:
        expanded.append(param)
    return expanded

  def GetAllPossibleParameterLists(self, params):
    """Returns all possible parameter lists for the given set of parameters.
    Every combination of arguments passed to any of the PropertyType.CHOICES
    parameters will have a corresponding parameter list returned here.
    """
    if not params:
      return [[]]
    partial_parameter_lists = self.GetAllPossibleParameterLists(params[1:])
    return [[param] + partial_list
            for param in self.ExpandParams(params[:1])
            for partial_list in partial_parameter_lists]

  def GetCppNamespaceName(self, namespace):
    """Gets the mapped C++ namespace name for the given namespace relative to
    the root namespace.
    """
    return self._cpp_namespaces[namespace]

  def GetRootNamespaceStart(self):
    """Get opening root namespace declarations.
    """
    c = Code()
    for namespace in self._root_namespace:
      c.Append('namespace %s {' % namespace)
    return c

  def GetRootNamespaceEnd(self):
    """Get closing root namespace declarations.
    """
    c = Code()
    for namespace in reversed(self._root_namespace):
      c.Append('}  // %s' % namespace)
    return c

  def GetNamespaceStart(self):
    """Get opening self._namespace namespace declaration.
    """
    return Code().Append('namespace %s {' %
        self.GetCppNamespaceName(self._namespace))

  def GetNamespaceEnd(self):
    """Get closing self._namespace namespace declaration.
    """
    return Code().Append('}  // %s' %
        self.GetCppNamespaceName(self._namespace))

  def GetEnumNoneValue(self, prop):
    """Gets the enum value in the given model.Property indicating no value has
    been set.
    """
    return '%s_NONE' % prop.unix_name.upper()

  def GetEnumValue(self, prop, enum_value):
    """Gets the enum value of the given model.Property of the given type.

    e.g VAR_STRING
    """
    return '%s_%s' % (
        prop.unix_name.upper(), cpp_util.Classname(enum_value.upper()))

  def GetChoicesEnumType(self, prop):
    """Gets the type of the enum for the given model.Property.

    e.g VarType
    """
    return cpp_util.Classname(prop.name) + 'Type'

  def GetType(self, prop, pad_for_generics=False, wrap_optional=False):
    return self._GetTypeHelper(prop, pad_for_generics, wrap_optional)

  def GetCompiledType(self, prop, pad_for_generics=False, wrap_optional=False):
    return self._GetTypeHelper(prop, pad_for_generics, wrap_optional,
                               use_compiled_type=True)

  def _GetTypeHelper(self, prop, pad_for_generics=False, wrap_optional=False,
                     use_compiled_type=False):
    """Translates a model.Property into its C++ type.

    If REF types from different namespaces are referenced, will resolve
    using self._type_namespaces.

    Use pad_for_generics when using as a generic to avoid operator ambiguity.

    Use wrap_optional to wrap the type in a scoped_ptr<T> if the Property is
    optional.

    Use use_compiled_type when converting from prop.type_ to prop.compiled_type.
    """
    cpp_type = None
    type_ = prop.type_ if not use_compiled_type else prop.compiled_type

    if type_ == PropertyType.REF:
      dependency_namespace = self._ResolveTypeNamespace(prop.ref_type)
      if not dependency_namespace:
        raise KeyError('Cannot find referenced type: %s' % prop.ref_type)
      if self._namespace != dependency_namespace:
        cpp_type = '%s::%s' % (self._cpp_namespaces[dependency_namespace],
            schema_util.StripSchemaNamespace(prop.ref_type))
      else:
        cpp_type = schema_util.StripSchemaNamespace(prop.ref_type)
    elif type_ == PropertyType.BOOLEAN:
      cpp_type = 'bool'
    elif type_ == PropertyType.INTEGER:
      cpp_type = 'int'
    elif type_ == PropertyType.INT64:
      cpp_type = 'int64'
    elif type_ == PropertyType.DOUBLE:
      cpp_type = 'double'
    elif type_ == PropertyType.STRING:
      cpp_type = 'std::string'
    elif type_ == PropertyType.ENUM:
      cpp_type = cpp_util.Classname(prop.name)
    elif type_ == PropertyType.ADDITIONAL_PROPERTIES:
      cpp_type = 'base::DictionaryValue'
    elif type_ == PropertyType.ANY:
      cpp_type = any_helper.ANY_CLASS
    elif type_ == PropertyType.OBJECT:
      cpp_type = cpp_util.Classname(prop.name)
    elif type_ == PropertyType.FUNCTION:
      # Functions come into the json schema compiler as empty objects. We can
      # record these as empty DictionaryValue's so that we know if the function
      # was passed in or not.
      cpp_type = 'base::DictionaryValue'
    elif type_ == PropertyType.ARRAY:
      item_type = prop.item_type
      if item_type.type_ == PropertyType.REF:
        item_type = self.GetReferencedProperty(item_type)
      if item_type.type_ in (
          PropertyType.REF, PropertyType.ANY, PropertyType.OBJECT):
        cpp_type = 'std::vector<linked_ptr<%s> > '
      else:
        cpp_type = 'std::vector<%s> '
      cpp_type = cpp_type % self.GetType(
          prop.item_type, pad_for_generics=True)
    elif type_ == PropertyType.BINARY:
      cpp_type = 'std::string'
    else:
      raise NotImplementedError(type_)

    # Enums aren't wrapped because C++ won't allow it. Optional enums have a
    # NONE value generated instead.
    if wrap_optional and prop.optional and type_ != PropertyType.ENUM:
      cpp_type = 'scoped_ptr<%s> ' % cpp_type
    if pad_for_generics:
      return cpp_type
    return cpp_type.strip()

  def GenerateForwardDeclarations(self):
    """Returns the forward declarations for self._namespace.

    Use after GetRootNamespaceStart. Assumes all namespaces are relative to
    self._root_namespace.
    """
    c = Code()
    namespace_type_dependencies = self._NamespaceTypeDependencies()
    for namespace in sorted(namespace_type_dependencies.keys(),
                            key=lambda ns: ns.name):
      c.Append('namespace %s {' % namespace.name)
      for type_ in sorted(namespace_type_dependencies[namespace],
                          key=schema_util.StripSchemaNamespace):
        type_name = schema_util.StripSchemaNamespace(type_)
        if namespace.types[type_].type_ == PropertyType.STRING:
          c.Append('typedef std::string %s;' % type_name)
        elif namespace.types[type_].type_ == PropertyType.ARRAY:
          c.Append('typedef std::vector<%(item_type)s> %(name)s;')
          c.Substitute({
            'name': type_name,
            'item_type': self.GetType(namespace.types[type_].item_type,
                                      wrap_optional=True)})
        else:
          c.Append('struct %s;' % type_name)
      c.Append('}')
    c.Concat(self.GetNamespaceStart())
    for (name, type_) in self._namespace.types.items():
      if not type_.functions and type_.type_ == PropertyType.OBJECT:
        c.Append('struct %s;' % schema_util.StripSchemaNamespace(name))
    c.Concat(self.GetNamespaceEnd())
    return c

  def GenerateIncludes(self):
    """Returns the #include lines for self._namespace.
    """
    c = Code()
    for header in sorted(
        ['%s/%s.h' % (dependency.source_file_dir,
                      self._cpp_namespaces[dependency])
         for dependency in self._NamespaceTypeDependencies().keys()]):
      c.Append('#include "%s"' % header)
    c.Append('#include "base/string_number_conversions.h"')

    if self._namespace.events:
      c.Append('#include "base/json/json_writer.h"')
    return c

  def _ResolveTypeNamespace(self, ref_type):
    """Resolves a type, which must be explicitly qualified, to its enclosing
    namespace.
    """
    if ref_type in self._type_namespaces:
      return self._type_namespaces[ref_type]
    raise KeyError(('Cannot resolve type: %s.' % ref_type) +
        'Maybe it needs a namespace prefix if it comes from another namespace?')
    return None

  def GetReferencedProperty(self, prop):
    """Returns the property a property of type REF is referring to.

    If the property passed in is not of type PropertyType.REF, it will be
    returned unchanged.
    """
    if prop.type_ != PropertyType.REF:
      return prop
    return self._ResolveTypeNamespace(prop.ref_type).types.get(prop.ref_type,
        None)

  def _NamespaceTypeDependencies(self):
    """Returns a dict containing a mapping of model.Namespace to the C++ type
    of type dependencies for self._namespace.
    """
    dependencies = set()
    for function in self._namespace.functions.values():
      for param in function.params:
        dependencies |= self._PropertyTypeDependencies(param)
      if function.callback:
        for param in function.callback.params:
          dependencies |= self._PropertyTypeDependencies(param)
    for type_ in self._namespace.types.values():
      for prop in type_.properties.values():
        dependencies |= self._PropertyTypeDependencies(prop)

    dependency_namespaces = dict()
    for dependency in dependencies:
      namespace = self._ResolveTypeNamespace(dependency)
      if namespace != self._namespace:
        dependency_namespaces.setdefault(namespace, [])
        dependency_namespaces[namespace].append(dependency)
    return dependency_namespaces

  def _PropertyTypeDependencies(self, prop):
    """Recursively gets all the type dependencies of a property.
    """
    deps = set()
    if prop:
      if prop.type_ == PropertyType.REF:
        deps.add(prop.ref_type)
      elif prop.type_ == PropertyType.ARRAY:
        deps = self._PropertyTypeDependencies(prop.item_type)
      elif prop.type_ == PropertyType.OBJECT:
        for p in prop.properties.values():
          deps |= self._PropertyTypeDependencies(p)
    return deps

  def GeneratePropertyValues(self, property, line, nodoc=False):
    """Generates the Code to display all value-containing properties.
    """
    c = Code()
    if not nodoc:
      c.Comment(property.description)

    if property.has_value:
      c.Append(line % {
          "type": self._GetPrimitiveType(property.type_),
          "name": property.name,
          "value": property.value
        })
    else:
      has_child_code = False
      c.Sblock('namespace %s {' % property.name)
      for child_property in property.properties.values():
        child_code = self.GeneratePropertyValues(
            child_property,
            line,
            nodoc=nodoc)
        if child_code:
          has_child_code = True
          c.Concat(child_code)
      c.Eblock('}  // namespace %s' % property.name)
      if not has_child_code:
        c = None
    return c

  def _GetPrimitiveType(self, type_):
    """Like |GetType| but only accepts and returns C++ primitive types.
    """
    if type_ == PropertyType.BOOLEAN:
      return 'bool'
    elif type_ == PropertyType.INTEGER:
      return 'int'
    elif type_ == PropertyType.DOUBLE:
      return 'double'
    elif type_ == PropertyType.STRING:
      return 'char*'
    raise Exception(type_ + ' is not primitive')
