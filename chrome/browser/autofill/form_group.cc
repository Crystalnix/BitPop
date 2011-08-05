// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "chrome/browser/autofill/form_group.h"

#include <iterator>

const string16 FormGroup::Label() const { return string16(); }

bool FormGroup::operator!=(const FormGroup& form_group) const {
  FieldTypeSet a, b, symmetric_difference;
  GetNonEmptyTypes(&a);
  form_group.GetNonEmptyTypes(&b);
  std::set_symmetric_difference(
      a.begin(), a.end(),
      b.begin(), b.end(),
      std::inserter(symmetric_difference, symmetric_difference.begin()));

  if (!symmetric_difference.empty())
    return true;

  return (!IntersectionOfTypesHasEqualValues(form_group));
}

bool FormGroup::IsSubsetOf(const FormGroup& form_group) const {
  FieldTypeSet types;
  GetNonEmptyTypes(&types);

  for (FieldTypeSet::const_iterator iter = types.begin(); iter != types.end();
       ++iter) {
    if (StringToLowerASCII(GetInfo(*iter)) !=
          StringToLowerASCII(form_group.GetInfo(*iter)))
      return false;
  }

  return true;
}

bool FormGroup::IntersectionOfTypesHasEqualValues(
    const FormGroup& form_group) const {
  FieldTypeSet a, b, intersection;
  GetNonEmptyTypes(&a);
  form_group.GetNonEmptyTypes(&b);
  std::set_intersection(a.begin(), a.end(),
                        b.begin(), b.end(),
                        std::inserter(intersection, intersection.begin()));

  // An empty intersection can't have equal values.
  if (intersection.empty())
    return false;

  for (FieldTypeSet::const_iterator iter = intersection.begin();
       iter != intersection.end(); ++iter) {
    if (StringToLowerASCII(GetInfo(*iter)) !=
          StringToLowerASCII(form_group.GetInfo(*iter)))
      return false;
  }

  return true;
}

void FormGroup::MergeWith(const FormGroup& form_group) {
  FieldTypeSet a, b, intersection;
  GetNonEmptyTypes(&a);
  form_group.GetNonEmptyTypes(&b);
  std::set_difference(b.begin(), b.end(),
                      a.begin(), a.end(),
                      std::inserter(intersection, intersection.begin()));

  for (FieldTypeSet::const_iterator iter = intersection.begin();
       iter != intersection.end(); ++iter) {
    SetInfo(*iter, form_group.GetInfo(*iter));
  }
}

void FormGroup::OverwriteWith(const FormGroup& form_group) {
  FieldTypeSet a;;
  form_group.GetNonEmptyTypes(&a);

  for (FieldTypeSet::const_iterator iter = a.begin(); iter != a.end(); ++iter) {
    SetInfo(*iter, form_group.GetInfo(*iter));
  }
}
