// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/android/content_settings.h"

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "content/browser/android/content_view_core_impl.h"
#include "content/browser/renderer_host/render_view_host_delegate.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_client.h"
#include "jni/ContentSettings_jni.h"
#include "webkit/glue/webkit_glue.h"
#include "webkit/glue/webpreferences.h"
#include "webkit/user_agent/user_agent.h"

using base::android::CheckException;
using base::android::ConvertJavaStringToUTF16;
using base::android::ConvertUTF16ToJavaString;
using base::android::ConvertUTF8ToJavaString;
using base::android::GetClass;
using base::android::GetFieldID;
using base::android::GetMethodIDFromClassName;
using base::android::ScopedJavaLocalRef;
using webkit_glue::WebPreferences;

namespace content {

struct ContentSettings::FieldIds {
  // Note on speed. One may think that an approach that reads field values via
  // JNI is ineffective and should not be used. Please keep in mind that in the
  // legacy WebView the whole Sync method took <1ms on Xoom, and no one is
  // expected to modify settings in performance-critical code.
  FieldIds() { }

  FieldIds(JNIEnv* env) {
    const char* kStringClassName = "Ljava/lang/String;";

    // FIXME: we should be using a new GetFieldIDFromClassName() with caching.
    ScopedJavaLocalRef<jclass> clazz(
        GetClass(env, "org/chromium/content/browser/ContentSettings"));
    text_size_percent = GetFieldID(env, clazz, "mTextSizePercent", "I");
    standard_fond_family =
        GetFieldID(env, clazz, "mStandardFontFamily", kStringClassName);
    fixed_font_family =
        GetFieldID(env, clazz, "mFixedFontFamily", kStringClassName);
    sans_serif_font_family =
        GetFieldID(env, clazz, "mSansSerifFontFamily", kStringClassName);
    serif_font_family =
        GetFieldID(env, clazz, "mSerifFontFamily", kStringClassName);
    cursive_font_family =
        GetFieldID(env, clazz, "mCursiveFontFamily", kStringClassName);
    fantasy_font_family =
        GetFieldID(env, clazz, "mFantasyFontFamily", kStringClassName);
    default_text_encoding =
        GetFieldID(env, clazz, "mDefaultTextEncoding", kStringClassName);
    user_agent =
        GetFieldID(env, clazz, "mUserAgent", kStringClassName);
    minimum_font_size = GetFieldID(env, clazz, "mMinimumFontSize", "I");
    minimum_logical_font_size =
        GetFieldID(env, clazz, "mMinimumLogicalFontSize", "I");
    default_font_size = GetFieldID(env, clazz, "mDefaultFontSize", "I");
    default_fixed_font_size =
        GetFieldID(env, clazz, "mDefaultFixedFontSize", "I");
    load_images_automatically =
        GetFieldID(env, clazz, "mLoadsImagesAutomatically", "Z");
    images_enabled =
        GetFieldID(env, clazz, "mImagesEnabled", "Z");
    java_script_enabled =
        GetFieldID(env, clazz, "mJavaScriptEnabled", "Z");
    allow_universal_access_from_file_urls =
        GetFieldID(env, clazz, "mAllowUniversalAccessFromFileURLs", "Z");
    allow_file_access_from_file_urls =
        GetFieldID(env, clazz, "mAllowFileAccessFromFileURLs", "Z");
    java_script_can_open_windows_automatically =
        GetFieldID(env, clazz, "mJavaScriptCanOpenWindowsAutomatically", "Z");
    support_multiple_windows =
        GetFieldID(env, clazz, "mSupportMultipleWindows", "Z");
    dom_storage_enabled =
        GetFieldID(env, clazz, "mDomStorageEnabled", "Z");
  }

  // Field ids
  jfieldID text_size_percent;
  jfieldID standard_fond_family;
  jfieldID fixed_font_family;
  jfieldID sans_serif_font_family;
  jfieldID serif_font_family;
  jfieldID cursive_font_family;
  jfieldID fantasy_font_family;
  jfieldID default_text_encoding;
  jfieldID user_agent;
  jfieldID minimum_font_size;
  jfieldID minimum_logical_font_size;
  jfieldID default_font_size;
  jfieldID default_fixed_font_size;
  jfieldID load_images_automatically;
  jfieldID images_enabled;
  jfieldID java_script_enabled;
  jfieldID allow_universal_access_from_file_urls;
  jfieldID allow_file_access_from_file_urls;
  jfieldID java_script_can_open_windows_automatically;
  jfieldID support_multiple_windows;
  jfieldID dom_storage_enabled;
};

ContentSettings::ContentSettings(JNIEnv* env,
                         jobject obj,
                         WebContents* contents,
                         bool is_master_mode)
    : WebContentsObserver(contents),
      is_master_mode_(is_master_mode),
      content_settings_(env, obj) {
}

ContentSettings::~ContentSettings() {
  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = content_settings_.get(env);
  if (obj.obj()) {
    Java_ContentSettings_onNativeContentSettingsDestroyed(env, obj.obj(),
        reinterpret_cast<jint>(this));
  }
}

// static
bool ContentSettings::RegisterContentSettings(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

void ContentSettings::SyncFromNativeImpl() {
  JNIEnv* env = base::android::AttachCurrentThread();
  CHECK(env);
  if (!field_ids_.get())
    field_ids_.reset(new FieldIds(env));

  ScopedJavaLocalRef<jobject> scoped_obj = content_settings_.get(env);
  jobject obj = scoped_obj.obj();
  if (!obj)
    return;
  RenderViewHost* render_view_host = web_contents()->GetRenderViewHost();
  WebPreferences prefs = render_view_host->GetDelegate()->GetWebkitPrefs();

  Java_ContentSettings_setTextAutosizingEnabled(
      env, obj, prefs.text_autosizing_enabled);
  CheckException(env);

  env->SetIntField(
      obj,
      field_ids_->text_size_percent,
      prefs.font_scale_factor * 100.0f);
  CheckException(env);

  ScopedJavaLocalRef<jstring> str =
      ConvertUTF16ToJavaString(env,
          prefs.standard_font_family_map[WebPreferences::kCommonScript]);
  env->SetObjectField(obj, field_ids_->standard_fond_family, str.obj());
  CheckException(env);

  str.Reset(ConvertUTF16ToJavaString(env,
      prefs.fixed_font_family_map[WebPreferences::kCommonScript]));
  env->SetObjectField(obj, field_ids_->fixed_font_family, str.obj());
  CheckException(env);

  str.Reset(ConvertUTF16ToJavaString(env,
      prefs.sans_serif_font_family_map[WebPreferences::kCommonScript]));
  env->SetObjectField(obj, field_ids_->sans_serif_font_family, str.obj());
  CheckException(env);

  str.Reset(ConvertUTF16ToJavaString(env,
      prefs.serif_font_family_map[WebPreferences::kCommonScript]));
  env->SetObjectField(obj, field_ids_->serif_font_family, str.obj());
  CheckException(env);

  str.Reset(ConvertUTF16ToJavaString(env,
      prefs.cursive_font_family_map[WebPreferences::kCommonScript]));
  env->SetObjectField(obj, field_ids_->cursive_font_family, str.obj());
  CheckException(env);

  str.Reset(ConvertUTF16ToJavaString(env,
      prefs.fantasy_font_family_map[WebPreferences::kCommonScript]));
  env->SetObjectField(obj, field_ids_->fantasy_font_family, str.obj());
  CheckException(env);

  str.Reset(ConvertUTF8ToJavaString(env, prefs.default_encoding));
  env->SetObjectField(obj, field_ids_->default_text_encoding, str.obj());
  CheckException(env);

  str.Reset(ConvertUTF8ToJavaString(env, webkit_glue::GetUserAgent(GURL(""))));
  env->SetObjectField(obj, field_ids_->user_agent, str.obj());
  CheckException(env);

  env->SetIntField(obj, field_ids_->minimum_font_size,
                   prefs.minimum_font_size);
  CheckException(env);

  env->SetIntField(
      obj,
      field_ids_->minimum_logical_font_size, prefs.minimum_logical_font_size);
  CheckException(env);

  env->SetIntField(obj, field_ids_->default_font_size,
                   prefs.default_font_size);
  CheckException(env);

  env->SetIntField(
      obj, field_ids_->default_fixed_font_size, prefs.default_fixed_font_size);
  CheckException(env);

  env->SetBooleanField(
      obj,
      field_ids_->load_images_automatically, prefs.loads_images_automatically);
  CheckException(env);

  env->SetBooleanField(
      obj,
      field_ids_->images_enabled, prefs.images_enabled);
  CheckException(env);

  env->SetBooleanField(
      obj, field_ids_->java_script_enabled, prefs.javascript_enabled);
  CheckException(env);

  env->SetBooleanField(
      obj,
      field_ids_->allow_universal_access_from_file_urls,
      prefs.allow_universal_access_from_file_urls);
  CheckException(env);

  env->SetBooleanField(
      obj,
      field_ids_->allow_file_access_from_file_urls,
      prefs.allow_file_access_from_file_urls);
  CheckException(env);

  env->SetBooleanField(
      obj,
      field_ids_->java_script_can_open_windows_automatically,
      prefs.javascript_can_open_windows_automatically);
  CheckException(env);

  env->SetBooleanField(
      obj,
      field_ids_->support_multiple_windows,
      prefs.supports_multiple_windows);
  CheckException(env);

  Java_ContentSettings_setPluginsDisabled(env, obj, !prefs.plugins_enabled);
  CheckException(env);

  // We don't need to sync AppCache settings to Java, because there are
  // no getters for them in the API.

  env->SetBooleanField(
      obj,
      field_ids_->dom_storage_enabled,
      prefs.local_storage_enabled);
  CheckException(env);
}

void ContentSettings::SyncToNativeImpl() {
  JNIEnv* env = base::android::AttachCurrentThread();
  CHECK(env);
  if (!field_ids_.get())
    field_ids_.reset(new FieldIds(env));

  ScopedJavaLocalRef<jobject> scoped_obj = content_settings_.get(env);
  jobject obj = scoped_obj.obj();
  if (!obj)
    return;
  RenderViewHost* render_view_host = web_contents()->GetRenderViewHost();
  WebPreferences prefs = render_view_host->GetDelegate()->GetWebkitPrefs();

  prefs.text_autosizing_enabled =
      Java_ContentSettings_getTextAutosizingEnabled(env, obj);

  int text_size_percent = env->GetIntField(obj, field_ids_->text_size_percent);
  prefs.font_scale_factor = text_size_percent / 100.0f;
  prefs.force_enable_zoom = text_size_percent >= 130;

  ScopedJavaLocalRef<jstring> str(
      env, static_cast<jstring>(
          env->GetObjectField(obj, field_ids_->standard_fond_family)));
  prefs.standard_font_family_map[WebPreferences::kCommonScript] =
      ConvertJavaStringToUTF16(str);

  str.Reset(
      env, static_cast<jstring>(
          env->GetObjectField(obj, field_ids_->fixed_font_family)));
  prefs.fixed_font_family_map[WebPreferences::kCommonScript] =
      ConvertJavaStringToUTF16(str);

  str.Reset(
      env, static_cast<jstring>(
          env->GetObjectField(obj, field_ids_->sans_serif_font_family)));
  prefs.sans_serif_font_family_map[WebPreferences::kCommonScript] =
      ConvertJavaStringToUTF16(str);

  str.Reset(
      env, static_cast<jstring>(
          env->GetObjectField(obj, field_ids_->serif_font_family)));
  prefs.serif_font_family_map[WebPreferences::kCommonScript] =
      ConvertJavaStringToUTF16(str);

  str.Reset(
      env, static_cast<jstring>(
          env->GetObjectField(obj, field_ids_->cursive_font_family)));
  prefs.cursive_font_family_map[WebPreferences::kCommonScript] =
      ConvertJavaStringToUTF16(str);

  str.Reset(
      env, static_cast<jstring>(
          env->GetObjectField(obj, field_ids_->fantasy_font_family)));
  prefs.fantasy_font_family_map[WebPreferences::kCommonScript] =
      ConvertJavaStringToUTF16(str);

  str.Reset(
      env, static_cast<jstring>(
          env->GetObjectField(obj, field_ids_->default_text_encoding)));
  prefs.default_encoding = ConvertJavaStringToUTF8(str);

  prefs.minimum_font_size =
      env->GetIntField(obj, field_ids_->minimum_font_size);

  prefs.minimum_logical_font_size =
      env->GetIntField(obj, field_ids_->minimum_logical_font_size);

  prefs.default_font_size =
      env->GetIntField(obj, field_ids_->default_font_size);

  prefs.default_fixed_font_size =
      env->GetIntField(obj, field_ids_->default_fixed_font_size);

  prefs.loads_images_automatically =
      env->GetBooleanField(obj, field_ids_->load_images_automatically);

  prefs.images_enabled =
      env->GetBooleanField(obj, field_ids_->images_enabled);

  prefs.javascript_enabled =
      env->GetBooleanField(obj, field_ids_->java_script_enabled);

  prefs.allow_universal_access_from_file_urls = env->GetBooleanField(
      obj, field_ids_->allow_universal_access_from_file_urls);

  prefs.allow_file_access_from_file_urls = env->GetBooleanField(
      obj, field_ids_->allow_file_access_from_file_urls);

  prefs.javascript_can_open_windows_automatically = env->GetBooleanField(
      obj, field_ids_->java_script_can_open_windows_automatically);

  prefs.supports_multiple_windows = env->GetBooleanField(
      obj, field_ids_->support_multiple_windows);

  prefs.plugins_enabled = !Java_ContentSettings_getPluginsDisabled(env, obj);

  prefs.application_cache_enabled =
      Java_ContentSettings_getAppCacheEnabled(env, obj);

  prefs.local_storage_enabled = env->GetBooleanField(
      obj, field_ids_->dom_storage_enabled);

  render_view_host->UpdateWebkitPreferences(prefs);
}

void ContentSettings::SyncFromNative(JNIEnv* env, jobject obj) {
  SyncFromNativeImpl();
}

void ContentSettings::SyncToNative(JNIEnv* env, jobject obj) {
  SyncToNativeImpl();
}

void ContentSettings::RenderViewCreated(RenderViewHost* render_view_host) {
  if (is_master_mode_)
    SyncToNativeImpl();
}

void ContentSettings::WebContentsDestroyed(WebContents* web_contents) {
  delete this;
}

static jint Init(JNIEnv* env, jobject obj, jint nativeContentViewCore,
                 jboolean is_master_mode) {
  WebContents* web_contents =
      reinterpret_cast<ContentViewCoreImpl*>(nativeContentViewCore)
          ->GetWebContents();
  ContentSettings* content_settings =
      new ContentSettings(env, obj, web_contents, is_master_mode);
  return reinterpret_cast<jint>(content_settings);
}

static jstring GetDefaultUserAgent(JNIEnv* env, jclass clazz) {
  return base::android::ConvertUTF8ToJavaString(
      env, GetContentClient()->GetUserAgent()).Release();
}

}  // namespace content
