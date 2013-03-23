// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser.test.util;


import android.util.Log;

import org.chromium.content.browser.ContentView;
import org.chromium.content.browser.ContentViewCore;

import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

/**
 * This class is used to provide callback hooks for tests and related classes.
 */
public class TestCallbackHelperContainer {
    private TestContentViewClient mTestContentViewClient;
    private TestWebContentsObserver mTestWebContentsObserver;

    public TestCallbackHelperContainer(ContentView contentView) {
        mTestContentViewClient = new TestContentViewClient();
        contentView.getContentViewCore().setContentViewClient(mTestContentViewClient);
        mTestWebContentsObserver = new TestWebContentsObserver(contentView.getContentViewCore());
    }

    protected TestCallbackHelperContainer(
            TestContentViewClient viewClient, TestWebContentsObserver contentsObserver) {
        mTestContentViewClient = viewClient;
        mTestWebContentsObserver = contentsObserver;
    }

    public static class OnPageFinishedHelper extends CallbackHelper {
        private String mUrl;
        public void notifyCalled(String url) {
            mUrl = url;
            notifyCalled();
        }
        public String getUrl() {
            assert getCallCount() > 0;
            return mUrl;
        }
    }

    public static class OnPageStartedHelper extends CallbackHelper {
        private String mUrl;
        public void notifyCalled(String url) {
            mUrl = url;
            notifyCalled();
        }
        public String getUrl() {
            assert getCallCount() > 0;
            return mUrl;
        }
    }

    public static class OnReceivedErrorHelper extends CallbackHelper {
        private int mErrorCode;
        private String mDescription;
        private String mFailingUrl;
        public void notifyCalled(int errorCode, String description, String failingUrl) {
            mErrorCode = errorCode;
            mDescription = description;
            mFailingUrl = failingUrl;
            notifyCalled();
        }
        public int getErrorCode() {
            assert getCallCount() > 0;
            return mErrorCode;
        }
        public String getDescription() {
            assert getCallCount() > 0;
            return mDescription;
        }
        public String getFailingUrl() {
            assert getCallCount() > 0;
            return mFailingUrl;
        }
    }

    public static class OnEvaluateJavaScriptResultHelper extends CallbackHelper {
        private volatile Integer mRequestId;
        private volatile Integer mId;
        private String mJsonResult;

        /**
         * Starts evaluation of a given JavaScript code on a given contentViewCore.
         * @param contentViewCore A ContentViewCore instance to be used.
         * @param code A JavaScript code to be evaluated.
         */
        public void evaluateJavaScript(ContentViewCore contentViewCore, String code) {
            setRequestId(contentViewCore.evaluateJavaScript(code));
        }

        /**
         * Returns true if the evaluation started by evaluateJavaScript() has completed.
         */
        public boolean hasValue() {
            return mId != null;
        }

        /**
         * Returns the JSON result of a previously completed JavaScript evaluation and
         * resets the helper to accept new evaluations.
         * @return String JSON result of a previously completed JavaScript evaluation.
         */
        public String getJsonResultAndClear() {
            assert hasValue();
            String result = mJsonResult;
            setRequestId(null);
            return result;
        }


        /**
         * Returns a criteria that checks that the evaluation has finished.
         */
        public Criteria getHasValueCriteria() {
            return new Criteria() {
                @Override
                public boolean isSatisfied() {
                    return hasValue();
                }
            };
        }

        /**
         * Waits till the JavaScript evaluation finishes and returns true if a value was returned,
         * false if it timed-out.
         */
        public boolean waitUntilHasValue(long timeout, TimeUnit timeoutUnits)
                throws InterruptedException, TimeoutException {
            waitUntilCriteria(getHasValueCriteria(), timeout, timeoutUnits);
            return hasValue();
        }

        public boolean waitUntilHasValue() throws InterruptedException, TimeoutException {
            waitUntilCriteria(getHasValueCriteria());
            return hasValue();
        }

        private void setRequestId(Integer requestId) {
            mRequestId = requestId;
            mId = null;
            mJsonResult = null;
        }

        public void notifyCalled(int id, String jsonResult) {
            if (mRequestId == null) {
                Log.w("TestCallbackHelperContainer",
                        "Received JavaScript eval result when request id was not set");
                return;
            }
            if (id != mRequestId.intValue()) return;
            assert mId == null;
            mId = Integer.valueOf(id);
            mJsonResult = jsonResult;
            notifyCalled();
        }
    }

    public static class OnStartContentIntentHelper extends CallbackHelper {
        private String mIntentUrl;
        public void notifyCalled(String intentUrl) {
            mIntentUrl = intentUrl;
            notifyCalled();
        }
        public String getIntentUrl() {
            assert getCallCount() > 0;
            return mIntentUrl;
        }
    }

    public OnPageStartedHelper getOnPageStartedHelper() {
        return mTestWebContentsObserver.getOnPageStartedHelper();
    }

    public OnPageFinishedHelper getOnPageFinishedHelper() {
        return mTestWebContentsObserver.getOnPageFinishedHelper();
    }

    public OnReceivedErrorHelper getOnReceivedErrorHelper() {
        return mTestWebContentsObserver.getOnReceivedErrorHelper();
    }

    public OnEvaluateJavaScriptResultHelper getOnEvaluateJavaScriptResultHelper() {
        return mTestContentViewClient.getOnEvaluateJavaScriptResultHelper();
    }

    public OnStartContentIntentHelper getOnStartContentIntentHelper() {
        return mTestContentViewClient.getOnStartContentIntentHelper();
    }
}
