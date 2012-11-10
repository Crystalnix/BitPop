// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_HUNG_RENDERER_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_HUNG_RENDERER_VIEW_H_

#include "base/memory/scoped_vector.h"
#include "chrome/browser/ui/tab_contents/tab_contents.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/views/controls/button/text_button.h"
#include "ui/views/controls/table/group_table_model.h"
#include "ui/views/controls/table/group_table_view.h"
#include "ui/views/window/dialog_delegate.h"

using content::RenderViewHost;
using content::WebContents;

// Provides functionality to display information about a hung renderer.
class HungPagesTableModel : public views::GroupTableModel {
 public:
  // The Delegate is notified any time a WebContents the model is listening to
  // is destroyed.
  class Delegate {
   public:
    virtual void TabDestroyed() = 0;

   protected:
    virtual ~Delegate() {}
  };

  explicit HungPagesTableModel(Delegate* delegate);
  virtual ~HungPagesTableModel();

  void InitForWebContents(WebContents* hung_contents);

  // Returns the first RenderProcessHost, or NULL if there aren't any
  // WebContents.
  content::RenderProcessHost* GetRenderProcessHost();

  // Returns the first RenderViewHost, or NULL if there aren't any WebContents.
  RenderViewHost* GetRenderViewHost();

  // Overridden from views::GroupTableModel:
  virtual int RowCount() OVERRIDE;
  virtual string16 GetText(int row, int column_id) OVERRIDE;
  virtual gfx::ImageSkia GetIcon(int row) OVERRIDE;
  virtual void SetObserver(ui::TableModelObserver* observer) OVERRIDE;
  virtual void GetGroupRangeForItem(int item, views::GroupRange* range)
      OVERRIDE;

 private:
  // Used to track a single WebContents. If the WebContents is destroyed
  // TabDestroyed() is invoked on the model.
  class WebContentsObserverImpl : public content::WebContentsObserver {
   public:
    WebContentsObserverImpl(HungPagesTableModel* model,
                            TabContents* tab);

    WebContents* web_contents() const {
      return content::WebContentsObserver::web_contents();
    }

    FaviconTabHelper* favicon_tab_helper() {
      return tab_->favicon_tab_helper();
    }

    // WebContentsObserver overrides:
    virtual void RenderViewGone(base::TerminationStatus status) OVERRIDE;
    virtual void WebContentsDestroyed(WebContents* tab) OVERRIDE;

   private:
    HungPagesTableModel* model_;
    TabContents* tab_;

    DISALLOW_COPY_AND_ASSIGN(WebContentsObserverImpl);
  };

  // Invoked when a WebContents is destroyed. Cleans up |tab_observers_| and
  // notifies the observer and delegate.
  void TabDestroyed(WebContentsObserverImpl* tab);

  typedef ScopedVector<WebContentsObserverImpl> TabObservers;
  TabObservers tab_observers_;

  ui::TableModelObserver* observer_;
  Delegate* delegate_;

  DISALLOW_COPY_AND_ASSIGN(HungPagesTableModel);
};

// This class displays a dialog which contains information about a hung
// renderer process.
class HungRendererDialogView : public views::DialogDelegateView,
                               public views::ButtonListener,
                               public HungPagesTableModel::Delegate {
 public:
  // Factory function for creating an instance of the HungRendererDialogView
  // class. At any given point only one instance can be active.
  static HungRendererDialogView* Create();

  // Returns a pointer to the singleton instance if any.
  static HungRendererDialogView* GetInstance();

  // Platform specific function to kill the renderer process identified by the
  // handle passed in.
  static void KillRendererProcess(base::ProcessHandle process_handle);

  // Returns true if the frame is in the foreground.
  static bool IsFrameActive(WebContents* contents);

  virtual void ShowForWebContents(WebContents* contents);
  virtual void EndForWebContents(WebContents* contents);

  // views::DialogDelegateView overrides:
  virtual string16 GetWindowTitle() const OVERRIDE;
  virtual void WindowClosing() OVERRIDE;
  virtual int GetDialogButtons() const OVERRIDE;
  virtual string16 GetDialogButtonLabel(ui::DialogButton button) const OVERRIDE;
  virtual views::View* GetExtraView() OVERRIDE;
  virtual bool Accept(bool window_closing)  OVERRIDE;
  virtual views::View* GetContentsView()  OVERRIDE;

  // views::ButtonListener overrides:
  virtual void ButtonPressed(views::Button* sender,
                             const views::Event& event) OVERRIDE;

  // HungPagesTableModel::Delegate overrides:
  virtual void TabDestroyed() OVERRIDE;

 protected:
  HungRendererDialogView();
  virtual ~HungRendererDialogView();

  // views::View overrides:
  virtual void ViewHierarchyChanged(bool is_add,
                                    views::View* parent,
                                    views::View* child) OVERRIDE;

  static HungRendererDialogView* g_instance_;

 private:
  // Initialize the controls in this dialog.
  void Init();
  void CreateKillButtonView();

  // Returns the bounds the dialog should be displayed at to be meaningfully
  // associated with the specified WebContents.
  gfx::Rect GetDisplayBounds(WebContents* contents);

  static void InitClass();

  // Controls within the dialog box.
  views::GroupTableView* hung_pages_table_;

  // The button we insert into the ClientView to kill the errant process. This
  // is parented to a container view that uses a grid layout to align it
  // properly.
  views::TextButton* kill_button_;
  views::View* kill_button_container_;

  // The model that provides the contents of the table that shows a list of
  // pages affected by the hang.
  scoped_ptr<HungPagesTableModel> hung_pages_table_model_;

  // Whether or not we've created controls for ourself.
  bool initialized_;

  // An amusing icon image.
  static gfx::ImageSkia* frozen_icon_;

  DISALLOW_COPY_AND_ASSIGN(HungRendererDialogView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_HUNG_RENDERER_VIEW_H_
