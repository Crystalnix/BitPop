// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser;

import android.content.Context;
import android.content.res.Configuration;
import android.graphics.Canvas;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.ActionMode;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.webkit.DownloadListener;

import org.chromium.base.CalledByNative;
import org.chromium.base.JNINamespace;
import org.chromium.base.WeakContext;
import org.chromium.content.browser.ContentViewGestureHandler;
import org.chromium.content.browser.TouchPoint;
import org.chromium.content.browser.ZoomManager;
import org.chromium.content.common.CleanupReference;
import org.chromium.content.common.TraceEvent;

import org.chromium.content.browser.accessibility.AccessibilityInjector;
import org.chromium.content.browser.ContentViewGestureHandler.MotionEventDelegate;

/**
 * Contains all the major functionality necessary to manage the lifecycle of a ContentView without
 * being tied to the view system.
 */
@JNINamespace("content")
public class ContentViewCore implements MotionEventDelegate {
    private static final String TAG = ContentViewCore.class.getName();

    // The following constants match the ones in chrome/common/page_transition_types.h.
    // Add more if you need them.
    public static final int PAGE_TRANSITION_LINK = 0;
    public static final int PAGE_TRANSITION_TYPED = 1;
    public static final int PAGE_TRANSITION_AUTO_BOOKMARK = 2;
    public static final int PAGE_TRANSITION_START_PAGE = 6;

    /** Translate the find selection into a normal selection. */
    public static final int FIND_SELECTION_ACTION_KEEP_SELECTION = 0;
    /** Clear the find selection. */
    public static final int FIND_SELECTION_ACTION_CLEAR_SELECTION = 1;
    /** Focus and click the selected node (for links). */
    public static final int FIND_SELECTION_ACTION_ACTIVATE_SELECTION = 2;

    // Personality of the ContentView.
    private int mPersonality;
    // Used when ContentView implements a standalone View.
    public static final int PERSONALITY_VIEW = 0;
    // Used for Chrome.
    public static final int PERSONALITY_CHROME = 1;

    // Used to avoid enabling zooming in / out if resulting zooming will
    // produce little visible difference.
    private static float ZOOM_CONTROLS_EPSILON = 0.007f;

    // To avoid checkerboard, we clamp the fling velocity based on the maximum number of tiles
    // should be allowed to upload per 100ms.
    private static int MAX_NUM_UPLOAD_TILES = 12;

    /**
     * Interface that consumers of {@link ContentViewCore} must implement to allow the proper
     * dispatching of view methods through the containing view.
     *
     * <p>
     * All methods with the "super_" prefix should be routed to the parent of the
     * implementing container view.
     */
    @SuppressWarnings("javadoc")
    public static interface InternalAccessDelegate {
        /**
         * @see View#drawChild(Canvas, View, long)
         */
        boolean drawChild(Canvas canvas, View child, long drawingTime);

        /**
         * @see View#onKeyUp(keyCode, KeyEvent)
         */
        boolean super_onKeyUp(int keyCode, KeyEvent event);

        /**
         * @see View#dispatchKeyEventPreIme(KeyEvent)
         */
        boolean super_dispatchKeyEventPreIme(KeyEvent event);

        /**
         * @see View#dispatchKeyEvent(KeyEvent)
         */
        boolean super_dispatchKeyEvent(KeyEvent event);

        /**
         * @see View#onGenericMotionEvent(MotionEvent)
         */
        boolean super_onGenericMotionEvent(MotionEvent event);

        /**
         * @see View#onConfigurationChanged(Configuration)
         */
        void super_onConfigurationChanged(Configuration newConfig);

        /**
         * @see View#onScrollChanged(int, int, int, int)
         */
        void onScrollChanged(int l, int t, int oldl, int oldt);

        /**
         * @see View#awakenScrollBars()
         */
        boolean awakenScrollBars();

        /**
         * @see View#awakenScrollBars(int, boolean)
         */
        boolean super_awakenScrollBars(int startDelay, boolean invalidate);
    }

    private static final class DestroyRunnable implements Runnable {
        private int mNativeContentViewCore;
        private DestroyRunnable(int nativeContentViewCore) {
            mNativeContentViewCore = nativeContentViewCore;
        }
        @Override
        public void run() {
            nativeDestroy(mNativeContentViewCore);
        }
    }

    private CleanupReference mCleanupReference;

    private Context mContext;
    private ViewGroup mContainerView;
    private InternalAccessDelegate mContainerViewInternals;

    // content_view_client.cc depends on ContentViewCore.java holding a ref to the current client
    // instance since the native side only holds a weak pointer to the client. We chose this
    // solution over the managed object owning the C++ object's memory since it's a lot simpler
    // in terms of clean up.
    private ContentViewClient mContentViewClient;

    private ContentSettings mContentSettings;

    // Native pointer to C++ ContentViewCoreImpl object which will be set by nativeInit().
    private int mNativeContentViewCore = 0;

    private ContentViewGestureHandler mContentViewGestureHandler;
    private ZoomManager mZoomManager;

    // Cached page scale factor from native
    private float mNativePageScaleFactor = 1.0f;
    private float mNativeMinimumScale = 1.0f;
    private float mNativeMaximumScale = 1.0f;

    // TODO(klobag): this is to avoid a bug in GestureDetector. With multi-touch,
    // mAlwaysInTapRegion is not reset. So when the last finger is up, onSingleTapUp()
    // will be mistakenly fired.
    private boolean mIgnoreSingleTap;

    // Only valid when focused on a text / password field.
    private ImeAdapter mImeAdapter;

    // Tracks whether a selection is currently active.  When applied to selected text, indicates
    // whether the last selected text is still highlighted.
    private boolean mHasSelection;
    private String mLastSelectedText;
    private boolean mSelectionEditable;
    private ActionMode mActionMode;

    // The legacy webview DownloadListener.
    private DownloadListener mDownloadListener;
    // ContentViewDownloadDelegate adds support for authenticated downloads
    // and POST downloads. Embedders should prefer ContentViewDownloadDelegate
    // over DownloadListener.
    private ContentViewDownloadDelegate mDownloadDelegate;

    // The AccessibilityInjector that handles loading Accessibility scripts into the web page.
    private final AccessibilityInjector mAccessibilityInjector;

    /**
     * Enable multi-process ContentView. This should be called by the application before
     * constructing any ContentView instances. If enabled, ContentView will run renderers in
     * separate processes up to the number of processes specified by maxRenderProcesses. If this is
     * not called then the default is to run the renderer in the main application on a separate
     * thread.
     *
     * @param context Context used to obtain the application context.
     * @param maxRendererProcesses Limit on the number of renderers to use. Each tab runs in its own
     * process until the maximum number of processes is reached. The special value of
     * MAX_RENDERERS_SINGLE_PROCESS requests single-process mode where the renderer will run in the
     * application process in a separate thread. If the special value MAX_RENDERERS_AUTOMATIC is
     * used then the number of renderers will be determined based on the device memory class. The
     * maximum number of allowed renderers is capped by MAX_RENDERERS_LIMIT.
     * @return Whether the process actually needed to be initialized (false if already running).
     */
    public static boolean enableMultiProcess(Context context, int maxRendererProcesses) {
        return AndroidBrowserProcess.initContentViewProcess(context, maxRendererProcesses);
    }

    /**
     * Initialize the process as the platform browser. This must be called before accessing
     * ContentView in order to treat this as a Chromium browser process.
     *
     * @param context Context used to obtain the application context.
     * @param maxRendererProcesses Same as ContentView.enableMultiProcess()
     * @return Whether the process actually needed to be initialized (false if already running).
     */
    public static boolean initChromiumBrowserProcess(Context context, int maxRendererProcesses) {
        return AndroidBrowserProcess.initChromiumBrowserProcess(context, maxRendererProcesses);
    }

    /**
     * Constructs a new ContentViewCore.
     *
     * @param context The context used to create this.
     * @param containerView The view that will act as a container for all views created by this.
     * @param internalDispatcher Handles dispatching all hidden or super methods to the
     *                           containerView.
     * @param nativeWebContents A pointer to the native web contents.
     * @param personality The type of ContentViewCore being created.
     */
    public ContentViewCore(
            Context context, ViewGroup containerView,
            InternalAccessDelegate internalDispatcher,
            int nativeWebContents, int personality) {
        mContext = context;
        mContainerView = containerView;
        mContainerViewInternals = internalDispatcher;

        WeakContext.initializeWeakContext(context);
        // By default, ContentView will initialize single process mode. The call to
        // initContentViewProcess below is ignored if either the ContentView host called
        // enableMultiProcess() or the platform browser called initChromiumBrowserProcess().
        AndroidBrowserProcess.initContentViewProcess(
                context, AndroidBrowserProcess.MAX_RENDERERS_SINGLE_PROCESS);

        mAccessibilityInjector = AccessibilityInjector.newInstance(this);
        mAccessibilityInjector.addOrRemoveAccessibilityApisIfNecessary();

        initialize(context, nativeWebContents, personality);
    }

    /**
     * @return The context used for creating this ContentViewCore.
     */
    public Context getContext() {
        return mContext;
    }

    /**
     * @return The ViewGroup that all view actions of this ContentViewCore should interact with.
     */
    protected ViewGroup getContainerView() {
        return mContainerView;
    }

    // TODO(jrg): incomplete; upstream the rest of this method.
    private void initialize(Context context, int nativeWebContents, int personality) {
        mNativeContentViewCore = nativeInit(nativeWebContents);
        mCleanupReference = new CleanupReference(this, new DestroyRunnable(mNativeContentViewCore));

        mPersonality = personality;
        mContentSettings = new ContentSettings(this, mNativeContentViewCore);
        mContainerView.setFocusable(true);
        mContainerView.setFocusableInTouchMode(true);
        if (mContainerView.getScrollBarStyle() == View.SCROLLBARS_INSIDE_OVERLAY) {
            mContainerView.setHorizontalScrollBarEnabled(false);
            mContainerView.setVerticalScrollBarEnabled(false);
        }
        mContainerView.setClickable(true);

        mZoomManager = new ZoomManager(context, this);
        mZoomManager.updateMultiTouchSupport();
        mContentViewGestureHandler = new ContentViewGestureHandler(context, this, mZoomManager);

        Log.i(TAG, "mNativeContentView=0x"+ Integer.toHexString(mNativeContentViewCore));
    }

    /**
     * @return Whether the configured personality of this ContentView is {@link #PERSONALITY_VIEW}.
     */
    boolean isPersonalityView() {
        switch (mPersonality) {
            case PERSONALITY_VIEW:
                return true;
            case PERSONALITY_CHROME:
                return false;
            default:
                Log.e(TAG, "Unknown ContentView personality: " + mPersonality);
                return false;
        }
    }

    /**
     * Destroy the internal state of the ContentView. This method may only be
     * called after the ContentView has been removed from the view system. No
     * other methods may be called on this ContentView after this method has
     * been called.
     */
    public void destroy() {
        hidePopupDialog();
        mCleanupReference.cleanupNow();
        mNativeContentViewCore = 0;
        // Do not propagate the destroy() to settings, as the client may still hold a reference to
        // that and could still be using it.
        mContentSettings = null;
    }

    /**
     * Returns true initially, false after destroy() has been called.
     * It is illegal to call any other public method after destroy().
     */
    public boolean isAlive() {
        return mNativeContentViewCore != 0;
    }

    /**
     * For internal use. Throws IllegalStateException if mNativeContentView is 0.
     * Use this to ensure we get a useful Java stack trace, rather than a native
     * crash dump, from use-after-destroy bugs in Java code.
     */
    void checkIsAlive() throws IllegalStateException {
        if (!isAlive()) {
            throw new IllegalStateException("ContentView used after destroy() was called");
        }
    }

    public void setContentViewClient(ContentViewClient client) {
        if (client == null) {
            throw new IllegalArgumentException("The client can't be null.");
        }
        mContentViewClient = client;
        if (mNativeContentViewCore != 0) {
            nativeSetClient(mNativeContentViewCore, mContentViewClient);
        }
    }

    ContentViewClient getContentViewClient() {
        if (mContentViewClient == null) {
            // We use the Null Object pattern to avoid having to perform a null check in this class.
            // We create it lazily because most of the time a client will be set almost immediately
            // after ContentView is created.
            mContentViewClient = new ContentViewClient();
            // We don't set the native ContentViewClient pointer here on purpose. The native
            // implementation doesn't mind a null delegate and using one is better than passing a
            // Null Object, since we cut down on the number of JNI calls.
        }
        return mContentViewClient;
    }

    /**
     * Load url without fixing up the url string. Consumers of ContentView are responsible for
     * ensuring the URL passed in is properly formatted (i.e. the scheme has been added if left
     * off during user input).
     *
     * @param url The url to load.
     */
    public void loadUrlWithoutUrlSanitization(String url) {
        loadUrlWithoutUrlSanitization(url, PAGE_TRANSITION_TYPED);
    }

    /**
     * Load url without fixing up the url string. Consumers of ContentView are responsible for
     * ensuring the URL passed in is properly formatted (i.e. the scheme has been added if left
     * off during user input).
     *
     * @param url The url to load.
     * @param pageTransition Page transition id that describes the action that led to this
     *                       navigation. It is important for ranking URLs in the history so the
     *                       omnibox can report suggestions correctly.
     */
    public void loadUrlWithoutUrlSanitization(String url, int pageTransition) {
        mAccessibilityInjector.addOrRemoveAccessibilityApisIfNecessary();
        if (mNativeContentViewCore != 0) {
            if (isPersonalityView()) {
                nativeLoadUrlWithoutUrlSanitizationWithUserAgentOverride(
                        mNativeContentViewCore,
                        url,
                        pageTransition,
                        mContentSettings.getUserAgentString());
            } else {
                // Chrome stores overridden UA strings in navigation history
                // items, so they stay the same on going back / forward.
                nativeLoadUrlWithoutUrlSanitization(
                        mNativeContentViewCore,
                        url,
                        pageTransition);
            }
        }
    }

    void setAllUserAgentOverridesInHistory() {
        // TODO(tedchoc): Pass user agent override down to native.
    }

    /**
     * Stops loading the current web contents.
     */
    public void stopLoading() {
        if (mNativeContentViewCore != 0) nativeStopLoading(mNativeContentViewCore);
    }

    /**
     * Get the URL of the current page.
     *
     * @return The URL of the current page.
     */
    public String getUrl() {
        if (mNativeContentViewCore != 0) return nativeGetURL(mNativeContentViewCore);
        return null;
    }

    /**
     * Get the title of the current page.
     *
     * @return The title of the current page.
     */
    public String getTitle() {
        if (mNativeContentViewCore != 0) return nativeGetTitle(mNativeContentViewCore);
        return null;
    }

    /**
     * @return The load progress of current web contents (range is 0 - 100).
     */
    public int getProgress() {
        if (mNativeContentViewCore != 0) {
            return (int) (100.0 * nativeGetLoadProgress(mNativeContentViewCore));
        }
        return 100;
    }

    public int getWidth() {
        return mContainerView.getWidth();
    }

    public int getHeight() {
        return mContainerView.getHeight();
    }

    /**
     * @return Whether the current WebContents has a previous navigation entry.
     */
    public boolean canGoBack() {
        return mNativeContentViewCore != 0 && nativeCanGoBack(mNativeContentViewCore);
    }

    /**
     * @return Whether the current WebContents has a navigation entry after the current one.
     */
    public boolean canGoForward() {
        return mNativeContentViewCore != 0 && nativeCanGoForward(mNativeContentViewCore);
    }

    /**
     * @param offset The offset into the navigation history.
     * @return Whether we can move in history by given offset
     */
    public boolean canGoToOffset(int offset) {
        return mNativeContentViewCore != 0 && nativeCanGoToOffset(mNativeContentViewCore, offset);
    }

    /**
     * Navigates to the specified offset from the "current entry". Does nothing if the offset is out
     * of bounds.
     * @param offset The offset into the navigation history.
     */
    public void goToOffset(int offset) {
        if (mNativeContentViewCore != 0) nativeGoToOffset(mNativeContentViewCore, offset);
    }

    /**
     * Goes to the navigation entry before the current one.
     */
    public void goBack() {
        if (mNativeContentViewCore != 0) nativeGoBack(mNativeContentViewCore);
    }

    /**
     * Goes to the navigation entry following the current one.
     */
    public void goForward() {
        if (mNativeContentViewCore != 0) nativeGoForward(mNativeContentViewCore);
    }

    /**
     * Reload the current page.
     */
    public void reload() {
        mAccessibilityInjector.addOrRemoveAccessibilityApisIfNecessary();
        if (mNativeContentViewCore != 0) nativeReload(mNativeContentViewCore);
    }

    /**
     * Clears the ContentViewCore's page history in both the backwards and
     * forwards directions.
     */
    public void clearHistory() {
        if (mNativeContentViewCore != 0) nativeClearHistory(mNativeContentViewCore);
    }

    String getSelectedText() {
        return mHasSelection ? mLastSelectedText : "";
    }

    // End FrameLayout overrides.


    /**
     * @see View#onTouchEvent(MotionEvent)
     */
    public boolean onTouchEvent(MotionEvent event) {
        return mContentViewGestureHandler.onTouchEvent(event);
    }

    /**
     * @return ContentViewGestureHandler for all MotionEvent and gesture related calls.
     */
    ContentViewGestureHandler getContentViewGestureHandler() {
        return mContentViewGestureHandler;
    }

    @Override
    public boolean sendTouchEvent(long timeMs, int action, TouchPoint[] pts) {
        if (mNativeContentViewCore != 0) {
            return nativeTouchEvent(mNativeContentViewCore, timeMs, action, pts);
        }
        return false;
    }

    @SuppressWarnings("unused")
    @CalledByNative
    private void didSetNeedTouchEvents(boolean needTouchEvents) {
        mContentViewGestureHandler.didSetNeedTouchEvents(needTouchEvents);
    }

    @SuppressWarnings("unused")
    @CalledByNative
    private void confirmTouchEvent(boolean handled) {
        mContentViewGestureHandler.confirmTouchEvent(handled);
    }

    @Override
    public boolean sendGesture(int type, long timeMs, int x, int y, Bundle b) {
        if (mNativeContentViewCore == 0) return false;

        switch (type) {
            case ContentViewGestureHandler.GESTURE_SHOW_PRESSED_STATE:
                nativeShowPressState(mNativeContentViewCore, timeMs, x, y);
                return true;
            case ContentViewGestureHandler.GESTURE_DOUBLE_TAP:
                nativeDoubleTap(mNativeContentViewCore, timeMs, x, y);
                return true;
            case ContentViewGestureHandler.GESTURE_SINGLE_TAP_UP:
                nativeSingleTap(mNativeContentViewCore, timeMs, x, y, false);
                return true;
            case ContentViewGestureHandler.GESTURE_SINGLE_TAP_CONFIRMED:
                handleTapOrPress(timeMs, x, y, false,
                        b.getBoolean(ContentViewGestureHandler.SHOW_PRESS, false));
                return true;
            case ContentViewGestureHandler.GESTURE_LONG_PRESS:
                handleTapOrPress(timeMs, x, y, true, false);
                return true;
            case ContentViewGestureHandler.GESTURE_SCROLL_START:
                nativeScrollBegin(mNativeContentViewCore, timeMs, x, y);
                return true;
            case ContentViewGestureHandler.GESTURE_SCROLL_BY:
                nativeScrollBy(mNativeContentViewCore, timeMs, x, y);
                return true;
            case ContentViewGestureHandler.GESTURE_SCROLL_END:
                nativeScrollEnd(mNativeContentViewCore, timeMs);
                return true;
            case ContentViewGestureHandler.GESTURE_FLING_START:
                nativeFlingStart(mNativeContentViewCore, timeMs, x, y,
                        clampFlingVelocityX(b.getInt(ContentViewGestureHandler.VELOCITY_X, 0)),
                        clampFlingVelocityY(b.getInt(ContentViewGestureHandler.VELOCITY_Y, 0)));
                return true;
            case ContentViewGestureHandler.GESTURE_FLING_CANCEL:
                nativeFlingCancel(mNativeContentViewCore, timeMs);
                return true;
            case ContentViewGestureHandler.GESTURE_PINCH_BEGIN:
                nativePinchBegin(mNativeContentViewCore, timeMs, x, y);
                return true;
            case ContentViewGestureHandler.GESTURE_PINCH_BY:
                nativePinchBy(mNativeContentViewCore, timeMs, x, y,
                        b.getFloat(ContentViewGestureHandler.DELTA, 0));
                return true;
            case ContentViewGestureHandler.GESTURE_PINCH_END:
                nativePinchEnd(mNativeContentViewCore, timeMs);
                return true;
            default:
                return false;
        }
    }

    /**
     * Injects the passed JavaScript code in the current page and evaluates it.
     * Once evaluated, an asynchronous call to
     * ContentViewClient.onJavaScriptEvaluationResult is made. Used in automation
     * tests.
     *
     * @return an id that is passed along in the asynchronous onJavaScriptEvaluationResult callback
     * @throws IllegalStateException If the ContentView has been destroyed.
     * @hide
     */
    public int evaluateJavaScript(String script) throws IllegalStateException {
        checkIsAlive();
        return nativeEvaluateJavaScript(script);
    }

    /**
     * This method should be called when the containing activity is paused.
     */
    public void onActivityPause() {
        TraceEvent.begin();
        hidePopupDialog();
        setAccessibilityState(false);
        TraceEvent.end();
    }

    /**
     * This method should be called when the containing activity is resumed.
     */
    public void onActivityResume() {
        setAccessibilityState(true);
    }

    /**
     * To be called when the ContentView is shown.
     */
    public void onShow() {
        setAccessibilityState(true);
    }

    /**
     * To be called when the ContentView is hidden.
     */
    public void onHide() {
        hidePopupDialog();
        setAccessibilityState(false);
    }

    /**
     * Return the ContentSettings object used to control the settings for this
     * ContentViewCore.
     *
     * Note that when ContentView is used in the PERSONALITY_CHROME role,
     * ContentSettings can only be used for retrieving settings values. For
     * modifications, ChromeNativePreferences is to be used.
     * @return A ContentSettings object that can be used to control this
     *         ContentViewCore's settings.
     */
    public ContentSettings getContentSettings() {
        return mContentSettings;
    }

    @Override
    public boolean didUIStealScroll(float x, float y) {
        // TODO(yusufo): Stubbed out for now. Upstream when computeHorizontalScrollOffset is
        // available.
        return false;
    }

    private void hidePopupDialog() {
        SelectPopupDialog.hide(this);
    }

    /**
     * @see View#onAttachedToWindow()
     */
    @SuppressWarnings("javadoc")
    protected void onAttachedToWindow() {
        setAccessibilityState(true);
    }

    /**
     * @see View#onDetachedFromWindow()
     */
    @SuppressWarnings("javadoc")
    protected void onDetachedFromWindow() {
        setAccessibilityState(false);
    }

    // End FrameLayout overrides.

    /**
     * @see View#awakenScrollBars(int, boolean)
     */
    @SuppressWarnings("javadoc")
    protected boolean awakenScrollBars(int startDelay, boolean invalidate) {
        // For the default implementation of ContentView which draws the scrollBars on the native
        // side, calling this function may get us into a bad state where we keep drawing the
        // scrollBars, so disable it by always returning false.
        if (mContainerView.getScrollBarStyle() == View.SCROLLBARS_INSIDE_OVERLAY) {
            return false;
        } else {
            return mContainerViewInternals.super_awakenScrollBars(startDelay, invalidate);
        }
    }

    private void handleTapOrPress(
            long timeMs, int x, int y, boolean isLongPress, boolean showPress) {
        //TODO(yusufo):Upstream the rest of the bits about handlerControllers.
        if (!mContainerView.isFocused()) mContainerView.requestFocus();

        if (isLongPress) {
            if (mNativeContentViewCore != 0) {
                nativeLongPress(mNativeContentViewCore, timeMs, x, y, false);
            }
        } else {
            if (!showPress && mNativeContentViewCore != 0) {
                nativeShowPressState(mNativeContentViewCore, timeMs, x, y);
            }
            if (mNativeContentViewCore != 0) {
                nativeSingleTap(mNativeContentViewCore, timeMs, x, y, false);
            }
        }
    }

    void updateMultiTouchZoomSupport() {
        mZoomManager.updateMultiTouchSupport();
    }

    public boolean isMultiTouchZoomSupported() {
        return mZoomManager.isMultiTouchZoomSupported();
    }

    void selectPopupMenuItems(int[] indices) {
        if (mNativeContentViewCore != 0) {
            nativeSelectPopupMenuItems(mNativeContentViewCore, indices);
        }
    }

    /*
     * To avoid checkerboard, we clamp the fling velocity based on the maximum number of tiles
     * allowed to be uploaded per 100ms. Calculation is limited to one direction. We assume the
     * tile size is 256x256. The precise distance / velocity should be calculated based on the
     * logic in Scroller.java. As it is almost linear for the first 100ms, we use a simple math.
     */
    private int clampFlingVelocityX(int velocity) {
        int cols = MAX_NUM_UPLOAD_TILES / (int) (Math.ceil((float) getHeight() / 256) + 1);
        int maxVelocity = cols > 0 ? cols * 2560 : 1000;
        if (Math.abs(velocity) > maxVelocity) {
            return velocity > 0 ? maxVelocity : -maxVelocity;
        } else {
            return velocity;
        }
    }

    private int clampFlingVelocityY(int velocity) {
        int rows = MAX_NUM_UPLOAD_TILES / (int) (Math.ceil((float) getWidth() / 256) + 1);
        int maxVelocity = rows > 0 ? rows * 2560 : 1000;
        if (Math.abs(velocity) > maxVelocity) {
            return velocity > 0 ? maxVelocity : -maxVelocity;
        } else {
            return velocity;
        }
    }

    /**
     * Register the listener to be used when content can not be handled by the
     * rendering engine, and should be downloaded instead. This will replace the
     * current listener.
     * @param listener An implementation of DownloadListener.
     */
    // TODO(nileshagrawal): decide if setDownloadDelegate will be public API. If so,
    // this method should be deprecated and the javadoc should make reference to the
    // fact that a ContentViewDownloadDelegate will be used in preference to a
    // DownloadListener.
    public void setDownloadListener(DownloadListener listener) {
        mDownloadListener = listener;
    }

    // Called by DownloadController.
    DownloadListener downloadListener() {
        return mDownloadListener;
    }

    /**
     * Register the delegate to be used when content can not be handled by
     * the rendering engine, and should be downloaded instead. This will replace
     * the current delegate or existing DownloadListner.
     * Embedders should prefer this over the legacy DownloadListener.
     * @param listener An implementation of ContentViewDownloadDelegate.
     */
    public void setDownloadDelegate(ContentViewDownloadDelegate delegate) {
        mDownloadDelegate = delegate;
    }

    // Called by DownloadController.
    ContentViewDownloadDelegate getDownloadDelegate() {
        return mDownloadDelegate;
    }

    private void showSelectActionBar() {
        if (mActionMode != null) {
            mActionMode.invalidate();
            return;
        }

        // Start a new action mode with a SelectActionModeCallback.
        SelectActionModeCallback.ActionHandler actionHandler =
                new SelectActionModeCallback.ActionHandler() {
            @Override
            public boolean selectAll() {
                return mImeAdapter.selectAll();
            }

            @Override
            public boolean cut() {
                return mImeAdapter.cut();
            }

            @Override
            public boolean copy() {
                return mImeAdapter.copy();
            }

            @Override
            public boolean paste() {
                return mImeAdapter.paste();
            }

            @Override
            public boolean isSelectionEditable() {
                return mSelectionEditable;
            }

            @Override
            public String getSelectedText() {
                return ContentViewCore.this.getSelectedText();
            }

            @Override
            public void onDestroyActionMode() {
                mActionMode = null;
                mImeAdapter.unselect();
                getContentViewClient().onContextualActionBarHidden();
            }
        };
        mActionMode = mContainerView.startActionMode(
                getContentViewClient().getSelectActionModeCallback(getContext(), actionHandler,
                        nativeIsIncognito(mNativeContentViewCore)));
        if (mActionMode == null) {
            // There is no ActionMode, so remove the selection.
            mImeAdapter.unselect();
        } else {
            getContentViewClient().onContextualActionBarShown();
        }
    }

    /**
     * @return Whether the native ContentView has crashed.
     */
    public boolean isCrashed() {
        if (mNativeContentViewCore == 0) return false;
        return nativeCrashed(mNativeContentViewCore);
    }

    // The following methods are called by native through jni

    /**
     * Called (from native) when the <select> popup needs to be shown.
     * @param items           Items to show.
     * @param enabled         POPUP_ITEM_TYPEs for items.
     * @param multiple        Whether the popup menu should support multi-select.
     * @param selectedIndices Indices of selected items.
     */
    @SuppressWarnings("unused")
    @CalledByNative
    private void showSelectPopup(String[] items, int[] enabled, boolean multiple,
            int[] selectedIndices) {
        SelectPopupDialog.show(this, items, enabled, multiple, selectedIndices);
    }

    @SuppressWarnings("unused")
    @CalledByNative
    private void onEvaluateJavaScriptResult(int id, String jsonResult) {
        getContentViewClient().onEvaluateJavaScriptResult(id, jsonResult);
    }

    /**
     * Called (from native) when page loading begins.
     */
    @SuppressWarnings("unused")
    @CalledByNative
    private void didStartLoading() {
        hidePopupDialog();
    }

    /**
     * @return Whether a reload happens when this ContentView is activated.
     */
    public boolean needsReload() {
        return mNativeContentViewCore != 0 && nativeNeedsReload(mNativeContentViewCore);
    }

    /**
     * Checks whether the ContentViewCore can be zoomed in.
     *
     * @return True if the ContentViewCore can be zoomed in.
     */
    // This method uses the term 'zoom' for legacy reasons, but relates
    // to what chrome calls the 'page scale factor'.
    public boolean canZoomIn() {
        return mNativeMaximumScale - mNativePageScaleFactor > ZOOM_CONTROLS_EPSILON;
    }

    /**
     * Checks whether the ContentViewCore can be zoomed out.
     *
     * @return True if the ContentViewCore can be zoomed out.
     */
    // This method uses the term 'zoom' for legacy reasons, but relates
    // to what chrome calls the 'page scale factor'.
    public boolean canZoomOut() {
        return mNativePageScaleFactor - mNativeMinimumScale > ZOOM_CONTROLS_EPSILON;
    }

    /**
     * Zooms in the ContentViewCore by 25% (or less if that would result in
     * zooming in more than possible).
     *
     * @return True if there was a zoom change, false otherwise.
     */
    // This method uses the term 'zoom' for legacy reasons, but relates
    // to what chrome calls the 'page scale factor'.
    public boolean zoomIn() {
        if (!canZoomIn()) {
            return false;
        }

        if (mNativeContentViewCore == 0) {
            return false;
        }

        long timeMs = System.currentTimeMillis();
        int x = getWidth() / 2;
        int y = getHeight() / 2;
        float delta = 1.25f;

        getContentViewGestureHandler().pinchBegin(timeMs, x, y);
        getContentViewGestureHandler().pinchBy(timeMs, x, y, delta);
        getContentViewGestureHandler().pinchEnd(timeMs);

        return true;
    }

    /**
     * Zooms out the ContentViewCore by 20% (or less if that would result in
     * zooming out more than possible).
     *
     * @return True if there was a zoom change, false otherwise.
     */
    // This method uses the term 'zoom' for legacy reasons, but relates
    // to what chrome calls the 'page scale factor'.
    public boolean zoomOut() {
        if (!canZoomOut()) {
            return false;
        }

        if (mNativeContentViewCore == 0) {
            return false;
        }

        long timeMs = System.currentTimeMillis();
        int x = getWidth() / 2;
        int y = getHeight() / 2;
        float delta = 0.8f;

        getContentViewGestureHandler().pinchBegin(timeMs, x, y);
        getContentViewGestureHandler().pinchBy(timeMs, x, y, delta);
        getContentViewGestureHandler().pinchEnd(timeMs);

        return true;
    }

    /**
     * Invokes the graphical zoom picker widget for this ContentView.
     */
    @Override
    public void invokeZoomPicker() {
        if (mContentSettings.supportZoom()) {
            mZoomManager.invokeZoomPicker();
        }
    }

    // Unlike legacy WebView getZoomControls which returns external zoom controls,
    // this method returns built-in zoom controls. This method is used in tests.
    public View getZoomControlsForTest() {
        return mZoomManager.getZoomControlsViewForTest();
    }

    /**
     * This method injects the supplied Java object into the ContentViewCore.
     * The object is injected into the JavaScript context of the main frame,
     * using the supplied name. This allows the Java object to be accessed from
     * JavaScript. Note that that injected objects will not appear in
     * JavaScript until the page is next (re)loaded. For example:
     * <pre> view.addJavascriptInterface(new Object(), "injectedObject");
     * view.loadData("<!DOCTYPE html><title></title>", "text/html", null);
     * view.loadUrl("javascript:alert(injectedObject.toString())");</pre>
     * <p><strong>IMPORTANT:</strong>
     * <ul>
     * <li> addJavascriptInterface() can be used to allow JavaScript to control
     * the host application. This is a powerful feature, but also presents a
     * security risk. Use of this method in a ContentViewCore containing
     * untrusted content could allow an attacker to manipulate the host
     * application in unintended ways, executing Java code with the permissions
     * of the host application. Use extreme care when using this method in a
     * ContentViewCore which could contain untrusted content. Particular care
     * should be taken to avoid unintentional access to inherited methods, such
     * as {@link Object#getClass()}. To prevent access to inherited methods,
     * set {@code allowInheritedMethods} to {@code false}. In addition, ensure
     * that the injected object's public methods return only objects designed
     * to be used by untrusted code, and never return a raw Object instance.
     * <li> JavaScript interacts with Java objects on a private, background
     * thread of the ContentViewCore. Care is therefore required to maintain
     * thread safety.</li>
     * </ul></p>
     *
     * @param object The Java object to inject into the ContentViewCore's
     *               JavaScript context. Null values are ignored.
     * @param name The name used to expose the instance in JavaScript.
     * @param allowInheritedMethods Whether or not inherited methods may be
     *                              called from JavaScript.
     */
    public void addJavascriptInterface(Object object, String name, boolean allowInheritedMethods) {
        if (mNativeContentViewCore != 0 && object != null) {
            nativeAddJavascriptInterface(mNativeContentViewCore, object, name,
                    allowInheritedMethods);
        }
    }

    /**
     * Removes a previously added JavaScript interface with the given name.
     *
     * @param name The name of the interface to remove.
     */
    public void removeJavascriptInterface(String name) {
        if (mNativeContentViewCore != 0) {
            nativeRemoveJavascriptInterface(mNativeContentViewCore, name);
        }
    }

    @CalledByNative
    private void startContentIntent(String contentUrl) {
        getContentViewClient().onStartContentIntent(getContext(), contentUrl);
    }

    /**
     * Determines whether or not this ContentViewCore can handle this accessibility action.
     * @param action The action to perform.
     * @return Whether or not this action is supported.
     */
    public boolean supportsAccessibilityAction(int action) {
        return mAccessibilityInjector.supportsAccessibilityAction(action);
    }

    /**
     * Attempts to perform an accessibility action on the web content.  If the accessibility action
     * cannot be processed, it returns {@code null}, allowing the caller to know to call the
     * super {@link View#performAccessibilityAction(int, Bundle)} method and use that return value.
     * Otherwise the return value from this method should be used.
     * @param action The action to perform.
     * @param arguments Optional action arguments.
     * @return Whether the action was performed or {@code null} if the call should be delegated to
     *         the super {@link View} class.
     */
    public boolean performAccessibilityAction(int action, Bundle arguments) {
        if (mAccessibilityInjector.supportsAccessibilityAction(action)) {
            return mAccessibilityInjector.performAccessibilityAction(action, arguments);
        }

        return false;
    }

    /**
     * @see View#onInitializeAccessibilityNodeInfo(AccessibilityNodeInfo)
     */
    public void onInitializeAccessibilityNodeInfo(AccessibilityNodeInfo info) {
        mAccessibilityInjector.onInitializeAccessibilityNodeInfo(info);

        // TODO(dtrainor): Upstream accessibility scrolling event information once that data is
        // available in ContentViewCore.  Currently internal scrolling variables aren't upstreamed.
    }

    /**
     * @see View#onInitializeAccessibilityEvent(AccessibilityEvent)
     */
    public void onInitializeAccessibilityEvent(AccessibilityEvent event) {
        event.setClassName(this.getClass().getName());
    }

    /**
     * Enable or disable accessibility features.
     */
    public void setAccessibilityState(boolean state) {
        mAccessibilityInjector.setScriptEnabled(state);
    }

    // The following methods are implemented at native side.

    /**
     * Initialize the ContentView native side.
     * Should be called with a valid native WebContents.
     * If nativeInitProcess is never called, the first time this method is called,
     * nativeInitProcess will be called implicitly with the default settings.
     * @param webContentsPtr the ContentView does not create a new native WebContents and uses
     *                       the provided one.
     * @return a native pointer to the native ContentView object.
     */
    private native int nativeInit(int webContentsPtr);

    private static native void nativeDestroy(int nativeContentViewCoreImpl);

    private native void nativeLoadUrlWithoutUrlSanitization(int nativeContentViewCoreImpl,
            String url, int pageTransition);
    private native void nativeLoadUrlWithoutUrlSanitizationWithUserAgentOverride(
            int nativeContentViewCoreImpl, String url, int pageTransition,
            String userAgentOverride);

    private native String nativeGetURL(int nativeContentViewCoreImpl);

    private native String nativeGetTitle(int nativeContentViewCoreImpl);

    private native double nativeGetLoadProgress(int nativeContentViewCoreImpl);

    private native boolean nativeIsIncognito(int nativeContentViewCoreImpl);

    // Returns true if the native side crashed so that java side can draw a sad tab.
    private native boolean nativeCrashed(int nativeContentViewCoreImpl);

    private native boolean nativeTouchEvent(int nativeContentViewCoreImpl,
            long timeMs, int action,
            TouchPoint[] pts);

    private native void nativeScrollBegin(int nativeContentViewCoreImpl, long timeMs, int x, int y);

    private native void nativeScrollEnd(int nativeContentViewCoreImpl, long timeMs);

    private native void nativeScrollBy(
            int nativeContentViewCoreImpl, long timeMs, int deltaX, int deltaY);

    private native void nativeFlingStart(
            int nativeContentViewCoreImpl, long timeMs, int x, int y, int vx, int vy);

    private native void nativeFlingCancel(int nativeContentViewCoreImpl, long timeMs);

    private native void nativeSingleTap(
            int nativeContentViewCoreImpl, long timeMs, int x, int y, boolean linkPreviewTap);

    private native void nativeShowPressState(
            int nativeContentViewCoreImpl, long timeMs, int x, int y);

    private native void nativeDoubleTap(int nativeContentViewCoreImpl, long timeMs, int x, int y);

    private native void nativeLongPress(int nativeContentViewCoreImpl, long timeMs, int x, int y,
            boolean linkPreviewTap);

    private native void nativePinchBegin(int nativeContentViewCoreImpl, long timeMs, int x, int y);

    private native void nativePinchEnd(int nativeContentViewCoreImpl, long timeMs);

    private native void nativePinchBy(int nativeContentViewCoreImpl, long timeMs,
            int anchorX, int anchorY, float deltaScale);

    private native boolean nativeCanGoBack(int nativeContentViewCoreImpl);

    private native boolean nativeCanGoForward(int nativeContentViewCoreImpl);

    private native boolean nativeCanGoToOffset(int nativeContentViewCoreImpl, int offset);

    private native void nativeGoToOffset(int nativeContentViewCoreImpl, int offset);

    private native void nativeGoBack(int nativeContentViewCoreImpl);

    private native void nativeGoForward(int nativeContentViewCoreImpl);

    private native void nativeStopLoading(int nativeContentViewCoreImpl);

    private native void nativeReload(int nativeContentViewCoreImpl);

    private native void nativeSelectPopupMenuItems(int nativeContentViewCoreImpl, int[] indices);

    private native void nativeSetClient(int nativeContentViewCoreImpl, ContentViewClient client);

    private native boolean nativeNeedsReload(int nativeContentViewCoreImpl);

    private native void nativeClearHistory(int nativeContentViewCoreImpl);

    private native int nativeEvaluateJavaScript(String script);

    private native void nativeAddJavascriptInterface(int nativeContentViewCoreImpl, Object object,
                                                     String name, boolean allowInheritedMethods);

    private native void nativeRemoveJavascriptInterface(int nativeContentViewCoreImpl, String name);
}
