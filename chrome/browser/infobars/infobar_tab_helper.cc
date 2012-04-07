// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/infobars/infobar_tab_helper.h"

#include "chrome/browser/infobars/infobar.h"
#include "chrome/browser/infobars/infobar_delegate.h"
#include "chrome/browser/tab_contents/insecure_content_infobar_delegate.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/render_messages.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"

using content::NavigationController;
using content::WebContents;

InfoBarTabHelper::InfoBarTabHelper(WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      infobars_enabled_(true) {
  DCHECK(web_contents);
}

InfoBarTabHelper::~InfoBarTabHelper() {
  // Destroy all remaining InfoBars.  It's important to not animate here so that
  // we guarantee that we'll delete all delegates before we do anything else.
  //
  // TODO(pkasting): If there is no InfoBarContainer, this leaks all the
  // InfoBarDelegates.  This will be fixed once we call CloseSoon() directly on
  // Infobars.
  RemoveAllInfoBars(false);
}

void InfoBarTabHelper::AddInfoBar(InfoBarDelegate* delegate) {
  if (!infobars_enabled_) {
    delegate->InfoBarClosed();
    return;
  }

  for (size_t i = 0; i < infobars_.size(); ++i) {
    if (GetInfoBarDelegateAt(i)->EqualsDelegate(delegate)) {
      delegate->InfoBarClosed();
      return;
    }
  }

  // TODO(pkasting): Consider removing InfoBarTabHelper arg from delegate
  // constructors and instead using a setter from here.
  infobars_.push_back(delegate);
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_TAB_CONTENTS_INFOBAR_ADDED,
      content::Source<InfoBarTabHelper>(this),
      content::Details<InfoBarAddedDetails>(delegate));

  // Add ourselves as an observer for navigations the first time a delegate is
  // added. We use this notification to expire InfoBars that need to expire on
  // page transitions.
  if (infobars_.size() == 1) {
    registrar_.Add(
        this, content::NOTIFICATION_NAV_ENTRY_COMMITTED,
        content::Source<NavigationController>(
            &web_contents()->GetController()));
  }
}

void InfoBarTabHelper::RemoveInfoBar(InfoBarDelegate* delegate) {
  RemoveInfoBarInternal(delegate, true);
}

void InfoBarTabHelper::ReplaceInfoBar(InfoBarDelegate* old_delegate,
                                      InfoBarDelegate* new_delegate) {
  if (!infobars_enabled_) {
    AddInfoBar(new_delegate);  // Deletes the delegate.
    return;
  }

  size_t i;
  for (i = 0; i < infobars_.size(); ++i) {
    if (GetInfoBarDelegateAt(i) == old_delegate)
      break;
  }
  DCHECK_LT(i, infobars_.size());

  infobars_.insert(infobars_.begin() + i, new_delegate);

  old_delegate->clear_owner();
  InfoBarReplacedDetails replaced_details(old_delegate, new_delegate);
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_TAB_CONTENTS_INFOBAR_REPLACED,
      content::Source<InfoBarTabHelper>(this),
      content::Details<InfoBarReplacedDetails>(&replaced_details));

  infobars_.erase(infobars_.begin() + i + 1);
}

InfoBarDelegate* InfoBarTabHelper::GetInfoBarDelegateAt(size_t index) {
  return infobars_[index];
}

void InfoBarTabHelper::RemoveInfoBarInternal(InfoBarDelegate* delegate,
                                             bool animate) {
  if (!infobars_enabled_) {
    DCHECK(infobars_.empty());
    return;
  }

  size_t i;
  for (i = 0; i < infobars_.size(); ++i) {
    if (GetInfoBarDelegateAt(i) == delegate)
      break;
  }
  DCHECK_LT(i, infobars_.size());
  InfoBarDelegate* infobar = infobars_[i];

  infobar->clear_owner();
  InfoBarRemovedDetails removed_details(infobar, animate);
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_TAB_CONTENTS_INFOBAR_REMOVED,
      content::Source<InfoBarTabHelper>(this),
      content::Details<InfoBarRemovedDetails>(&removed_details));

  infobars_.erase(infobars_.begin() + i);
  // Remove ourselves as an observer if we are tracking no more InfoBars.
  if (infobars_.empty()) {
    registrar_.Remove(
        this, content::NOTIFICATION_NAV_ENTRY_COMMITTED,
        content::Source<NavigationController>(
            &web_contents()->GetController()));
  }
}

void InfoBarTabHelper::RemoveAllInfoBars(bool animate) {
  while (!infobars_.empty())
    RemoveInfoBarInternal(GetInfoBarDelegateAt(infobar_count() - 1), animate);
}

void InfoBarTabHelper::OnDidBlockDisplayingInsecureContent() {
  // At most one infobar and do not supersede the stronger running content bar.
  for (size_t i = 0; i < infobars_.size(); ++i) {
    if (GetInfoBarDelegateAt(i)->AsInsecureContentInfoBarDelegate())
      return;
  }
  AddInfoBar(new InsecureContentInfoBarDelegate(this,
      InsecureContentInfoBarDelegate::DISPLAY));
}

void InfoBarTabHelper::OnDidBlockRunningInsecureContent() {
  // At most one infobar superseding any weaker displaying content bar.
  for (size_t i = 0; i < infobars_.size(); ++i) {
    InsecureContentInfoBarDelegate* delegate =
        GetInfoBarDelegateAt(i)->AsInsecureContentInfoBarDelegate();
    if (delegate) {
      if (delegate->type() != InsecureContentInfoBarDelegate::RUN) {
        ReplaceInfoBar(delegate, new InsecureContentInfoBarDelegate(
            this, InsecureContentInfoBarDelegate::RUN));
      }
      return;
    }
  }
  AddInfoBar(new InsecureContentInfoBarDelegate(this,
      InsecureContentInfoBarDelegate::RUN));
}

void InfoBarTabHelper::RenderViewGone(base::TerminationStatus status) {
  RemoveAllInfoBars(true);
}

bool InfoBarTabHelper::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(InfoBarTabHelper, message)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_DidBlockDisplayingInsecureContent,
                        OnDidBlockDisplayingInsecureContent)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_DidBlockRunningInsecureContent,
                        OnDidBlockRunningInsecureContent)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void InfoBarTabHelper::Observe(int type,
                               const content::NotificationSource& source,
                               const content::NotificationDetails& details) {
  switch (type) {
    case content::NOTIFICATION_NAV_ENTRY_COMMITTED: {
      DCHECK(&web_contents()->GetController() ==
             content::Source<NavigationController>(source).ptr());

      content::LoadCommittedDetails& committed_details =
          *(content::Details<content::LoadCommittedDetails>(details).ptr());

      // NOTE: It is not safe to change the following code to count upwards or
      // use iterators, as the RemoveInfoBar() call synchronously modifies our
      // delegate list.
      for (size_t i = infobars_.size(); i > 0; --i) {
        InfoBarDelegate* delegate = GetInfoBarDelegateAt(i - 1);
        if (delegate->ShouldExpire(committed_details))
          RemoveInfoBar(delegate);
      }

      break;
    }
    default:
      NOTREACHED();
  }
}
