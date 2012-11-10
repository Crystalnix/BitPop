// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser;

import android.content.Context;
import android.content.res.Configuration;
import android.graphics.Canvas;
import android.os.Build;
import android.util.AttributeSet;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.webkit.DownloadListener;
import android.widget.FrameLayout;

import org.chromium.content.browser.ContentViewCore;

/**
 * The containing view for {@link ContentViewCore} that exists in the Android UI hierarchy and
 * exposes the various {@link View} functionality to it.
 *
 * TODO(joth): Remove any methods overrides from this class that were added for WebView
 *             compatibility.
 */
public class ContentView extends FrameLayout implements ContentViewCore.InternalAccessDelegate {

    // The following constants match the ones in chrome/common/page_transition_types.h.
    // Add more if you need them.
    public static final int PAGE_TRANSITION_LINK = 0;
    public static final int PAGE_TRANSITION_TYPED = 1;
    public static final int PAGE_TRANSITION_AUTO_BOOKMARK = 2;
    public static final int PAGE_TRANSITION_START_PAGE = 6;

    /** Translate the find selection into a normal selection. */
    public static final int FIND_SELECTION_ACTION_KEEP_SELECTION =
            ContentViewCore.FIND_SELECTION_ACTION_KEEP_SELECTION;
    /** Clear the find selection. */
    public static final int FIND_SELECTION_ACTION_CLEAR_SELECTION =
            ContentViewCore.FIND_SELECTION_ACTION_CLEAR_SELECTION;
    /** Focus and click the selected node (for links). */
    public static final int FIND_SELECTION_ACTION_ACTIVATE_SELECTION =
            ContentViewCore.FIND_SELECTION_ACTION_ACTIVATE_SELECTION;

    // Used when ContentView implements a standalone View.
    public static final int PERSONALITY_VIEW = ContentViewCore.PERSONALITY_VIEW;
    // Used for Chrome.
    public static final int PERSONALITY_CHROME = ContentViewCore.PERSONALITY_CHROME;

    /**
     * Automatically decide the number of renderer processes to use based on device memory class.
     * */
    public static final int MAX_RENDERERS_AUTOMATIC = AndroidBrowserProcess.MAX_RENDERERS_AUTOMATIC;
    /**
     * Use single-process mode that runs the renderer on a separate thread in the main application.
     */
    public static final int MAX_RENDERERS_SINGLE_PROCESS =
            AndroidBrowserProcess.MAX_RENDERERS_SINGLE_PROCESS;
    /**
     * Cap on the maximum number of renderer processes that can be requested.
     */
    public static final int MAX_RENDERERS_LIMIT = AndroidBrowserProcess.MAX_RENDERERS_LIMIT;

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
        return ContentViewCore.enableMultiProcess(context, maxRendererProcesses);
    }

    /**
     * Initialize the process as the platform browser. This must be called before accessing
     * ContentView in order to treat this as a Chromium browser process.
     *
     * @param context Context used to obtain the application context.
     * @param maxRendererProcesses Same as ContentView.enableMultiProcess()
     * @return Whether the process actually needed to be initialized (false if already running).
     * @hide Only used by the platform browser.
     */
    public static boolean initChromiumBrowserProcess(Context context, int maxRendererProcesses) {
        return ContentViewCore.initChromiumBrowserProcess(context, maxRendererProcesses);
    }

    private ContentViewCore mContentViewCore;

    /**
     * Creates an instance of a ContentView.
     * @param context The Context the view is running in, through which it can
     *                access the current theme, resources, etc.
     * @param nativeWebContents A pointer to the native web contents.
     * @param personality One of {@link #PERSONALITY_CHROME} or {@link #PERSONALITY_VIEW}.
     * @return A ContentView instance.
     */
    public static ContentView newInstance(Context context, int nativeWebContents, int personality) {
        return newInstance(context, nativeWebContents, null, android.R.attr.webViewStyle,
                personality);
    }

    /**
     * Creates an instance of a ContentView.
     * @param context The Context the view is running in, through which it can
     *                access the current theme, resources, etc.
     * @param nativeWebContents A pointer to the native web contents.
     * @param attrs The attributes of the XML tag that is inflating the view.
     * @return A ContentView instance.
     */
    public static ContentView newInstance(Context context, int nativeWebContents,
            AttributeSet attrs) {
        // TODO(klobag): use the WebViewStyle as the default style for now. It enables scrollbar.
        // When ContentView is moved to framework, we can define its own style in the res.
        return newInstance(context, nativeWebContents, attrs, android.R.attr.webViewStyle);
    }

    /**
     * Creates an instance of a ContentView.
     * @param context The Context the view is running in, through which it can
     *                access the current theme, resources, etc.
     * @param nativeWebContents A pointer to the native web contents.
     * @param attrs The attributes of the XML tag that is inflating the view.
     * @param defStyle The default style to apply to this view.
     * @return A ContentView instance.
     */
    public static ContentView newInstance(Context context, int nativeWebContents,
            AttributeSet attrs, int defStyle) {
        return newInstance(context, nativeWebContents, attrs, defStyle, PERSONALITY_VIEW);
    }

    private static ContentView newInstance(Context context, int nativeWebContents,
            AttributeSet attrs, int defStyle, int personality) {
        // TODO(dtrainor): Upstream JellyBean version of AccessibilityInjector when SDK is 16.
        return new ContentView(context, nativeWebContents, attrs, defStyle, personality);
    }

    protected ContentView(Context context, int nativeWebContents, AttributeSet attrs, int defStyle,
            int personality) {
        super(context, attrs, defStyle);

        mContentViewCore = new ContentViewCore(context, this, this, nativeWebContents, personality);
    }

    /**
     * @return The core component of the ContentView that handles JNI communication.  Should only be
     *         used for passing to native.
     */
    public ContentViewCore getContentViewCore() {
        return mContentViewCore;
    }

    /**
     * @return Whether the configured personality of this ContentView is {@link #PERSONALITY_VIEW}.
     */
    boolean isPersonalityView() {
        return mContentViewCore.isPersonalityView();
    }

    /**
     * Destroy the internal state of the WebView. This method may only be called
     * after the WebView has been removed from the view system. No other methods
     * may be called on this WebView after this method has been called.
     */
    public void destroy() {
        mContentViewCore.destroy();
    }

    /**
     * Returns true initially, false after destroy() has been called.
     * It is illegal to call any other public method after destroy().
     */
    public boolean isAlive() {
        return mContentViewCore.isAlive();
    }

    /**
     * For internal use. Throws IllegalStateException if mNativeContentView is 0.
     * Use this to ensure we get a useful Java stack trace, rather than a native
     * crash dump, from use-after-destroy bugs in Java code.
     */
    void checkIsAlive() throws IllegalStateException {
        mContentViewCore.checkIsAlive();
    }

    public void setContentViewClient(ContentViewClient client) {
        mContentViewCore.setContentViewClient(client);
    }

    ContentViewClient getContentViewClient() {
        return mContentViewCore.getContentViewClient();
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
        mContentViewCore.loadUrlWithoutUrlSanitization(url, pageTransition);
    }

    void setAllUserAgentOverridesInHistory() {
        mContentViewCore.setAllUserAgentOverridesInHistory();
    }

    /**
     * Stops loading the current web contents.
     */
    public void stopLoading() {
        mContentViewCore.stopLoading();
    }

    /**
     * Get the URL of the current page.
     *
     * @return The URL of the current page.
     */
    public String getUrl() {
        return mContentViewCore.getUrl();
    }

    /**
     * Get the title of the current page.
     *
     * @return The title of the current page.
     */
    public String getTitle() {
        return mContentViewCore.getTitle();
    }

    /**
     * @return The load progress of current web contents (range is 0 - 100).
     */
    public int getProgress() {
        return mContentViewCore.getProgress();
    }

    /**
     * @return Whether the current WebContents has a previous navigation entry.
     */
    public boolean canGoBack() {
        return mContentViewCore.canGoBack();
    }

    /**
     * @return Whether the current WebContents has a navigation entry after the current one.
     */
    public boolean canGoForward() {
        return mContentViewCore.canGoForward();
    }

    /**
     * @param offset The offset into the navigation history.
     * @return Whether we can move in history by given offset
     */
    public boolean canGoToOffset(int offset) {
        return mContentViewCore.canGoToOffset(offset);
    }

    /**
     * Navigates to the specified offset from the "current entry". Does nothing if the offset is out
     * of bounds.
     * @param offset The offset into the navigation history.
     */
    public void goToOffset(int offset) {
        mContentViewCore.goToOffset(offset);
    }

    /**
     * Goes to the navigation entry before the current one.
     */
    public void goBack() {
        mContentViewCore.goBack();
    }

    /**
     * Goes to the navigation entry following the current one.
     */
    public void goForward() {
        mContentViewCore.goForward();
    }

    /**
     * Reload the current page.
     */
    public void reload() {
        mContentViewCore.reload();
    }

    /**
     * Clears the WebView's page history in both the backwards and forwards
     * directions.
     */
    public void clearHistory() {
        mContentViewCore.clearHistory();
    }

    /**
     * Start pinch zoom. You must call {@link #pinchEnd} to stop.
     */
    void pinchBegin(long timeMs, int x, int y) {
        mContentViewCore.getContentViewGestureHandler().pinchBegin(timeMs, x, y);
    }

    /**
     * Stop pinch zoom.
     */
    void pinchEnd(long timeMs) {
        mContentViewCore.getContentViewGestureHandler().pinchEnd(timeMs);
    }

    void setIgnoreSingleTap(boolean value) {
        mContentViewCore.getContentViewGestureHandler().setIgnoreSingleTap(value);
    }

    /**
     * Modify the ContentView magnification level. The effect of calling this
     * method is exactly as after "pinch zoom".
     *
     * @param timeMs The event time in milliseconds.
     * @param delta The ratio of the new magnification level over the current
     *            magnification level.
     * @param anchorX The magnification anchor (X) in the current view
     *            coordinate.
     * @param anchorY The magnification anchor (Y) in the current view
     *            coordinate.
     */
    void pinchBy(long timeMs, int anchorX, int anchorY, float delta) {
        mContentViewCore.getContentViewGestureHandler().pinchBy(timeMs, anchorX, anchorY, delta);
    }

    /**
     * This method should be called when the containing activity is paused.
     **/
    public void onActivityPause() {
        mContentViewCore.onActivityPause();
    }

    /**
     * This method should be called when the containing activity is resumed.
     **/
    public void onActivityResume() {
        mContentViewCore.onActivityResume();
    }

    /**
     * To be called when the ContentView is shown.
     **/
    public void onShow() {
        mContentViewCore.onShow();
    }

    /**
     * To be called when the ContentView is hidden.
     **/
    public void onHide() {
        mContentViewCore.onHide();
    }

    /**
     * Return the ContentSettings object used to control the settings for this
     * WebView.
     *
     * Note that when ContentView is used in the PERSONALITY_CHROME role,
     * ContentSettings can only be used for retrieving settings values. For
     * modifications, ChromeNativePreferences is to be used.
     * @return A ContentSettings object that can be used to control this WebView's
     *         settings.
     */
    public ContentSettings getContentSettings() {
        return mContentViewCore.getContentSettings();
    }

    // FrameLayout overrides.

    // Needed by ContentViewCore.InternalAccessDelegate
    @Override
    public boolean drawChild(Canvas canvas, View child, long drawingTime) {
        return super.drawChild(canvas, child, drawingTime);
    }

    // Needed by ContentViewCore.InternalAccessDelegate
    @Override
    public void onScrollChanged(int l, int t, int oldl, int oldt) {
        super.onScrollChanged(l, t, oldl, oldt);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        return mContentViewCore.onTouchEvent(event);
    }
    // End FrameLayout overrides.

    @Override
    public boolean awakenScrollBars(int startDelay, boolean invalidate) {
        return mContentViewCore.awakenScrollBars(startDelay, invalidate);
    }

    @Override
    public boolean awakenScrollBars() {
        return super.awakenScrollBars();
    }

    @Override
    public void onInitializeAccessibilityNodeInfo(AccessibilityNodeInfo info) {
        super.onInitializeAccessibilityNodeInfo(info);
        mContentViewCore.onInitializeAccessibilityNodeInfo(info);
    }

    @Override
    public void onInitializeAccessibilityEvent(AccessibilityEvent event) {
        super.onInitializeAccessibilityEvent(event);
        mContentViewCore.onInitializeAccessibilityEvent(event);
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        mContentViewCore.onAttachedToWindow();
    }

    @Override
    protected void onDetachedFromWindow() {
        super.onDetachedFromWindow();
        mContentViewCore.onDetachedFromWindow();
    }

    void updateMultiTouchZoomSupport() {
        mContentViewCore.updateMultiTouchZoomSupport();
    }

    public boolean isMultiTouchZoomSupported() {
        return mContentViewCore.isMultiTouchZoomSupported();
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
        mContentViewCore.setDownloadListener(listener);
    }

    // Called by DownloadController.
    DownloadListener downloadListener() {
        return mContentViewCore.downloadListener();
    }

    /**
     * Register the delegate to be used when content can not be handled by
     * the rendering engine, and should be downloaded instead. This will replace
     * the current delegate or existing DownloadListner.
     * Embedders should prefer this over the legacy DownloadListener.
     * @param listener An implementation of ContentViewDownloadDelegate.
     */
    public void setDownloadDelegate(ContentViewDownloadDelegate delegate) {
        mContentViewCore.setDownloadDelegate(delegate);
    }

    // Called by DownloadController.
    ContentViewDownloadDelegate getDownloadDelegate() {
        return mContentViewCore.getDownloadDelegate();
    }

    /**
     * @return Whether the native ContentView has crashed.
     */
    public boolean isCrashed() {
        return mContentViewCore.isCrashed();
    }

    /**
     * @return Whether a reload happens when this ContentView is activated.
     */
    public boolean needsReload() {
        return mContentViewCore.needsReload();
    }

    /**
     * Checks whether the WebView can be zoomed in.
     *
     * @return True if the WebView can be zoomed in.
     */
    // This method uses the term 'zoom' for legacy reasons, but relates
    // to what chrome calls the 'page scale factor'.
    public boolean canZoomIn() {
        return mContentViewCore.canZoomIn();
    }

    /**
     * Checks whether the WebView can be zoomed out.
     *
     * @return True if the WebView can be zoomed out.
     */
    // This method uses the term 'zoom' for legacy reasons, but relates
    // to what chrome calls the 'page scale factor'.
    public boolean canZoomOut() {
        return mContentViewCore.canZoomOut();
    }

    /**
     * Zooms in the WebView by 25% (or less if that would result in zooming in
     * more than possible).
     *
     * @return True if there was a zoom change, false otherwise.
     */
    // This method uses the term 'zoom' for legacy reasons, but relates
    // to what chrome calls the 'page scale factor'.
    public boolean zoomIn() {
        return mContentViewCore.zoomIn();
    }

    /**
     * Zooms out the WebView by 20% (or less if that would result in zooming out
     * more than possible).
     *
     * @return True if there was a zoom change, false otherwise.
     */
    // This method uses the term 'zoom' for legacy reasons, but relates
    // to what chrome calls the 'page scale factor'.
    public boolean zoomOut() {
        return mContentViewCore.zoomOut();
    }

    // Invokes the graphical zoom picker widget for this ContentView.
    public void invokeZoomPicker() {
        mContentViewCore.invokeZoomPicker();
    }

    // Unlike legacy WebView getZoomControls which returns external zoom controls,
    // this method returns built-in zoom controls. This method is used in tests.
    public View getZoomControlsForTest() {
        return mContentViewCore.getZoomControlsForTest();
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////
    //              Start Implementation of ContentViewCore.InternalAccessDelegate               //
    ///////////////////////////////////////////////////////////////////////////////////////////////

    @Override
    public boolean super_onKeyUp(int keyCode, KeyEvent event) {
        return super.onKeyUp(keyCode, event);
    }

    @Override
    public boolean super_dispatchKeyEventPreIme(KeyEvent event) {
        return super.dispatchKeyEventPreIme(event);
    }

    @Override
    public boolean super_dispatchKeyEvent(KeyEvent event) {
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean super_onGenericMotionEvent(MotionEvent event) {
        return super.onGenericMotionEvent(event);
    }

    @Override
    public void super_onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
    }

    @Override
    public boolean super_awakenScrollBars(int startDelay, boolean invalidate) {
        return super.awakenScrollBars(startDelay, invalidate);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////
    //                End Implementation of ContentViewCore.InternalAccessDelegate               //
    ///////////////////////////////////////////////////////////////////////////////////////////////
}
