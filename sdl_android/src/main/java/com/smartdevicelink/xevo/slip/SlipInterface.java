// Copyright 2017 Xevo Inc. All rights reserved.
package com.smartdevicelink.xevo.slip;

import android.os.Handler;
import android.os.Looper;

import com.smartdevicelink.util.DebugTool;

import java.io.FileDescriptor;
import java.net.InetAddress;
import java.nio.ByteBuffer;
import java.security.InvalidParameterException;

/**
 * SLIP interface that locates on physical transport (i.g. USB or BT) and lwIP.
 *
 * @hide
 */
@SuppressWarnings("unused")
public final class SlipInterface {

    /**
     * Default buffer size to receive outgoing SLIP packets.
     */
    public static final int RCVBUF_SIZE = 8192;

    /**
     * An error code representing I/O error on USB.
     *
     * Constant Value: -1
     */
    public static int ERROR_USB_WRITE = -1;

    /**
     * An error code representing write blocking timeout on USB.
     *
     * Constant Value: -2
     */
    public static int ERROR_USB_STUCK = -2;

    /**
     * Approx. timeout of detecting USB write stuck, in seconds
     *
     * Constant Value: 10
     */
    private static final int USB_STUCK_TIMEOUT_SEC = 10;

    /**
     * Invoked when outgoing SLIP packet gets ready to send
     */
    public interface OutputPacketListener {
        void onSlipPacketReady(ByteBuffer buffer, int offset, int length);
    }

    /**
     * A callback to handle asynchronous error on SLIP driver.
     */
    public interface OnErrorListener {
        void onSlipError(int error);
    }

    /**
     * Invoked on a native thread when write buffer gets empty after requestWriteBufferEmptyEvent() (USB only)
     */
    public interface WriteBufferEmptyListener {
        void onBufferEmpty(int id);
    }

    // Called when encounter an error on JNI side.
    private interface NativeErrorListener {
        void onNativeError(int errorCode);
    }

    // Called by the native layer
    private interface NativeWriteBufferListener {
        void onBufferEmpty(int id);
    }

    private InetAddress mAddress;
    private InetAddress mNetmask;
    private FileDescriptor mFD;

    private ByteBuffer mRecvBuffer;
    private OutputPacketListener mListener;

    private static OnErrorListener sDefaultErrorListener = new OnErrorListener() {
        @Override
        public void onSlipError(int error) {
            // do nothing
        }
    };
    private OnErrorListener mErrorListener = sDefaultErrorListener;

    private static WriteBufferEmptyListener sDefaultWriteBufferEmptyListener = new WriteBufferEmptyListener() {
        @Override
        public void onBufferEmpty(int id) {
            // do nothing
        }
    };
    private WriteBufferEmptyListener mWriteBufferEmptyListener = sDefaultWriteBufferEmptyListener;

    /**
     * Init SLIP driver with address and netmask assigning to SLIP interface.
     * Currently this constructor is used by {@link UsbSlipDriver}.
     *
     * @param address   IP address assigned to the the interface.
     * @param netmask   Netmask address assigned to the interface.
     * @param fd        File descriptor in which read and write data.
     */
    public SlipInterface(InetAddress address, InetAddress netmask, FileDescriptor fd) {
        this(address, netmask, fd, null);
    }

    public SlipInterface(InetAddress address, InetAddress netmask, OutputPacketListener listener) {
        this(address, netmask, null, listener);
    }

    /**
     * Init SLIP driver with address and netmask assigning to SLIP interface.
     *
     * @param address   IP address assigned to the the interface.
     * @param netmask   Netmask address assigned to the interface.
     * @param fd        File descriptor in which read and write data.
     * @param listener
     */
    public SlipInterface(InetAddress address, InetAddress netmask, FileDescriptor fd, OutputPacketListener listener) {
        if (address == null)
            throw new InvalidParameterException("address is null");

        if (netmask == null)
            throw new InvalidParameterException("netmask is null");

        if (listener == null && fd == null)
            throw new InvalidParameterException("file descriptor is null");

        DebugTool.logInfo(String.format("New SLIP (addr=%s, netmask=%s)",address.toString(), netmask.toString()));
        mAddress = address;
        mNetmask = netmask;
        mFD = fd;
        mListener   = listener;

        if (listener != null) {
            mRecvBuffer = ByteBuffer.allocateDirect(RCVBUF_SIZE);
        }
    }

    /**
     * Set an error listener.
     * Notes error listener is always invoked on main thread.
     *
     * @param listener  A listener function to being invoked when encounter an error.
     */
    public void setErrorListener(OnErrorListener listener) {
        if (listener != null) {
            mErrorListener = listener;
        } else {
            mErrorListener = sDefaultErrorListener;
        }
    }

    /**
     * Set write buffer empty event listener.
     *
     * After requestWriteBufferEmptyEvent() is called, the event is notified on a native thread.
     * See the comment of requestWriteBufferEmptyEvent().
     *
     * @param listener  A listener that receives write buffer empty events.
     */
    public void setWriteBufferEmptyEventListener(WriteBufferEmptyListener listener) {
        if (listener != null) {
            mWriteBufferEmptyListener = listener;
        } else {
            mWriteBufferEmptyListener = sDefaultWriteBufferEmptyListener;
        }
    }

    /**
     * Get address assigned on SLIP layer.
     *
     * @return  An address of the SLIP interface.
     */
    public InetAddress getAddress() { return mAddress; }

    /**
     * Get netmask assigned on SLIP layer
     *
     * @return A netmask of the SLIP interface.
     */
    public InetAddress getNetmask() { return mNetmask; }

    /**
     * Attach SLIP network interface
     *
     * @return  true if it is successful, otherwise false
     */
    public boolean attach() {
        NativeErrorListener errback = new NativeErrorListener() {
            @Override
            public void onNativeError(final int errorCode) {
                // Caution: this callback will be called either from USB write thread or tcpip_thread.
                if (errorCode == ERROR_USB_WRITE || errorCode == ERROR_USB_STUCK) {
                    DebugTool.logInfo("SLIP Interface error: " + errorCode);
                    Handler handler = new Handler(Looper.getMainLooper());
                    handler.post(new Runnable() {
                        @Override
                        public void run() {
                            mErrorListener.onSlipError(errorCode);
                        }
                    });
                }
            }
        };

        if (mFD != null) {
            NativeWriteBufferListener listener = new NativeWriteBufferListener() {
                @Override
                public void onBufferEmpty(int id) {
                    mWriteBufferEmptyListener.onBufferEmpty(id);
                }
            };
            return nativeAttachWithFD(mAddress, mNetmask, mFD, USB_STUCK_TIMEOUT_SEC, errback, listener);
        } else {
            return nativeAttach(mAddress, mNetmask, mRecvBuffer, mListener, errback);
        }
    }

    /**
     * Detach SLIP network interface
     *
     * @return  true if it is successful, otherwise false
     */
    public native boolean detach();

    /**
     * Configure native layer for USB write error handling.
     *
     * @param flag  If true, then native layer will not write to USB transport any more after a write error.
     *              If false, then native layer will ignore the write error. Also, OnErrorListener will not
     *              be called.
     * @return      true if it is successful, otherwise false.
     */
    public native boolean setStopOnUsbWriteError(boolean flag);

    /**
     * Send incoming SLIP packets to native TCP/IP/SLIP stack.
     *
     * @param buffer    The buffer must be direct byte buffer.
     * @param offset    An offset of the data to read in buffer
     * @param length    A length of the data
     * @return          true if it is successful, otherwise false.
     */
    public native boolean input(ByteBuffer buffer, int offset, int length);

    /**
     * Request native layer to stop reading from USB. Read operation will be stopped when native layer receives some data.
     *
     * @return          true if it is successful, otherwise false.
     */
    public native boolean stopReading();

    /**
     * Ask native layer if its read() operation on USB file descriptor is stopped.
     *
     * @return          true if it is stopped, otherwise false.
     */
    public native boolean readStopped();

    /**
     * Request a write buffer empty event through WriteBufferEmptyListener. (USB only)
     *
     * If the buffer is empty, an event is sent immediately through the listener. If the buffer is not
     * empty then an event is notified through the listener when the send buffer gets empty.
     * The event is notified only once. If you need another event then call this method again.
     * Note that WriteBufferEmptyListener is called on a native thread. Therefore it is possible that
     * the event is notified *before* requestWriteBufferEmptyEvent() returns.
     * Write buffer empty events are available only when fd is provided (i.e. only for USB).
     *
     * @return  An integer representing the request. Same ID will be provided through
     *          WriteBufferEmptyListener. On error, -1 is returned.
     */
    public native int requestWriteBufferEmptyEvent();

    private long mNativeInstance;
    private native boolean nativeAttachWithFD(InetAddress address, InetAddress netmask, FileDescriptor fd, int writeStuckTimeoutSec,
                                              NativeErrorListener errback, NativeWriteBufferListener listener);
    private native boolean nativeAttach(InetAddress address, InetAddress netmask, ByteBuffer rcvbuf, OutputPacketListener listener,
            NativeErrorListener errback);

    static {
        System.loadLibrary("tcpip");
    }
}
