// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_TAB_BASE_ANDROID_IMPL_H_
#define CHROME_BROWSER_ANDROID_TAB_BASE_ANDROID_IMPL_H_

#include <jni.h>

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/android/tab_android.h"

namespace browser_sync {
class SyncedTabDelegate;
}

namespace chrome {
namespace android {
class ChromeWebContentsDelegateAndroid;
}
}

namespace content {
class WebContents;
}

namespace ui {
class WindowAndroid;
}

class TabBaseAndroidImpl : public TabAndroid {
 public:
  TabBaseAndroidImpl(JNIEnv* env,
                     jobject obj,
                     content::WebContents* web_contents,
                     ui::WindowAndroid* window_android);
  void Destroy(JNIEnv* env, jobject obj);

  // --------------------------------------------------------------------------
  // TabAndroid Methods
  // --------------------------------------------------------------------------
  virtual browser_sync::SyncedTabDelegate* GetSyncedTabDelegate() OVERRIDE;

  virtual void OnReceivedHttpAuthRequest(jobject auth_handler,
                                         const string16& host,
                                         const string16& realm) OVERRIDE;
  virtual void ShowContextMenu(
      const content::ContextMenuParams& params) OVERRIDE;

  virtual void ShowCustomContextMenu(
      const content::ContextMenuParams& params,
      const base::Callback<void(int)>& callback) OVERRIDE;

  virtual void AddShortcutToBookmark(const GURL& url,
                                     const string16& title,
                                     const SkBitmap& skbitmap,
                                     int r_value,
                                     int g_value,
                                     int b_value) OVERRIDE;
  virtual void EditBookmark(int64 node_id, bool is_folder) OVERRIDE;

  virtual void RunExternalProtocolDialog(const GURL& url) OVERRIDE;

  // Register the Tab's native methods through JNI.
  static bool RegisterTabBaseAndroidImpl(JNIEnv* env);

  // --------------------------------------------------------------------------
  // Methods called from Java via JNI
  // --------------------------------------------------------------------------
  void InitWebContentsDelegate(JNIEnv* env,
                               jobject obj,
                               jobject web_contents_delegate);

  base::android::ScopedJavaLocalRef<jstring> FixupUrl(JNIEnv* env,
                                                      jobject obj,
                                                      jstring url);

 protected:
  virtual ~TabBaseAndroidImpl();

 private:
  scoped_ptr<content::WebContents> web_contents_;
  scoped_ptr<chrome::android::ChromeWebContentsDelegateAndroid>
          web_contents_delegate_;

  DISALLOW_COPY_AND_ASSIGN(TabBaseAndroidImpl);
};

#endif  // CHROME_BROWSER_ANDROID_TAB_BASE_ANDROID_IMPL_H_
