// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser;

import android.content.Context;
import android.os.Bundle;
import android.util.Log;
import android.util.Pair;
import android.view.GestureDetector;
import android.view.GestureDetector.OnGestureListener;
import android.view.MotionEvent;
import android.view.ViewConfiguration;

import org.chromium.content.browser.LongPressDetector.LongPressDelegate;
import org.chromium.content.common.TraceEvent;

import java.util.ArrayDeque;
import java.util.Deque;

/**
 * This class handles all MotionEvent handling done in ContentViewCore including the gesture
 * recognition. It sends all related native calls through the interface MotionEventDelegate.
 */
class ContentViewGestureHandler implements LongPressDelegate {

    private static final String TAG = ContentViewGestureHandler.class.toString();
    /**
     * Used for GESTURE_FLING_START x velocity
     */
    static final String VELOCITY_X = "Velocity X";
    /**
     * Used for GESTURE_FLING_START y velocity
     */
    static final String VELOCITY_Y = "Velocity Y";
    /**
     * Used in GESTURE_SINGLE_TAP_CONFIRMED to check whether ShowPress has been called before.
     */
    static final String SHOW_PRESS = "ShowPress";
    /**
     * Used for GESTURE_PINCH_BY delta
     */
    static final String DELTA = "Delta";

    private final Bundle mExtraParamBundle;
    private GestureDetector mGestureDetector;
    private final ZoomManager mZoomManager;
    private LongPressDetector mLongPressDetector;
    private OnGestureListener mListener;
    private MotionEvent mCurrentDownEvent;
    private final MotionEventDelegate mMotionEventDelegate;

    // Queue of motion events. If the boolean value is true, it means
    // that the event has been offered to the native side but not yet acknowledged. If the
    // value is false, it means the touch event has not been offered
    // to the native side and can be immediately processed.
    private final Deque<Pair<MotionEvent, Boolean>> mPendingMotionEvents =
            new ArrayDeque<Pair<MotionEvent, Boolean>>();

    // Has WebKit told us the current page requires touch events.
    private boolean mNeedTouchEvents = false;

    // Remember whether onShowPress() is called. If it is not, in onSingleTapConfirmed()
    // we will first show the press state, then trigger the click.
    private boolean mShowPressIsCalled;

    // TODO(klobag): this is to avoid a bug in GestureDetector. With multi-touch,
    // mAlwaysInTapRegion is not reset. So when the last finger is up, onSingleTapUp()
    // will be mistakenly fired.
    private boolean mIgnoreSingleTap;

    // Does native think we are scrolling?  True from right before we
    // send the first scroll event until the last finger is raised, or
    // until after the follow-up fling has finished.  Call
    // nativeScrollBegin() when setting this to true, and use
    // tellNativeScrollingHasEnded() to set it to false.
    private boolean mNativeScrolling;

    private boolean mPinchInProgress = false;

    // Tracks whether a touch cancel event has been sent as a result of switching
    // into scrolling or pinching mode.
    private boolean mTouchCancelEventSent = false;

    private static final int DOUBLE_TAP_TIMEOUT = ViewConfiguration.getDoubleTapTimeout();

    //On single tap this will store the x, y coordinates of the touch.
    private int mSingleTapX;
    private int mSingleTapY;

    // Used to track the last rawX/Y coordinates for moves.  This gives absolute scroll distance.
    // Useful for full screen tracking.
    private float mLastRawX = 0;
    private float mLastRawY = 0;

    // Cache of square of the scaled touch slop so we don't have to calculate it on every touch.
    private int mScaledTouchSlopSquare;

    // Used to track the accumulated scroll error over time. This is used to remove the
    // rounding error we introduced by passing integers to webkit.
    private float mAccumulatedScrollErrorX = 0;
    private float mAccumulatedScrollErrorY = 0;

    private static final int SNAP_NONE = 0;
    private static final int SNAP_HORIZ = 1;
    private static final int SNAP_VERT = 2;
    private int mSnapScrollMode = SNAP_NONE;
    private float mAverageAngle;
    private boolean mSeenFirstScroll;

    /*
     * Here is the snap align logic:
     * 1. If it starts nearly horizontally or vertically, snap align;
     * 2. If there is a dramatic direction change, let it go;
     *
     * Adjustable parameters. Angle is the radians on a unit circle, limited
     * to quadrant 1. Values range from 0f (horizontal) to PI/2 (vertical)
     */
    private static final float HSLOPE_TO_START_SNAP = .25f;
    private static final float HSLOPE_TO_BREAK_SNAP = .6f;
    private static final float VSLOPE_TO_START_SNAP = 1.25f;
    private static final float VSLOPE_TO_BREAK_SNAP = .6f;

    /*
     * These values are used to influence the average angle when entering
     * snap mode. If it is the first movement entering snap, we set the average
     * to the appropriate ideal. If the user is entering into snap after the
     * first movement, then we average the average angle with these values.
     */
    private static final float ANGLE_VERT = (float)(Math.PI / 2.0);
    private static final float ANGLE_HORIZ = 0f;

    /*
     *  The modified moving average weight.
     *  Formula: MAV[t]=MAV[t-1] + (P[t]-MAV[t-1])/n
     */
    private static final float MMA_WEIGHT_N = 5;

    static final int GESTURE_SHOW_PRESSED_STATE = 0;
    static final int GESTURE_DOUBLE_TAP = 1;
    static final int GESTURE_SINGLE_TAP_UP = 2;
    static final int GESTURE_SINGLE_TAP_CONFIRMED = 3;
    static final int GESTURE_LONG_PRESS = 4;
    static final int GESTURE_SCROLL_START = 5;
    static final int GESTURE_SCROLL_BY = 6;
    static final int GESTURE_SCROLL_END = 7;
    static final int GESTURE_FLING_START = 8;
    static final int GESTURE_FLING_CANCEL = 9;
    static final int GESTURE_PINCH_BEGIN = 10;
    static final int GESTURE_PINCH_BY = 11;
    static final int GESTURE_PINCH_END = 12;

    /**
     * This is an interface to handle MotionEvent related communication with the native side also
     * access some ContentView specific parameters.
     */
    public interface MotionEventDelegate {
        /**
         * Send a raw {@link MotionEvent} to the native side
         * @param timeMs Time of the event in ms.
         * @param action The action type for the event.
         * @param pts The TouchPoint array to be sent for the event.
         * @return Whether the event was sent to the native side successfully or not.
         */
        public boolean sendTouchEvent(long timeMs, int action, TouchPoint[] pts);

        /**
         * Send a gesture event to the native side.
         * @param type The type of the gesture event.
         * @param timeMs The time the gesture event occurred at.
         * @param x The x location for the gesture event.
         * @param y The y location for the gesture event.
         * @param extraParams A bundle that holds specific extra parameters for certain gestures.
         * Refer to gesture type definition for more information.
         * @return Whether the gesture was sent successfully.
         */
        boolean sendGesture(
                int type, long timeMs, int x, int y, Bundle extraParams);

        /**
         * Gives the UI the chance to override each scroll event.
         * @param x The amount scrolled in the X direction.
         * @param y The amount scrolled in the Y direction.
         * @return Whether or not the UI consumed and handled this event.
         */
        boolean didUIStealScroll(float x, float y);

        /**
         * Show the zoom picker UI.
         */
        public void invokeZoomPicker();
    }

    ContentViewGestureHandler(
            Context context, MotionEventDelegate delegate, ZoomManager zoomManager) {
        mExtraParamBundle = new Bundle();
        mLongPressDetector = new LongPressDetector(context, this);
        mMotionEventDelegate = delegate;
        mZoomManager = zoomManager;
        initGestureDetectors(context);
    }

    /**
     * Used to override the default long press detector, gesture detector and listener.
     * This is used for testing only.
     * @param longPressDetector The new LongPressDetector to be assigned.
     * @param gestureDetector The new GestureDetector to be assigned.
     * @param listener The new onGestureListener to be assigned.
     */
    void setTestDependencies(
            LongPressDetector longPressDetector, GestureDetector gestureDetector,
            OnGestureListener listener) {
        mLongPressDetector = longPressDetector;
        mGestureDetector = gestureDetector;
        mListener = listener;
    }

    private void initGestureDetectors(final Context context) {
        int scaledTouchSlop = ViewConfiguration.get(context).getScaledTouchSlop();
        mScaledTouchSlopSquare = scaledTouchSlop * scaledTouchSlop;
        try {
            TraceEvent.begin();
            GestureDetector.SimpleOnGestureListener listener =
                new GestureDetector.SimpleOnGestureListener() {
                    @Override
                    public boolean onDown(MotionEvent e) {
                        mShowPressIsCalled = false;
                        mIgnoreSingleTap = false;
                        mSeenFirstScroll = false;
                        mNativeScrolling = false;
                        mSnapScrollMode = SNAP_NONE;
                        mLastRawX = e.getRawX();
                        mLastRawY = e.getRawY();
                        mAccumulatedScrollErrorX = 0;
                        mAccumulatedScrollErrorY = 0;
                        // Return true to indicate that we want to handle touch
                        return true;
                    }

                    @Override
                    public boolean onScroll(MotionEvent e1, MotionEvent e2,
                            float distanceX, float distanceY) {
                        // Scroll snapping
                        if (!mSeenFirstScroll) {
                            mAverageAngle = calculateDragAngle(distanceX, distanceY);
                            // Initial scroll event
                            if (!mZoomManager.isScaleGestureDetectionInProgress()) {
                                // if it starts nearly horizontal or vertical, enforce it
                                if (mAverageAngle < HSLOPE_TO_START_SNAP) {
                                    mSnapScrollMode = SNAP_HORIZ;
                                    mAverageAngle = ANGLE_HORIZ;
                                } else if (mAverageAngle > VSLOPE_TO_START_SNAP) {
                                    mSnapScrollMode = SNAP_VERT;
                                    mAverageAngle = ANGLE_VERT;
                                }
                            }
                            mSeenFirstScroll = true;
                            // Ignore the first scroll delta to avoid a visible jump.
                            return true;
                        } else {
                            mAverageAngle +=
                                (calculateDragAngle(distanceX, distanceY) - mAverageAngle)
                                / MMA_WEIGHT_N;
                            if (mSnapScrollMode != SNAP_NONE) {
                                if ((mSnapScrollMode == SNAP_VERT
                                        && mAverageAngle < VSLOPE_TO_BREAK_SNAP)
                                        || (mSnapScrollMode == SNAP_HORIZ
                                                && mAverageAngle > HSLOPE_TO_BREAK_SNAP)) {
                                    // radical change means getting out of snap mode
                                    mSnapScrollMode = SNAP_NONE;
                                }
                            } else {
                                if (!mZoomManager.isScaleGestureDetectionInProgress()) {
                                    if (mAverageAngle < HSLOPE_TO_START_SNAP) {
                                        mSnapScrollMode = SNAP_HORIZ;
                                        mAverageAngle = (mAverageAngle + ANGLE_HORIZ) / 2;
                                    } else if (mAverageAngle > VSLOPE_TO_START_SNAP) {
                                        mSnapScrollMode = SNAP_VERT;
                                        mAverageAngle = (mAverageAngle + ANGLE_VERT) / 2;
                                    }
                                }
                            }
                        }

                        if (mSnapScrollMode != SNAP_NONE) {
                            if (mSnapScrollMode == SNAP_HORIZ) {
                                distanceY = 0;
                            } else {
                                distanceX = 0;
                            }
                        }

                        boolean didUIStealScroll = mMotionEventDelegate.didUIStealScroll(
                                e2.getRawX() - mLastRawX, e2.getRawY() - mLastRawY);

                        mLastRawX = e2.getRawX();
                        mLastRawY = e2.getRawY();
                        if (didUIStealScroll) return true;
                        if (!mNativeScrolling && mMotionEventDelegate.sendGesture(
                                GESTURE_SCROLL_START, e1.getEventTime(),
                                        (int) e1.getX(), (int) e1.getY(), null)) {
                            mNativeScrolling = true;

                        }
                        // distanceX and distanceY is the scrolling offset since last onScroll.
                        // Because we are passing integers to webkit, this could introduce
                        // rounding errors. The rounding errors will accumulate overtime.
                        // To solve this, we should adding back the rounding errors each time
                        // when we calculate the new offset.
                        int dx = (int) (distanceX + mAccumulatedScrollErrorX);
                        int dy = (int) (distanceY + mAccumulatedScrollErrorY);
                        mAccumulatedScrollErrorX = distanceX + mAccumulatedScrollErrorX - dx;
                        mAccumulatedScrollErrorY = distanceY + mAccumulatedScrollErrorY - dy;
                        if ((dx | dy) != 0) {
                            mMotionEventDelegate.sendGesture(GESTURE_SCROLL_BY,
                                    e2.getEventTime(), dx, dy, null);
                        }

                        mMotionEventDelegate.invokeZoomPicker();

                        return true;
                    }

                    @Override
                    public boolean onFling(MotionEvent e1, MotionEvent e2,
                            float velocityX, float velocityY) {
                        if (mSnapScrollMode == SNAP_NONE) {
                            float flingAngle = calculateDragAngle(velocityX, velocityY);
                            if (flingAngle < HSLOPE_TO_START_SNAP) {
                                mSnapScrollMode = SNAP_HORIZ;
                                mAverageAngle = ANGLE_HORIZ;
                            } else if (flingAngle > VSLOPE_TO_START_SNAP) {
                                mSnapScrollMode = SNAP_VERT;
                                mAverageAngle = ANGLE_VERT;
                            }
                        }

                        if (mSnapScrollMode != SNAP_NONE) {
                            if (mSnapScrollMode == SNAP_HORIZ) {
                                velocityY = 0;
                            } else {
                                velocityX = 0;
                            }
                        }

                        fling(e1.getEventTime(),(int) e1.getX(0), (int) e1.getY(0),
                                        (int) velocityX, (int) velocityY);
                        return true;
                    }

                    @Override
                    public void onShowPress(MotionEvent e) {
                        mShowPressIsCalled = true;
                        mMotionEventDelegate.sendGesture(GESTURE_SHOW_PRESSED_STATE,
                                e.getEventTime(), (int) e.getX(), (int) e.getY(), null);
                    }

                    @Override
                    public boolean onSingleTapUp(MotionEvent e) {
                        if (isDistanceBetweenDownAndUpTooLong(e.getRawX(), e.getRawY())) {
                            mIgnoreSingleTap = true;
                            return true;
                        }
                        // This is a hack to address the issue where user hovers
                        // over a link for longer than DOUBLE_TAP_TIMEOUT, then
                        // onSingleTapConfirmed() is not triggered. But we still
                        // want to trigger the tap event at UP. So we override
                        // onSingleTapUp() in this case. This assumes singleTapUp
                        // gets always called before singleTapConfirmed.
                        if (!mIgnoreSingleTap && !mLongPressDetector.isInLongPress() &&
                                (e.getEventTime() - e.getDownTime() > DOUBLE_TAP_TIMEOUT)) {
                            float x = e.getX();
                            float y = e.getY();
                            if (mMotionEventDelegate.sendGesture(GESTURE_SINGLE_TAP_UP,
                                    e.getEventTime(), (int) x, (int) y, null)) {
                                mIgnoreSingleTap = true;
                            }
                            setClickXAndY((int) x, (int) y);
                            return true;
                        }
                        return false;
                    }

                    @Override
                    public boolean onSingleTapConfirmed(MotionEvent e) {
                        // Long taps in the edges of the screen have their events delayed by
                        // ChromeViewHolder for tab swipe operations. As a consequence of the delay
                        // this method might be called after receiving the up event.
                        // These corner cases should be ignored.
                        if (mLongPressDetector.isInLongPress() || mIgnoreSingleTap) return true;

                        int x = (int) e.getX();
                        int y = (int) e.getY();
                        mExtraParamBundle.clear();
                        mExtraParamBundle.putBoolean(SHOW_PRESS, mShowPressIsCalled);
                        mMotionEventDelegate.sendGesture(GESTURE_SINGLE_TAP_CONFIRMED,
                                e.getEventTime(), x, y, mExtraParamBundle);
                        setClickXAndY(x, y);
                        return true;
                    }

                    @Override
                    public boolean onDoubleTap(MotionEvent e) {
                        mMotionEventDelegate.sendGesture(GESTURE_DOUBLE_TAP,
                                e.getEventTime(), (int) e.getX(), (int) e.getY(), null);
                        return true;
                    }

                    @Override
                    public void onLongPress(MotionEvent e) {
                        if (!mZoomManager.isScaleGestureDetectionInProgress()) {
                            mMotionEventDelegate.sendGesture(GESTURE_LONG_PRESS,
                                    e.getEventTime(), (int) e.getX(), (int) e.getY(), null);
                        }
                    }

                    /**
                     * This method inspects the distance between where the user started touching
                     * the surface, and where she released. If the points are too far apart, we
                     * should assume that the web page has consumed the scroll-events in-between,
                     * and as such, this should not be considered a single-tap.
                     *
                     * We use the Android frameworks notion of how far a touch can wander before
                     * we think the user is scrolling.
                     *
                     * @param x the new x coordinate
                     * @param y the new y coordinate
                     * @return true if the distance is too long to be considered a single tap
                     */
                    private boolean isDistanceBetweenDownAndUpTooLong(float x, float y) {
                        double deltaX = mLastRawX - x;
                        double deltaY = mLastRawY - y;
                        return deltaX * deltaX + deltaY * deltaY > mScaledTouchSlopSquare;
                    }
                };
                mListener = listener;
                mGestureDetector = new GestureDetector(context, listener);
                mGestureDetector.setIsLongpressEnabled(false);
        } finally {
            TraceEvent.end();
        }
    }

    /**
     * @return LongPressDetector handling setting up timers for and canceling LongPress gestures.
     */
    LongPressDetector getLongPressDetector() {
        return mLongPressDetector;
    }

    /**
     * @param event Start a LongPress gesture event from the listener.
     */
    @Override
    public void onLongPress(MotionEvent event) {
        mListener.onLongPress(event);
    }

    /**
     * Cancels any ongoing LongPress timers.
     */
    void cancelLongPress() {
        mLongPressDetector.cancelLongPress();
    }

    /**
     * Fling the ContentView from the current position.
     * @param x Fling touch starting position
     * @param y Fling touch starting position
     * @param velocityX Initial velocity of the fling (X) measured in pixels per second.
     * @param velocityY Initial velocity of the fling (Y) measured in pixels per second.
     */
    void fling(long timeMs, int x, int y, int velocityX, int velocityY) {
        endFling(timeMs);
        mExtraParamBundle.clear();
        mExtraParamBundle.putInt(VELOCITY_X, velocityX);
        mExtraParamBundle.putInt(VELOCITY_Y, velocityY);
        mMotionEventDelegate.sendGesture(GESTURE_FLING_START,
                timeMs, x, y, mExtraParamBundle);
    }

    /**
     * Send a FlingCancel gesture event and also cancel scrolling if it is active.
     * @param timeMs The time in ms for the event initiating this gesture.
     */
    void endFling(long timeMs) {
        mMotionEventDelegate.sendGesture(GESTURE_FLING_CANCEL, timeMs, 0, 0, null);
        tellNativeScrollingHasEnded(timeMs);
    }

    // If native thinks scrolling (or fling-scrolling) is going on, tell native
    // it has ended.
    private void tellNativeScrollingHasEnded(long timeMs) {
        if (mNativeScrolling) {
            mNativeScrolling = false;
            mMotionEventDelegate.sendGesture(GESTURE_SCROLL_END, timeMs, 0, 0, null);
        }
    }

    /**
     * Starts a pinch gesture.
     * @param timeMs The time in ms for the event initiating this gesture.
     * @param x The x coordinate for the event initiating this gesture.
     * @param y The x coordinate for the event initiating this gesture.
     */
    void pinchBegin(long timeMs, int x, int y) {
        mMotionEventDelegate.sendGesture(GESTURE_PINCH_BEGIN, timeMs, x, y, null);
    }

    /**
     * Pinch by a given percentage.
     * @param timeMs The time in ms for the event initiating this gesture.
     * @param anchorX The x coordinate for the anchor point to be used in pinch.
     * @param anchorY The y coordinate for the anchor point to be used in pinch.
     * @param delta The percentage to pinch by.
     */
    void pinchBy(long timeMs, int anchorX, int anchorY, float delta) {
        mExtraParamBundle.clear();
        mExtraParamBundle.putFloat(DELTA, delta);
        mMotionEventDelegate.sendGesture(GESTURE_PINCH_BY,
                timeMs, anchorX, anchorY, mExtraParamBundle);
        mPinchInProgress = true;
    }

    /**
     * End a pinch gesture.
     * @param timeMs The time in ms for the event initiating this gesture.
     */
    void pinchEnd(long timeMs) {
        mMotionEventDelegate.sendGesture(GESTURE_PINCH_END, timeMs, 0, 0, null);
        mPinchInProgress = false;
    }

    /**
     * Ignore singleTap gestures.
     */
    void setIgnoreSingleTap(boolean value) {
        mIgnoreSingleTap = value;
    }

    private float calculateDragAngle(float dx, float dy) {
        dx = Math.abs(dx);
        dy = Math.abs(dy);
        return (float) Math.atan2(dy, dx);
    }

    private void setClickXAndY(int x, int y) {
        mSingleTapX = x;
        mSingleTapY = y;
    }

    /**
     * @return The x coordinate for the last point that a singleTap gesture was initiated from.
     */
    public int getSingleTapX()  {
        return mSingleTapX;
    }

    /**
     * @return The y coordinate for the last point that a singleTap gesture was initiated from.
     */
    public int getSingleTapY()  {
        return mSingleTapY;
    }

    /**
     * Handle the incoming MotionEvent.
     * @return Whether the event was handled.
     */
    boolean onTouchEvent(MotionEvent event) {
        TraceEvent.begin("onTouchEvent");
        mLongPressDetector.cancelLongPressIfNeeded(event);
        // Notify native that scrolling has stopped whenever a down action is processed prior to
        // passing the event to native as it will drop them as an optimization if scrolling is
        // enabled.  Ending the fling ensures scrolling has stopped as well as terminating the
        // current fling if applicable.
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
            endFling(event.getEventTime());
        }

        if (offerTouchEventToJavaScript(event)) {
            // offerTouchEventToJavaScript returns true to indicate the event was sent
            // to the render process. If it is not subsequently handled, it will
            // be returned via confirmTouchEvent(false) and eventually passed to
            // processTouchEvent asynchronously.
            TraceEvent.end("onTouchEvent");
            return true;
        }
        return processTouchEvent(event);
    }

    /**
     * Sets the flag indicating that the content has registered listeners for touch events.
     */
    void didSetNeedTouchEvents(boolean needTouchEvents) {
        mNeedTouchEvents = needTouchEvents;
        // When mainframe is loading, FrameLoader::transitionToCommitted will
        // call this method to set mNeedTouchEvents to false. We use this as
        // an indicator to clear the pending motion events so that events from
        // the previous page will not be carried over to the new page.
        if (!mNeedTouchEvents) mPendingMotionEvents.clear();
    }

    private boolean offerTouchEventToJavaScript(MotionEvent event) {
        mLongPressDetector.onOfferTouchEventToJavaScript(event);

        if (!mNeedTouchEvents) return false;

        if (event.getActionMasked() == MotionEvent.ACTION_MOVE) {
            // Only send move events if the move has exceeded the slop threshold.
            if (!mLongPressDetector.confirmOfferMoveEventToJavaScript(event)) {
                return true;
            }
            // Avoid flooding the renderer process with move events: if the previous pending
            // command is also a move (common case), skip sending this event to the webkit
            // side and collapse it into the pending event.
            Pair<MotionEvent, Boolean> previousEvent = mPendingMotionEvents.peekLast();
            if (previousEvent != null && previousEvent.second == true
                    && previousEvent.first.getActionMasked() == MotionEvent.ACTION_MOVE
                    && previousEvent.first.getPointerCount() == event.getPointerCount()) {
                MotionEvent.PointerCoords[] coords =
                    new MotionEvent.PointerCoords[event.getPointerCount()];
                for (int i = 0; i < coords.length; ++i) {
                    coords[i] = new MotionEvent.PointerCoords();
                    event.getPointerCoords(i, coords[i]);
                }
                previousEvent.first.addBatch(event.getEventTime(), coords, event.getMetaState());
                return true;
            }
        }

        TouchPoint[] pts = new TouchPoint[event.getPointerCount()];
        int type = TouchPoint.createTouchPoints(event, pts);

        boolean forwarded = false;
        if (type != TouchPoint.CONVERSION_ERROR && !mNativeScrolling && !mPinchInProgress) {
            mTouchCancelEventSent = false;
            forwarded = mMotionEventDelegate.sendTouchEvent(event.getEventTime(), type, pts);
        } else if ((mNativeScrolling || mPinchInProgress) && !mTouchCancelEventSent) {
            forwarded = mMotionEventDelegate.sendTouchEvent(event.getEventTime(),
                    TouchPoint.TOUCH_EVENT_TYPE_CANCEL, pts);
            mTouchCancelEventSent = true;
        }
        if (forwarded || !mPendingMotionEvents.isEmpty()) {
            // Copy the event, as the original may get mutated after this method returns.
            mPendingMotionEvents.add(Pair.create(MotionEvent.obtain(event), forwarded));
            // TODO(joth): If needed, start a watchdog timer to pump mPendingMotionEvents
            // in the case of the WebKit renderer / JS being unresponsive.
            return true;
        }
        return false;
    }

    private boolean processTouchEvent(MotionEvent event) {
        boolean handled = false;
        // The last "finger up" is an end to scrolling but may not be
        // an end to movement (e.g. fling scroll).  We do not tell
        // native code to end scrolling until we are sure we did not
        // fling.
        boolean possiblyEndMovement = false;

        // "Last finger raised" could be an end to movement.  However,
        // give the mSimpleTouchDetector a chance to continue
        // scrolling with a fling.
        if ((event.getAction() == MotionEvent.ACTION_UP) &&
            (event.getPointerCount() == 1)) {
            if (mNativeScrolling) {
                possiblyEndMovement = true;
            }
        }

        mLongPressDetector.startLongPressTimerIfNeeded(event);

        // Use the framework's GestureDetector to detect pans and zooms not already
        // handled by the WebKit touch events gesture manager.
        if (canHandle(event)) {
            handled |= mGestureDetector.onTouchEvent(event);
            if (event.getAction() == MotionEvent.ACTION_DOWN) mCurrentDownEvent = event;
        }

        handled |= mZoomManager.processTouchEvent(event);

        if (possiblyEndMovement && !handled) {
            tellNativeScrollingHasEnded(event.getEventTime());
        }

        return handled;
    }

    /**
     * Respond to a MotionEvent being returned from the native side.
     * @param handled Whether the MotionEvent was handled on the native side.
     */
    void confirmTouchEvent(boolean handled) {
        MotionEvent eventToPassThrough = null;
        if (mPendingMotionEvents.isEmpty()) {
            Log.w(TAG, "confirmTouchEvent with Empty pending list!");
            return;
        }
        TraceEvent.begin();
        Pair<MotionEvent, Boolean> event = mPendingMotionEvents.removeFirst();
        if (!handled) {
            if (!processTouchEvent(event.first)) {
                // TODO(joth): If the Java side gesture handler also fails to consume
                // this deferred event, should it be bubbled up to the parent view?
                Log.w(TAG, "Unhandled deferred touch event");
            }
        } else {
            mZoomManager.passTouchEventThrough(event.first);
        }

        // Now process all events that are in the queue but not sent to the native.
        Pair<MotionEvent, Boolean> nextEvent = mPendingMotionEvents.peekFirst();
        while (nextEvent != null && nextEvent.second == false) {
            processTouchEvent(nextEvent.first);
            mPendingMotionEvents.removeFirst();
            nextEvent.first.recycle();
            nextEvent = mPendingMotionEvents.peekFirst();
        }

        // We may have pending events that could cancel the timers:
        // For instance, if we received an UP before the DOWN completed
        // its roundtrip (so it didn't cancel the timer during onTouchEvent()).
        mLongPressDetector.cancelLongPressIfNeeded(mPendingMotionEvents.iterator());
        event.first.recycle();
        TraceEvent.end();
    }

    /**
     * @return Whether the ContentViewGestureHandler can handle a MotionEvent right now. True only
     * if it's the start of a new stream (ACTION_DOWN), or a continuation of the current stream.
     */
    boolean canHandle(MotionEvent ev) {
        return ev.getAction() == MotionEvent.ACTION_DOWN ||
                (mCurrentDownEvent != null && mCurrentDownEvent.getDownTime() == ev.getDownTime());
    }

}
