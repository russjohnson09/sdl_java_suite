// Copyright 2017 Xevo Inc. All rights reserved.

package com.smartdevicelink.xevo.slip;

//import android.support.annotation.NonNull;

import com.smartdevicelink.util.DebugTool;

import java.util.ArrayDeque;
import java.util.Queue;

/**
 * A simple rate limiter implementation.
 *
 * After we send some data through USB, we simply wait until all of them are sent out, then send the
 * next data.
 * This class is implemented as a global object (singleton), because it needs to be accessed from
 * UsbSlipDriver and RtspServer.
 *
 * @hide
 */
public class UsbRateLimitHelper implements SlipInterface.WriteBufferEmptyListener {
    // singleton implementation
    private static UsbRateLimitHelper sInstance = new UsbRateLimitHelper();

    private SlipInterface mSlipInterface;
    private Queue<Integer> mQueue = new ArrayDeque<>();
    private boolean mCanceled;

    private UsbRateLimitHelper() {
    }

    public static UsbRateLimitHelper getInstance() {
        return sInstance;
    }

    /**
     * Set the SlipInterface instance which is associated with USB transport. To remove the instance,
     * put null to the parameter.
     *
     * @param slip  An instance of SlipInterface associated with USB transport
     */
    public synchronized void setUsbSlipInterface(SlipInterface slip) {
        if (mSlipInterface != null) {
            mSlipInterface.setWriteBufferEmptyEventListener(null);
            mSlipInterface = null;
        }

        if (slip != null) {
            mSlipInterface = slip;
            mSlipInterface.setWriteBufferEmptyEventListener(this);
        }
    }

    /**
     * Block until USB transport's write buffer gets empty.
     *
     * To cancel this operation, either call cancel() from another thread, or interrupt to the
     * blocking thread. If cancel() is called, this method returns without any errors. If the thread
     * is interrupted, an InterruptedException is thrown.
     */
    public synchronized void waitForBufferEmpty() throws InterruptedException {
        if (mSlipInterface == null) {
            return;
        }

        int id = mSlipInterface.requestWriteBufferEmptyEvent();
        if (id < 0) {
            // don't block
            return;
        }

        while (true) {
            while (mQueue.isEmpty() && !mCanceled) {
                this.wait();
            }

            if (mCanceled) {
                DebugTool.logInfo("waitForBufferEmpty() canceled");
                mCanceled = false;
                break;
            }

            Integer receivedId = mQueue.poll();
            if (receivedId == null) {
                // should not happen
                continue;
            }
            if (receivedId == id) {
                // got expected ID so stop waiting
                break;
            }
        }
    }

    /**
     * Cancel blocking operation of waitForBufferEmpty().
     *
     * Note: if cancel() is called then waitForBufferEmpty() is called after that, waitForBufferEmpty()
     * will return without waiting.
     */
    public synchronized void cancel() {
        DebugTool.logInfo("UsbRateLimitHelper.cancel() called");
        mCanceled = true;
        this.notifyAll();
    }

    public synchronized void onBufferEmpty(int id) {
        mQueue.add(id);
        this.notifyAll();
    }
}
