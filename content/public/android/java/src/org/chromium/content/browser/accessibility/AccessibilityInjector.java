// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser.accessibility;

import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.os.Vibrator;
import android.provider.Settings;
import android.speech.tts.TextToSpeech;
import android.view.View;
import android.view.accessibility.AccessibilityManager;
import android.view.accessibility.AccessibilityNodeInfo;

import org.apache.http.NameValuePair;
import org.apache.http.client.utils.URLEncodedUtils;
import org.chromium.content.browser.ContentViewCore;
import org.json.JSONException;
import org.json.JSONObject;

import java.lang.reflect.Field;
import java.net.URI;
import java.net.URISyntaxException;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Responsible for accessibility injection and management of a {@link ContentViewCore}.
 */
public class AccessibilityInjector {
    // The ContentView this injector is responsible for managing.
    protected ContentViewCore mContentViewCore;

    // The Java objects that are exposed to JavaScript
    private TextToSpeechWrapper mTextToSpeech;
    private VibratorWrapper mVibrator;

    // Lazily loaded helper objects.
    private AccessibilityManager mAccessibilityManager;

    // Whether or not we should be injecting the script.
    protected boolean mInjectedScriptEnabled;
    protected boolean mScriptInjected;

    // constants for determining script injection strategy
    private static final int ACCESSIBILITY_SCRIPT_INJECTION_UNDEFINED = -1;
    private static final int ACCESSIBILITY_SCRIPT_INJECTION_OPTED_OUT = 0;
    private static final int ACCESSIBILITY_SCRIPT_INJECTION_PROVIDED = 1;
    private static final String ALIAS_ACCESSIBILITY_JS_INTERFACE = "accessibility";
    private static final String ALIAS_ACCESSIBILITY_JS_INTERFACE_2 = "accessibility2";

    // Template for JavaScript that injects a screen-reader.
    private static final String ACCESSIBILITY_SCREEN_READER_URL =
            "https://ssl.gstatic.com/accessibility/javascript/android/chromeandroidvox.js";

    private static final String ACCESSIBILITY_SCREEN_READER_JAVASCRIPT_TEMPLATE =
            "(function() {" +
            "    var chooser = document.createElement('script');" +
            "    chooser.type = 'text/javascript';" +
            "    chooser.src = '%1s';" +
            "    document.getElementsByTagName('head')[0].appendChild(chooser);" +
            "  })();";

    // JavaScript call to turn ChromeVox on or off.
    private static final String TOGGLE_CHROME_VOX_JAVASCRIPT =
            "(function() {" +
            "    if (typeof cvox !== 'undefined') {" +
            "        cvox.ChromeVox.host.activateOrDeactivateChromeVox(%1s);" +
            "    }" +
            "  })();";

    /**
     * Returns an instance of the {@link AccessibilityInjector} based on the SDK version.
     * @param view The ContentViewCore that this AccessibilityInjector manages.
     * @return An instance of a {@link AccessibilityInjector}.
     */
    public static AccessibilityInjector newInstance(ContentViewCore view) {
        // TODO(dtrainor): Upstream JellyBean version of AccessibilityInjector when SDK is 16.
        return new AccessibilityInjector(view);
    }

    /**
     * Creates an instance of the IceCreamSandwichAccessibilityInjector.
     * @param view The ContentViewCore that this AccessibilityInjector manages.
     */
    protected AccessibilityInjector(ContentViewCore view) {
        mContentViewCore = view;
    }

    /**
     * Injects a <script> tag into the current web site that pulls in the ChromeVox script for
     * accessibility support.  Only injects if accessibility is turned on by
     * {@link AccessibilityManager#isEnabled()}, accessibility script injection is turned on, and
     * javascript is enabled on this page.
     *
     * @see AccessibilityManager#isEnabled()
     */
    public void injectAccessibilityScriptIntoPage() {
        if (!accessibilityIsAvailable()) return;

        int axsParameterValue = getAxsUrlParameterValue();
        if (axsParameterValue == ACCESSIBILITY_SCRIPT_INJECTION_UNDEFINED) {
            try {
                Field field = Settings.Secure.class.getField("ACCESSIBILITY_SCRIPT_INJECTION");
                field.setAccessible(true);
                String ACCESSIBILITY_SCRIPT_INJECTION = (String) field.get(null);

                boolean onDeviceScriptInjectionEnabled = (Settings.Secure.getInt(
                        mContentViewCore.getContext().getContentResolver(),
                        ACCESSIBILITY_SCRIPT_INJECTION, 0) == 1);
                String js = getScreenReaderInjectingJs();

                if (onDeviceScriptInjectionEnabled && js != null && mContentViewCore.isAlive()) {
                    addOrRemoveAccessibilityApisIfNecessary();
                    mContentViewCore.evaluateJavaScript(js);
                    mInjectedScriptEnabled = true;
                    mScriptInjected = true;
                }
            } catch (NoSuchFieldException ex) {
            } catch (IllegalArgumentException ex) {
            } catch (IllegalAccessException ex) {
            }
        }
    }

    /**
     * Handles adding or removing accessibility related Java objects ({@link TextToSpeech} and
     * {@link Vibrator}) interfaces from Javascript.  This method should be called at a time when it
     * is safe to add or remove these interfaces, specifically when the {@link ContentView} is first
     * initialized or right before the {@link ContentView} is about to navigate to a URL or reload.
     * <p>
     * If this method is called at other times, the interfaces might not be correctly removed,
     * meaning that Javascript can still access these Java objects that may have been already
     * shut down.
     */
    public void addOrRemoveAccessibilityApisIfNecessary() {
        if (accessibilityIsAvailable()) {
            addAccessibilityApis();
        } else {
            removeAccessibilityApis();
        }
    }

    /**
     * Checks whether or not touch to explore is enabled on the system.
     */
    public boolean accessibilityIsAvailable() {
        return getAccessibilityManager().isEnabled() &&
                mContentViewCore.getContentSettings() != null &&
                mContentViewCore.getContentSettings().getJavaScriptEnabled();
    }

    /**
     * Sets whether or not the script is enabled.  If the script is disabled, we also stop any
     * we output that is occurring.
     * @param enabled Whether or not to enable the script.
     */
    public void setScriptEnabled(boolean enabled) {
        if (!accessibilityIsAvailable() || mInjectedScriptEnabled == enabled) return;

        mInjectedScriptEnabled = enabled;
        if (mContentViewCore.isAlive()) {
            String js = String.format(TOGGLE_CHROME_VOX_JAVASCRIPT, Boolean.toString(
                    mInjectedScriptEnabled));
            mContentViewCore.evaluateJavaScript(js);

            if (!mInjectedScriptEnabled) {
                // Stop any TTS/Vibration right now.
                onPageLostFocus();
            }
        }
    }

    /**
     * Notifies this handler that a page load has started, which means we should mark the
     * accessibility script as not being injected.  This way we can properly ignore incoming
     * accessibility gesture events.
     */
    public void onPageLoadStarted() {
        mScriptInjected = false;
    }

    /**
     * Stop any notifications that are currently going on (e.g. Text-to-Speech).
     */
    public void onPageLostFocus() {
        if (mContentViewCore.isAlive()) {
            if (mTextToSpeech != null) mTextToSpeech.stop();
            if (mVibrator != null) mVibrator.cancel();
        }
    }

    /**
     * Initializes an {@link AccessibilityNodeInfo} with the actions and movement granularity
     * levels supported by this {@link AccessibilityInjector}.
     * <p>
     * If an action identifier is added in this method, this {@link AccessibilityInjector} should
     * also return {@code true} from {@link #supportsAccessibilityAction(int)}.
     * </p>
     *
     * @param info The info to initialize.
     * @see View#onInitializeAccessibilityNodeInfo(AccessibilityNodeInfo)
     */
    public void onInitializeAccessibilityNodeInfo(AccessibilityNodeInfo info) { }

    /**
     * Returns {@code true} if this {@link AccessibilityInjector} should handle the specified
     * action.
     *
     * @param action An accessibility action identifier.
     * @return {@code true} if this {@link AccessibilityInjector} should handle the specified
     *         action.
     */
    public boolean supportsAccessibilityAction(int action) {
        return false;
    }

    /**
     * Performs the specified accessibility action.
     *
     * @param action The identifier of the action to perform.
     * @param arguments The action arguments, or {@code null} if no arguments.
     * @return {@code true} if the action was successful.
     * @see View#performAccessibilityAction(int, Bundle)
     */
    public boolean performAccessibilityAction(int action, Bundle arguments) {
        return false;
    }

    protected void addAccessibilityApis() {
        Context context = mContentViewCore.getContext();
        if (context != null) {
            // Enabled, we should try to add if we have to.
            if (mTextToSpeech == null) {
                mTextToSpeech = new TextToSpeechWrapper(context);
                mContentViewCore.addJavascriptInterface(mTextToSpeech,
                        ALIAS_ACCESSIBILITY_JS_INTERFACE, false);
            }

            if (mVibrator == null) {
                mVibrator = new VibratorWrapper(context);
                mContentViewCore.addJavascriptInterface(mVibrator,
                        ALIAS_ACCESSIBILITY_JS_INTERFACE_2, false);
            }
        }
    }

    protected void removeAccessibilityApis() {
        if (mTextToSpeech != null) {
            mContentViewCore.removeJavascriptInterface(ALIAS_ACCESSIBILITY_JS_INTERFACE);
            mTextToSpeech.stop();
            mTextToSpeech.shutdownInternal();
            mTextToSpeech = null;
        }

        if (mVibrator != null) {
            mContentViewCore.removeJavascriptInterface(ALIAS_ACCESSIBILITY_JS_INTERFACE_2);
            mVibrator.cancel();
            mVibrator = null;
        }
    }

    private int getAxsUrlParameterValue() {
        if (mContentViewCore.getUrl() == null) return ACCESSIBILITY_SCRIPT_INJECTION_UNDEFINED;

        try {
            List<NameValuePair> params = URLEncodedUtils.parse(new URI(mContentViewCore.getUrl()),
                    null);

            for (NameValuePair param : params) {
                if ("axs".equals(param.getName())) {
                    return Integer.parseInt(param.getValue());
                }
            }
        } catch (URISyntaxException ex) {
        } catch (NumberFormatException ex) {
        } catch (IllegalArgumentException ex) {
        }

        return ACCESSIBILITY_SCRIPT_INJECTION_UNDEFINED;
    }

    private String getScreenReaderInjectingJs() {
        return String.format(ACCESSIBILITY_SCREEN_READER_JAVASCRIPT_TEMPLATE,
                ACCESSIBILITY_SCREEN_READER_URL);
    }

    private AccessibilityManager getAccessibilityManager() {
        if (mAccessibilityManager == null) {
            mAccessibilityManager = (AccessibilityManager) mContentViewCore.getContext().
                    getSystemService(Context.ACCESSIBILITY_SERVICE);
        }

        return mAccessibilityManager;
    }

    /**
     * Used to protect how long JavaScript can vibrate for.  This isn't a good comprehensive
     * protection, just used to cover mistakes and protect against long vibrate durations/repeats.
     *
     * Also only exposes methods we *want* to expose, no others for the class.
     */
    private static class VibratorWrapper {
        private static final long MAX_VIBRATE_DURATION_MS = 5000;

        private Vibrator mVibrator;

        public VibratorWrapper(Context context) {
            mVibrator = (Vibrator) context.getSystemService(Context.VIBRATOR_SERVICE);
        }

        @SuppressWarnings("unused")
        public boolean hasVibrator() {
            return mVibrator.hasVibrator();
        }

        @SuppressWarnings("unused")
        public void vibrate(long milliseconds) {
            milliseconds = Math.min(milliseconds, MAX_VIBRATE_DURATION_MS);
            mVibrator.vibrate(milliseconds);
        }

        @SuppressWarnings("unused")
        public void vibrate(long[] pattern, int repeat) {
            for (int i = 0; i < pattern.length; ++i) {
                pattern[i] = Math.min(pattern[i], MAX_VIBRATE_DURATION_MS);
            }

            repeat = -1;

            mVibrator.vibrate(pattern, repeat);
        }

        @SuppressWarnings("unused")
        public void cancel() {
            mVibrator.cancel();
        }
    }

    /**
     * Used to protect the TextToSpeech class, only exposing the methods we want to expose.
     */
    private static class TextToSpeechWrapper {
        private TextToSpeech mTextToSpeech;

        public TextToSpeechWrapper(Context context) {
            mTextToSpeech = new TextToSpeech(context, null, null);
        }

        @SuppressWarnings("unused")
        public boolean isSpeaking() {
            return mTextToSpeech.isSpeaking();
        }

        @SuppressWarnings("unused")
        public int speak(String text, int queueMode, HashMap<String, String> params) {
            return mTextToSpeech.speak(text, queueMode, params);
        }

        @SuppressWarnings("unused")
        public int stop() {
            return mTextToSpeech.stop();
        }

        @SuppressWarnings("unused")
        protected void shutdownInternal() {
            mTextToSpeech.shutdown();
        }
    }
}
