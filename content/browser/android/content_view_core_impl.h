// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_ANDROID_CONTENT_VIEW_CORE_IMPL_H_
#define CONTENT_BROWSER_ANDROID_CONTENT_VIEW_CORE_IMPL_H_

#include <vector>

#include "base/android/jni_helper.h"
#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/i18n/rtl.h"
#include "base/memory/scoped_ptr.h"
#include "base/process.h"
#include "content/browser/renderer_host/render_widget_host_view_android.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/android/content_view_core.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "googleurl/src/gurl.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebInputEvent.h"
#include "ui/gfx/rect.h"

struct WebMenuItem;

namespace content {
class ContentViewClient;
class RenderWidgetHostViewAndroid;

// TODO(jrg): this is a shell.  Upstream the rest.
class ContentViewCoreImpl : public ContentViewCore,
                            public NotificationObserver {
 public:
  ContentViewCoreImpl(JNIEnv* env,
                      jobject obj,
                      WebContents* web_contents);

  // ContentViewCore overrides
  virtual void Destroy(JNIEnv* env, jobject obj) OVERRIDE;

  // --------------------------------------------------------------------------
  // Methods called from Java via JNI
  // --------------------------------------------------------------------------

  // Notifies the ContentViewCore that items were selected in the currently
  // showing select popup.
  void SelectPopupMenuItems(JNIEnv* env, jobject obj, jintArray indices);

  void LoadUrlWithoutUrlSanitization(JNIEnv* env,
                                     jobject,
                                     jstring jurl,
                                     int page_transition);
  void LoadUrlWithoutUrlSanitizationWithUserAgentOverride(
      JNIEnv* env,
      jobject,
      jstring jurl,
      int page_transition,
      jstring user_agent_override);
  base::android::ScopedJavaLocalRef<jstring> GetURL(JNIEnv* env, jobject) const;
  base::android::ScopedJavaLocalRef<jstring> GetTitle(
      JNIEnv* env, jobject obj) const;
  jboolean IsIncognito(JNIEnv* env, jobject obj);
  jboolean Crashed(JNIEnv* env, jobject obj) const { return tab_crashed_; }
  jboolean TouchEvent(JNIEnv* env,
                      jobject obj,
                      jlong time_ms,
                      jint type,
                      jobjectArray pts);
  void ScrollBegin(JNIEnv* env, jobject obj, jlong time_ms, jint x, jint y);
  void ScrollEnd(JNIEnv* env, jobject obj, jlong time_ms);
  void ScrollBy(JNIEnv* env, jobject obj, jlong time_ms, jint dx, jint dy);
  void FlingStart(JNIEnv* env,
                  jobject obj,
                  jlong time_ms,
                  jint x,
                  jint y,
                  jint vx,
                  jint vy);
  void FlingCancel(JNIEnv* env, jobject obj, jlong time_ms);
  void SingleTap(JNIEnv* env,
                 jobject obj,
                 jlong time_ms,
                 jint x,
                 jint y,
                 jboolean link_preview_tap);
  void ShowPressState(JNIEnv* env, jobject obj, jlong time_ms, jint x, jint y);
  void DoubleTap(JNIEnv* env, jobject obj, jlong time_ms, jint x, jint y) ;
  void LongPress(JNIEnv* env,
                 jobject obj,
                 jlong time_ms,
                 jint x,
                 jint y,
                 jboolean link_preview_tap);
  void PinchBegin(JNIEnv* env, jobject obj, jlong time_ms, jint x, jint y);
  void PinchEnd(JNIEnv* env, jobject obj, jlong time_ms);
  void PinchBy(JNIEnv* env,
               jobject obj,
               jlong time_ms,
               jint x,
               jint y,
               jfloat delta);
  jboolean CanGoBack(JNIEnv* env, jobject obj);
  jboolean CanGoForward(JNIEnv* env, jobject obj);
  jboolean CanGoToOffset(JNIEnv* env, jobject obj, jint offset);
  void GoBack(JNIEnv* env, jobject obj);
  void GoForward(JNIEnv* env, jobject obj);
  void GoToOffset(JNIEnv* env, jobject obj, jint offset);
  jdouble GetLoadProgress(JNIEnv* env, jobject obj) const;
  void StopLoading(JNIEnv* env, jobject obj);
  void Reload(JNIEnv* env, jobject obj);
  jboolean NeedsReload(JNIEnv* env, jobject obj);
  void ClearHistory(JNIEnv* env, jobject obj);
  void SetClient(JNIEnv* env, jobject obj, jobject jclient);
  jint EvaluateJavaScript(JNIEnv* env, jobject obj, jstring script);
  void AddJavascriptInterface(JNIEnv* env,
                              jobject obj,
                              jobject object,
                              jstring name,
                              jboolean allow_inherited_methods);
  void RemoveJavascriptInterface(JNIEnv* env, jobject obj, jstring name);

  // --------------------------------------------------------------------------
  // Public methods that call to Java via JNI
  // --------------------------------------------------------------------------

  // Creates a popup menu with |items|.
  // |multiple| defines if it should support multi-select.
  // If not |multiple|, |selected_item| sets the initially selected item.
  // Otherwise, item's "checked" flag selects it.
  void ShowSelectPopupMenu(const std::vector<WebMenuItem>& items,
                           int selected_item,
                           bool multiple);

  void OnTabCrashed(const base::ProcessHandle handle);
  void SetTitle(const string16& title);

  bool HasFocus();
  void ConfirmTouchEvent(bool handled);
  void DidSetNeedTouchEvents(bool need_touch_events);
  void OnSelectionChanged(const std::string& text);
  void OnSelectionBoundsChanged(int startx,
                                int starty,
                                base::i18n::TextDirection start_dir,
                                int endx,
                                int endy,
                                base::i18n::TextDirection end_dir);

  // Called when page loading begins.
  void DidStartLoading();

  void OnAcceleratedCompositingStateChange(RenderWidgetHostViewAndroid* rwhva,
                                           bool activated,
                                           bool force);
  void StartContentIntent(const GURL& content_url);

  // --------------------------------------------------------------------------
  // Methods called from native code
  // --------------------------------------------------------------------------

  gfx::Rect GetBounds() const;

  WebContents* web_contents() const { return web_contents_; }
  void LoadUrl(const GURL& url, int page_transition);
  void LoadUrlWithUserAgentOverride(
      const GURL& url,
      int page_transition,
      const std::string& user_agent_override);

 private:
  // NotificationObserver implementation.
  virtual void Observe(int type,
                       const NotificationSource& source,
                       const NotificationDetails& details) OVERRIDE;

  // --------------------------------------------------------------------------
  // Private methods that call to Java via JNI
  // --------------------------------------------------------------------------
  virtual ~ContentViewCoreImpl();

  // --------------------------------------------------------------------------
  // Other private methods and data
  // --------------------------------------------------------------------------

  void InitJNI(JNIEnv* env, jobject obj);

  RenderWidgetHostViewAndroid* GetRenderWidgetHostViewAndroid();

  void SendGestureEvent(WebKit::WebInputEvent::Type type, long time_ms,
                        int x, int y,
                        float dx, float dy, bool link_preview_tap);

  void PostLoadUrl(const GURL& url);

  struct JavaObject;
  JavaObject* java_object_;

  // A weak reference to the Java ContentViewCore object.
  JavaObjectWeakGlobalRef java_ref_;

  NotificationRegistrar notification_registrar_;

  // Reference to the current WebContents used to determine how and what to
  // display in the ContentViewCore.
  WebContentsImpl* web_contents_;

  // We only set this to be the delegate of the web_contents if we own it.
  scoped_ptr<ContentViewClient> content_view_client_;

  // Whether the renderer backing this ContentViewCore has crashed.
  bool tab_crashed_;

  DISALLOW_COPY_AND_ASSIGN(ContentViewCoreImpl);
};

bool RegisterContentViewCore(JNIEnv* env);

}  // namespace content

#endif  // CONTENT_BROWSER_ANDROID_CONTENT_VIEW_CORE_IMPL_H_
