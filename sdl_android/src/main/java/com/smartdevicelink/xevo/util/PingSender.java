// Copyright 2017 Xevo Inc. All rights reserved.

package com.smartdevicelink.xevo.util;

public class PingSender {

    public interface PingSenderListener {
        void onReply();
        void onTimeout();
    }

    public PingSender(String destAddr, int intervalMsec, int dataSize, PingSenderListener listener) {
        if (destAddr == null || intervalMsec <= 0 || dataSize < 0) {
            throw new IllegalArgumentException("Invalid argument");
        }

        mListener = listener;

        nativeNew();

        boolean ret = nativeConfigure(destAddr, intervalMsec, listener, dataSize);
        if (!ret) {
            nativeRelease();
            throw new IllegalArgumentException("Invalid argument");
        }
    }

    @Override
    public void finalize() throws Throwable {
        super.finalize();
        nativeRelease();
    }

    public boolean start() {
        return nativeStart();
    }

    public boolean stop(int timeoutMsec) {
        return nativeStop(timeoutMsec);
    }

    public boolean checkReply() {
        return nativeCheckReply();
    }

    private void onReply() {
        if (mListener != null) {
            mListener.onReply();
        }
    }

    private void onTimeout() {
        if (mListener != null) {
            mListener.onTimeout();
        }
    }

    private long mNativeInstance;
    private PingSenderListener mListener;

    private native void nativeNew();
    private native boolean nativeConfigure(String destAddr, int intervalMsec, PingSenderListener listener, int dataSize);
    private native boolean nativeRelease();
    private native boolean nativeStart();
    private native boolean nativeStop(int timeoutMsec);
    private native boolean nativeCheckReply();

    static {
        System.loadLibrary("tcpip");
    }
}
