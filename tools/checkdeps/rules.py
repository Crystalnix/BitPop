# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Base classes to represent dependency rules, used by checkdeps.py"""


class Rule(object):
  """Specifies a single rule for an include, which can be one of
  ALLOW, DISALLOW and TEMP_ALLOW.
  """

  # These are the prefixes used to indicate each type of rule. These
  # are also used as values for self.allow to indicate which type of
  # rule this is.
  ALLOW = '+'
  DISALLOW = '-'
  TEMP_ALLOW = '!'

  def __init__(self, allow, directory, source):
    self.allow = allow
    self._dir = directory
    self._source = source

  def __str__(self):
    return '"%s%s" from %s.' % (self.allow, self._dir, self._source)

  def ParentOrMatch(self, other):
    """Returns true if the input string is an exact match or is a parent
    of the current rule. For example, the input "foo" would match "foo/bar"."""
    return self._dir == other or self._dir.startswith(other + '/')

  def ChildOrMatch(self, other):
    """Returns true if the input string would be covered by this rule. For
    example, the input "foo/bar" would match the rule "foo"."""
    return self._dir == other or other.startswith(self._dir + '/')


class SpecificRule(Rule):
  """A rule that has a specific reason not related to directory or
  source, for failing.
  """

  def __init__(self, reason):
    super(SpecificRule, self).__init__(Rule.DISALLOW, '', '')
    self._reason = reason

  def __str__(self):
    return self._reason


def ParseRuleString(rule_string, source):
  """Returns a tuple of a boolean indicating whether the directory is an allow
  rule, and a string holding the directory name.
  """
  if not rule_string:
    raise Exception('The rule string "%s" is empty\nin %s' %
                    (rule_string, source))

  if not rule_string[0] in [Rule.ALLOW, Rule.DISALLOW, Rule.TEMP_ALLOW]:
    raise Exception(
      'The rule string "%s" does not begin with a "+", "-" or "!".' %
      rule_string)

  return (rule_string[0], rule_string[1:])


class Rules(object):
  def __init__(self):
    """Initializes the current rules with an empty rule list."""
    self._rules = []

  def __str__(self):
    return 'Rules = [\n%s]' % '\n'.join(' %s' % x for x in self._rules)

  def AddRule(self, rule_string, source):
    """Adds a rule for the given rule string.

    Args:
      rule_string: The include_rule string read from the DEPS file to apply.
      source: A string representing the location of that string (filename, etc.)
              so that we can give meaningful errors.
    """
    (add_rule, rule_dir) = ParseRuleString(rule_string, source)
    # Remove any existing rules or sub-rules that apply. For example, if we're
    # passed "foo", we should remove "foo", "foo/bar", but not "foobar".
    self._rules = [x for x in self._rules if not x.ParentOrMatch(rule_dir)]
    self._rules.insert(0, Rule(add_rule, rule_dir, source))

  def RuleApplyingTo(self, allowed_dir):
    """Returns the rule that applies to 'allowed_dir'."""
    for rule in self._rules:
      if rule.ChildOrMatch(allowed_dir):
        return rule
    return SpecificRule('no rule applying.')