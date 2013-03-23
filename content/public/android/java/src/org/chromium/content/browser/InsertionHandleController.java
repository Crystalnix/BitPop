// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser;

import android.content.ClipboardManager;
import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.drawable.Drawable;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.ViewGroup;
import android.view.ViewGroup.LayoutParams;
import android.widget.PopupWindow;

/**
 * CursorController for inserting text at the cursor position.
 */
abstract class InsertionHandleController implements CursorController {

    /** The handle view, lazily created when first shown */
    private HandleView mHandle;

    /** The view over which the insertion handle should be shown */
    private View mParent;

    /** True iff the insertion handle is currently showing */
    private boolean mIsShowing;

    /** True iff the insertion handle can be shown automatically when selection changes */
    private boolean mAllowAutomaticShowing;

    private Context mContext;

    InsertionHandleController(View parent) {
        mParent = parent;
        mContext = parent.getContext();
    }

    /** Allows the handle to be shown automatically when cursor position changes */
    void allowAutomaticShowing() {
        mAllowAutomaticShowing = true;
    }

    /** Disallows the handle from being shown automatically when cursor position changes */
    void hideAndDisallowAutomaticShowing() {
        hide();
        mAllowAutomaticShowing = false;
    }

    /**
     * Sets the position and shows the handle.
     * @param x1
     * @param y1
     */
    void showHandleAt(int x, int y) {
        createHandleIfNeeded();
        setHandlePosition(x, y);
        showHandleIfNeeded();
    }

    void showPastePopup() {
        if (mIsShowing) {
            mHandle.showPastePopupWindow();
        }
    }

    void showHandleWithPastePopupAt(int x, int y) {
        showHandleAt(x, y);
        showPastePopup();
    }

    /** Shows the handle at the given coordinates, as long as automatic showing is allowed */
    void onCursorPositionChanged(int x, int y) {
        if (mAllowAutomaticShowing) {
            showHandleAt(x, y);
        }
    }

    /**
     * Moves the handle so that it points at the given coordinates.
     * @param x
     * @param y
     */
    void setHandlePosition(int x, int y) {
        mHandle.positionAt(x, y);
    }

    public HandleView getHandleViewForTest() {
        return mHandle;
    }

    @Override
    public void onTouchModeChanged(boolean isInTouchMode) {
        if (!isInTouchMode) {
            hide();
        }
    }

    @Override
    public void hide() {
        if (mIsShowing) {
            if (mHandle != null) mHandle.hide();
            mIsShowing = false;
        }
    }

    @Override
    public boolean isShowing() {
        return mIsShowing;
    }

    @Override
    public void beforeStartUpdatingPosition(HandleView handle) {}

    @Override
    public void updatePosition(HandleView handle, int x, int y) {
        setCursorPosition(x, y);
    }

    /**
     * The concrete implementation must cause the cursor position to move to the given
     * coordinates and (possibly asynchronously) set the insertion handle position
     * after the cursor position change is made via showHandleAt(x,y).
     * @param x
     * @param y
     */
    protected abstract void setCursorPosition(int x, int y);

    /** Pastes the contents of clipboard at the current insertion point */
    protected abstract void paste();

    /** Returns the current line height in pixels */
    protected abstract int getLineHeight();

    @Override
    public void onDetached() {}

    boolean canPaste() {
        return ((ClipboardManager)mContext.getSystemService(
                Context.CLIPBOARD_SERVICE)).hasPrimaryClip();
    }

    private void createHandleIfNeeded() {
        if (mHandle == null) mHandle = new HandleView(this, HandleView.CENTER, mParent);
    }

    private void showHandleIfNeeded() {
        if (!mIsShowing) {
            mIsShowing = true;
            mHandle.show();
        }
    }

    /*
     * This class is based on TextView.PastePopupMenu.
     */
    class PastePopupMenu implements OnClickListener {
        private final PopupWindow mContainer;
        private int mPositionX;
        private int mPositionY;
        private View[] mPasteViews;
        private int[] mPasteViewLayouts;

        public PastePopupMenu() {
            mContainer = new PopupWindow(mContext, null,
                    android.R.attr.textSelectHandleWindowStyle);
            mContainer.setSplitTouchEnabled(true);
            mContainer.setClippingEnabled(false);

            mContainer.setWidth(ViewGroup.LayoutParams.WRAP_CONTENT);
            mContainer.setHeight(ViewGroup.LayoutParams.WRAP_CONTENT);

            final int[] POPUP_LAYOUT_ATTRS = {
                android.R.attr.textEditPasteWindowLayout,
                android.R.attr.textEditNoPasteWindowLayout,
                android.R.attr.textEditSidePasteWindowLayout,
                android.R.attr.textEditSideNoPasteWindowLayout,
            };

            mPasteViews = new View[POPUP_LAYOUT_ATTRS.length];
            mPasteViewLayouts = new int[POPUP_LAYOUT_ATTRS.length];

            TypedArray attrs = mContext.obtainStyledAttributes(POPUP_LAYOUT_ATTRS);
            for (int i = 0; i < attrs.length(); ++i) {
                mPasteViewLayouts[i] = attrs.getResourceId(attrs.getIndex(i), 0);
            }
            attrs.recycle();
        }

        private int viewIndex(boolean onTop) {
            return (onTop ? 0 : 1<<1) + (canPaste() ? 0 : 1 << 0);
        }

        private void updateContent(boolean onTop) {
            final int viewIndex = viewIndex(onTop);
            View view = mPasteViews[viewIndex];

            if (view == null) {
                final int layout = mPasteViewLayouts[viewIndex];
                LayoutInflater inflater = (LayoutInflater)mContext.
                    getSystemService(Context.LAYOUT_INFLATER_SERVICE);
                if (inflater != null) {
                    view = inflater.inflate(layout, null);
                }

                if (view == null) {
                    throw new IllegalArgumentException("Unable to inflate TextEdit paste window");
                }

                final int size = View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED);
                view.setLayoutParams(new LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT));
                view.measure(size, size);

                view.setOnClickListener(this);

                mPasteViews[viewIndex] = view;
            }

            mContainer.setContentView(view);
        }

        void show() {
            updateContent(true);
            positionAtCursor();
        }

        void hide() {
            mContainer.dismiss();
        }

        boolean isShowing() {
            return mContainer.isShowing();
        }

        @Override
        public void onClick(View v) {
            if (canPaste()) {
                paste();
            }
            hide();
        }

        void positionAtCursor() {
            View contentView = mContainer.getContentView();
            int width = contentView.getMeasuredWidth();
            int height = contentView.getMeasuredHeight();

            int lineHeight = getLineHeight();

            mPositionX = (int) (mHandle.getAdjustedPositionX() - width / 2.0f);
            mPositionY = mHandle.getAdjustedPositionY() - height - lineHeight;

            final int[] coords = new int[2];
            mParent.getLocationInWindow(coords);
            coords[0] += mPositionX;
            coords[1] += mPositionY;

            final int screenWidth = mContext.getResources().getDisplayMetrics().widthPixels;
            if (coords[1] < 0) {
                updateContent(false);
                // Update dimensions from new view
                contentView = mContainer.getContentView();
                width = contentView.getMeasuredWidth();
                height = contentView.getMeasuredHeight();

                // Vertical clipping, move under edited line and to the side of insertion cursor
                // TODO bottom clipping in case there is no system bar
                coords[1] += height;
                coords[1] += lineHeight;

                // Move to right hand side of insertion cursor by default. TODO RTL text.
                final Drawable handle = mHandle.getDrawable();
                final int handleHalfWidth = handle.getIntrinsicWidth() / 2;

                if (mHandle.getAdjustedPositionX() + width < screenWidth) {
                    coords[0] += handleHalfWidth + width / 2;
                } else {
                    coords[0] -= handleHalfWidth + width / 2;
                }
            } else {
                // Horizontal clipping
                coords[0] = Math.max(0, coords[0]);
                coords[0] = Math.min(screenWidth - width, coords[0]);
            }

            mContainer.showAtLocation(mParent, Gravity.NO_GRAVITY, coords[0], coords[1]);
        }
    }
}
