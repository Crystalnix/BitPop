// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_TAB_CONTENTS_TAB_CONTENTS_OBSERVER_H_
#define CONTENT_BROWSER_TAB_CONTENTS_TAB_CONTENTS_OBSERVER_H_

#include "content/browser/tab_contents/navigation_controller.h"
#include "content/common/page_transition_types.h"
#include "ipc/ipc_channel.h"
#include "webkit/glue/window_open_disposition.h"

struct ViewHostMsg_FrameNavigate_Params;

// An observer API implemented by classes which are interested in various page
// load events from TabContents.  They also get a chance to filter IPC messages.
class TabContentsObserver : public IPC::Channel::Listener,
                            public IPC::Message::Sender {
 public:
  // Use this as a member variable in a class that uses the emptry constructor
  // version of this interface.
  class Registrar {
   public:
    explicit Registrar(TabContentsObserver* observer);
    ~Registrar();

    // Call this to start observing a tab.  Passing in NULL resets it.
    // This can only be used to watch one tab at a time.  If you call this and
    // you're already observing another tab, the old tab won't be observed
    // afterwards.
    void Observe(TabContents* tab);

   private:
    TabContentsObserver* observer_;
    TabContents* tab_;

    DISALLOW_COPY_AND_ASSIGN(Registrar);
  };

  virtual void NavigateToPendingEntry();

  virtual void DidNavigateMainFramePostCommit(
      const NavigationController::LoadCommittedDetails& details,
      const ViewHostMsg_FrameNavigate_Params& params);
  virtual void DidNavigateAnyFramePostCommit(
      const NavigationController::LoadCommittedDetails& details,
      const ViewHostMsg_FrameNavigate_Params& params);
  virtual void DidStartProvisionalLoadForFrame(int64 frame_id,
                                               bool is_main_frame,
                                               const GURL& validated_url,
                                               bool is_error_page);
  virtual void ProvisionalChangeToMainFrameUrl(const GURL& url);
  virtual void DidCommitProvisionalLoadForFrame(
      int64 frame_id,
      bool is_main_frame,
      const GURL& url,
      PageTransition::Type transition_type);
  virtual void DidFailProvisionalLoad(int64 frame_id,
                                      bool is_main_frame,
                                      const GURL& validated_url,
                                      int error_code);
  virtual void DocumentLoadedInFrame(int64 frame_id);
  virtual void DidFinishLoad(int64 frame_id);

  virtual void DidStartLoading();
  virtual void DidStopLoading();
  virtual void RenderViewGone();
  virtual void StopNavigation();

  virtual void DidOpenURL(const GURL& url,
                          const GURL& referrer,
                          WindowOpenDisposition disposition,
                          PageTransition::Type transition);

#if 0
  // For unifying with delegate...

  // Notifies the delegate that this contents is starting or is done loading
  // some resource. The delegate should use this notification to represent
  // loading feedback. See TabContents::is_loading()
  virtual void LoadingStateChanged(TabContents* contents) { }
  // Called to inform the delegate that the tab content's navigation state
  // changed. The |changed_flags| indicates the parts of the navigation state
  // that have been updated, and is any combination of the
  // |TabContents::InvalidateTypes| bits.
  virtual void NavigationStateChanged(const TabContents* source,
                                      unsigned changed_flags) { }
#endif

 protected:
  // Use this constructor when the object is tied to a single TabContents for
  // its entire lifetime.
  explicit TabContentsObserver(TabContents* tab_contents);

  // Use this constructor when the object wants to observe a TabContents for
  // part of its lifetime.  It can use a TabContentsRegistrar member variable
  // to start and stop observing.
  TabContentsObserver();

  virtual ~TabContentsObserver();

  // Invoked when the TabContents is being destroyed. Gives subclasses a chance
  // to cleanup. At the time this is invoked |tab_contents()| returns NULL.
  // It is safe to delete 'this' from here.
  virtual void TabContentsDestroyed(TabContents* tab);

  // IPC::Channel::Listener implementation.
  virtual bool OnMessageReceived(const IPC::Message& message);

  // IPC::Message::Sender implementation.
  virtual bool Send(IPC::Message* message);

  TabContents* tab_contents() const { return tab_contents_; }
  int routing_id() const;

 protected:
  friend class Registrar;

  void SetTabContents(TabContents* tab_contents);

 private:
  friend class TabContents;

  // Invoked from TabContents. Invokes TabContentsDestroyed and NULL out
  // |tab_contents_|.
  void TabContentsDestroyed();

  TabContents* tab_contents_;

  // The routing ID of the associated TabContents.
  int routing_id_;

  DISALLOW_COPY_AND_ASSIGN(TabContentsObserver);
};

#endif  // CONTENT_BROWSER_TAB_CONTENTS_TAB_CONTENTS_OBSERVER_H_
