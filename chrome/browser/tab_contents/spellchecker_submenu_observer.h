// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_TAB_CONTENTS_SPELLCHECKER_SUBMENU_OBSERVER_H_
#define CHROME_BROWSER_TAB_CONTENTS_SPELLCHECKER_SUBMENU_OBSERVER_H_
#pragma once

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/tab_contents/render_view_context_menu_observer.h"
#include "ui/base/models/simple_menu_model.h"

class RenderViewContextMenuProxy;

// A class that implements the 'spell-checker options' submenu. This class
// creates the submenu, add it to the parent menu, and handles events.
class SpellCheckerSubMenuObserver : public RenderViewContextMenuObserver {
 public:
  SpellCheckerSubMenuObserver(RenderViewContextMenuProxy* proxy,
                              ui::SimpleMenuModel::Delegate* delegate,
                              int group);
  virtual ~SpellCheckerSubMenuObserver();

  // RenderViewContextMenuObserver implementation.
  virtual void InitMenu(const ContextMenuParams& params) OVERRIDE;
  virtual bool IsCommandIdSupported(int command_id) OVERRIDE;
  virtual bool IsCommandIdChecked(int command_id) OVERRIDE;
  virtual bool IsCommandIdEnabled(int command_id) OVERRIDE;
  virtual void ExecuteCommand(int command_id) OVERRIDE;

 private:
  // The interface for adding a submenu to the parent.
  RenderViewContextMenuProxy* proxy_;

  // The submenu of the 'spell-checker options'. This class adds items to this
  // submenu and add it to the parent menu.
  ui::SimpleMenuModel submenu_model_;

#if defined(OS_MACOSX)
  // On OS X we use the Cocoa spellchecker and try to match the native context
  // menu.
  bool check_spelling_while_typing_;
#else
  // Hunspell spelling submenu.

  // Whether spellchecking is enabled in the focused element or not.
  bool spellcheck_enabled_;

  // Whether we integrate the spelling service to Chrome.
  bool integrate_spelling_service_;

  // The radio items representing languages available for spellchecking.
  int language_group_;
  int language_selected_;
  std::vector<std::string> languages_;
#endif  // !OS_MACOSX

  DISALLOW_COPY_AND_ASSIGN(SpellCheckerSubMenuObserver);
};

#endif  // CHROME_BROWSER_TAB_CONTENTS_SPELLCHECKER_SUBMENU_OBSERVER_H_
