// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content_shell;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.text.TextUtils;
import android.util.Log;
import android.view.KeyEvent;

import org.chromium.base.ChromiumActivity;
import org.chromium.content.app.LibraryLoader;
import org.chromium.content.browser.ActivityContentVideoViewDelegate;
import org.chromium.content.browser.ContentVideoView;
import org.chromium.content.browser.ContentView;
import org.chromium.content.browser.DeviceUtils;
import org.chromium.content.common.CommandLine;
import org.chromium.ui.gfx.ActivityNativeWindow;

/**
 * Activity for managing the Content Shell.
 */
public class ContentShellActivity extends ChromiumActivity {

    public static final String COMMAND_LINE_FILE = "/data/local/tmp/content-shell-command-line";
    private static final String TAG = ContentShellActivity.class.getName();

    private static final String ACTIVE_SHELL_URL_KEY = "activeUrl";
    public static final String DEFAULT_SHELL_URL = "http://www.google.com";
    public static final String COMMAND_LINE_ARGS_KEY = "commandLineArgs";

    private ShellManager mShellManager;
    private ActivityNativeWindow mActivityNativeWindow;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Initializing the command line must occur before loading the library.
        if (!CommandLine.isInitialized()) {
            CommandLine.initFromFile(COMMAND_LINE_FILE);
            String[] commandLineParams = getCommandLineParamsFromIntent(getIntent());
            if (commandLineParams != null) {
                CommandLine.getInstance().appendSwitchesAndArguments(commandLineParams);
            }
        }
        waitForDebuggerIfNeeded();

        DeviceUtils.addDeviceSpecificUserAgentSwitch(this);

        LibraryLoader.ensureInitialized();

        setContentView(R.layout.content_shell_activity);
        mShellManager = (ShellManager) findViewById(R.id.shell_container);
        mActivityNativeWindow = new ActivityNativeWindow(this);
        mActivityNativeWindow.restoreInstanceState(savedInstanceState);
        mShellManager.setWindow(mActivityNativeWindow);
        ContentVideoView.registerContentVideoViewContextDelegate(
            new ActivityContentVideoViewDelegate(this));

        String startupUrl = getUrlFromIntent(getIntent());
        if (!TextUtils.isEmpty(startupUrl)) {
            mShellManager.setStartupUrl(Shell.sanitizeUrl(startupUrl));
        }

        if (!ContentView.enableMultiProcess(this, ContentView.MAX_RENDERERS_AUTOMATIC)) {
            String shellUrl = DEFAULT_SHELL_URL;
            if (savedInstanceState != null
                    && savedInstanceState.containsKey(ACTIVE_SHELL_URL_KEY)) {
                shellUrl = savedInstanceState.getString(ACTIVE_SHELL_URL_KEY);
            }
            mShellManager.launchShell(shellUrl);
        }
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
        Shell activeShell = getActiveShell();
        if (activeShell != null) {
            outState.putString(ACTIVE_SHELL_URL_KEY, activeShell.getContentView().getUrl());
        }

        mActivityNativeWindow.saveInstanceState(outState);
    }

    private void waitForDebuggerIfNeeded() {
        if (CommandLine.getInstance().hasSwitch(CommandLine.WAIT_FOR_JAVA_DEBUGGER)) {
            Log.e(TAG, "Waiting for Java debugger to connect...");
            android.os.Debug.waitForDebugger();
            Log.e(TAG, "Java debugger connected. Resuming execution.");
        }
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (keyCode != KeyEvent.KEYCODE_BACK) return super.onKeyUp(keyCode, event);

        Shell activeView = getActiveShell();
        if (activeView != null && activeView.getContentView().canGoBack()) {
            activeView.getContentView().goBack();
            return true;
        }

        return super.onKeyUp(keyCode, event);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        if (getCommandLineParamsFromIntent(intent) != null) {
            Log.i(TAG, "Ignoring command line params: can only be set when creating the activity.");
        }

        String url = getUrlFromIntent(intent);
        if (!TextUtils.isEmpty(url)) {
            Shell activeView = getActiveShell();
            if (activeView != null) {
                activeView.loadUrl(url);
            }
        }
    }

    @Override
    protected void onPause() {
        ContentView view = getActiveContentView();
        if (view != null) view.onActivityPause();

        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();

        ContentView view = getActiveContentView();
        if (view != null) view.onActivityResume();
    }

    @Override
    public void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        mActivityNativeWindow.onActivityResult(requestCode, resultCode, data);
    }

    private static String getUrlFromIntent(Intent intent) {
        return intent != null ? intent.getDataString() : null;
    }

    private static String[] getCommandLineParamsFromIntent(Intent intent) {
        return intent != null ? intent.getStringArrayExtra(COMMAND_LINE_ARGS_KEY) : null;
    }

    /**
     * @return The {@link ShellManager} configured for the activity or null if it has not been
     *         created yet.
     */
    public ShellManager getShellManager() {
        return mShellManager;
    }

    /**
     * @return The currently visible {@link Shell} or null if one is not showing.
     */
    public Shell getActiveShell() {
        return mShellManager != null ? mShellManager.getActiveShell() : null;
    }

    /**
     * @return The {@link ContentView} owned by the currently visible {@link Shell} or null if one
     *         is not showing.
     */
    public ContentView getActiveContentView() {
        Shell shell = getActiveShell();
        return shell != null ? shell.getContentView() : null;
    }
}
