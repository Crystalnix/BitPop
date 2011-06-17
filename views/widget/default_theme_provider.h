// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VIEWS_DEFAULT_THEME_PROVIDER_H_
#define VIEWS_DEFAULT_THEME_PROVIDER_H_
#pragma once

#include <vector>

#include "base/basictypes.h"
#include "ui/base/theme_provider.h"

class Profile;

namespace ui {
class ResourceBundle;
}
using ui::ResourceBundle;

namespace views {

class DefaultThemeProvider : public ui::ThemeProvider {
 public:
  DefaultThemeProvider();
  virtual ~DefaultThemeProvider();

  // Overridden from ui::ThemeProvider.
  virtual void Init(Profile* profile);
  virtual SkBitmap* GetBitmapNamed(int id) const;
  virtual SkColor GetColor(int id) const;
  virtual bool GetDisplayProperty(int id, int* result) const;
  virtual bool ShouldUseNativeFrame() const;
  virtual bool HasCustomImage(int id) const;
  virtual RefCountedMemory* GetRawData(int id) const;

 private:
  DISALLOW_COPY_AND_ASSIGN(DefaultThemeProvider);
};

}  // namespace views

#endif  // VIEWS_DEFAULT_THEME_PROVIDER_H_
