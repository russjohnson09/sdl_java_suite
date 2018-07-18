/*
 * Copyright 2017 Xevo Inc. All rights reserved.
 */
package com.smartdevicelink.xevo.transport;

import com.smartdevicelink.util.DebugTool;

import java.io.IOException;
import java.net.BindException;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.SocketAddress;
import java.nio.ByteBuffer;
import java.security.InvalidParameterException;

/**
 * This class is the Java API that encapsulates lwIP's Netconn APIs in native.
 * Both TCP and UDP is supported on one class.
 *
 * @hide
 */
@SuppressWarnings("unused")
public class NetconnSocket {
    /**
     * Transport protocol
     *
     * @see NetconnSocket#NetconnSocket(Protocol)
     */
    public enum Protocol {
        TCP,
        UDP
    }

    /**
     * Type to specify SSL context created by native should describe the server or client side.
     */
    public enum SSLSide {
        SERVER,
        CLIENT,
    }

    public static final int DEFAULT_BACKLOG = 4;

    // See err.h in lwIP
    public static final int ERR_MEM                     = -1;
    public static final int ERR_BUF                     = -2;
    public static final int ERR_TIMEOUT                 = -3;
    public static final int ERR_RTE                     = -4;
    public static final int ERR_INPROGRESS              = -5;
    public static final int ERR_VAL                     = -6;
    public static final int ERR_WOULDBLOCK              = -7;
    public static final int ERR_USE                     = -8;
    public static final int ERR_ISCONN                  = -9;
    public static final int ERR_ABRT                    = -10;
    public static final int ERR_RST                     = -11;
    public static final int ERR_CLSD                    = -12;
    public static final int ERR_CONN                    = -13;
    public static final int ERR_ARG                     = -14;
    public static final int ERR_IF                      = -15;

    // SSL error codes
    public static final int ERR_SSL_SSL                 = -101;
    public static final int ERR_SSL_WANT_READ           = -102;
    public static final int ERR_SSL_WANT_WRITE          = -103;
    public static final int ERR_SSL_WANT_X509_LOOKUP    = -104;
    public static final int ERR_SSL_SYSCALL             = -105;
    public static final int ERR_SSL_ZERO_RETURN         = -106;
    public static final int ERR_SSL_WANT_CONNECT        = -107;
    public static final int ERR_SSL_WANT_ACCEPT         = -108;

    private Protocol mProtocol;
    private boolean mServerSide;

    /**
     * Close all active PCBs in TCP/IP stack.
     */
    public static void resetStack() {
        DebugTool.logInfo("Clearing out TCP/IP stack");
        nativeResetStack();
    }

    /**
     * Create new un-secure socket for the protocol.
     *
     * @param protocol
     */
    public NetconnSocket(Protocol protocol) {
        nativeNew(protocol.ordinal());
        mProtocol = protocol;
    }

    /**
     * Create secure socket for the specified protocol.
     * Second parameter side specifies server or client side.
     *
     * @param protocol  TCP or UDP
     * @param side      Client or Server
     */
    public NetconnSocket(Protocol protocol, SSLSide side) {
        nativeNew(protocol.ordinal());
        mServerSide = (side == SSLSide.SERVER);
        mProtocol = protocol;
    }

    // Initialized with accepted native socket instance
    NetconnSocket(long nativeInstance) {
        mNativeInstance = nativeInstance;
        nativeSetOwner();
        mProtocol = Protocol.TCP;
    }

    @Override
    public void finalize() throws Throwable {
        super.finalize();
        nativeRelease();
    }

    /**
     * Bind address and port.
     *
     * @param addr
     * @param port
     * @throws BindException
     */
    public void bind(InetAddress addr, int port) throws BindException {
        int err;
        // NetconnSocket supports Inet4Address only
        if (!(addr instanceof Inet4Address)) {
            DebugTool.logError("NetconnSocket cannot bind Inet6Address");
            throw new BindException("bind failed: " + "cannot bind Inet6Address");
        }
        if ((err = nativeBind(addr, port)) < 0) {
            throw new BindException("bind failed: " + errorString(err));
        }
        if (mProtocol == Protocol.TCP) {
            nativeListen(DEFAULT_BACKLOG);
        }
    }

    /**
     * Accepts a connection to this server socket.
     *
     * @param timeout
     * @return
     */
    public NetconnSocket accept(int timeout, int sslHandshakeTimeout) throws IOException {
        long nativeInstance = nativeAccept(timeout, sslHandshakeTimeout);
        if (nativeInstance != 0) {
            return new NetconnSocket(nativeInstance);
        }
        return null;
    }

    /**
     * Accepts a connection to this server socket.
     *
     * @return  The accepted socket instance.
     */
    public NetconnSocket accept() throws IOException {
        return accept(0, 0);
    }

    /**
     * Close the connection.
     *
     * @throws IOException
     */
    public int close() throws IOException {
        return nativeClose();
    }

    /**
     * Connect to peer.
     *
     * @param remoteAddr
     * @throws IOException
     */
    public int connect(SocketAddress remoteAddr) throws IOException {
        return connect(remoteAddr, 0);
    }

    /**
     * Connect to peer.
     *
     * @param remoteAddr
     * @throws IOException
     */
    public int connect(SocketAddress remoteAddr, int timeout) throws IOException {
        InetSocketAddress addr = (InetSocketAddress)remoteAddr;
        return nativeConnect(addr.getAddress(), addr.getPort(), 0);
    }

    /**
     * Send data to the socket.
     *
     * @param buffer
     * @throws IOException
     */
    public void send(byte[] buffer) throws IOException {
        if (buffer == null) {
            throw new InvalidParameterException("buffer is null");
        }
        ByteBuffer buf = ByteBuffer.allocateDirect(buffer.length);
        buf.put(buffer);
        buf.flip();
        send(buf);
    }

    /**
     * Send data to the socket.
     *
     * @param buffer
     * @param offset
     * @param length
     */
    public void send(byte[] buffer, int offset, int length) throws IOException {
        if (buffer == null) {
            throw new InvalidParameterException("buffer is null");
        }

        ByteBuffer buf = ByteBuffer.allocateDirect(length);
        buf.put(buffer, offset, length);
        buf.flip();
        send(buf);
    }

    /**
     * Send data to the socket.
     *
     * @param buffer
     * @throws IOException
     */
    public void send(ByteBuffer buffer) throws IOException {
        if (buffer == null) {
            throw new InvalidParameterException("buffer is null");
        }

        // Note that buffer must be DirectBuffer
        ByteBuffer buf = buffer;
        if (!buffer.isDirect()) {
            buf = ByteBuffer.allocateDirect(buffer.capacity());
            buf.rewind();
            buf.put(buffer);
            buffer.rewind();
            buf.flip();
        }
        int err;
        if ((err = nativeSend(buf, buf.position(), buf.limit() - buf.position())) < 0) {
            throw new IOException("send failed: " + errorString(err));
        }
    }

    /**
     * Receive incoming data from the connection.
     *
     * @param buffer
     * @return
     * @throws IOException
     */
    public int recv(ByteBuffer buffer) throws IOException {
        return recv(buffer, 0);
    }

    /**
     * Receive incoming data from the connection.
     * Note that it always fill received data into begging of the buffer.
     *
     * @param buffer        Buffer to fill the incoming data
     * @param timeoutMs     Timeout in milliseconds, no timeout if 0.
     * @return              Number of bytes to read from the connection, or error code if failed.
     * @throws IOException
     */
    public int recv(ByteBuffer buffer, int timeoutMs) throws IOException {
        if (!buffer.isDirect()) {
            throw new InvalidParameterException("buffer is not DirectBuffer");
        }

        int readsz = nativeRecv(buffer, timeoutMs);
        if (readsz < 0) {
            if (readsz != ERR_TIMEOUT) {
                throw new IOException("recv failed: " + errorString(readsz));
            }
            return readsz;
        }
        buffer.position(0);
        buffer.limit(readsz);
        return readsz;
    }

    /**
     * Returns the remote address and port of this socket as a SocketAddress or null if the socket is not connected.
     */
    public SocketAddress getRemoteSocketAddress() {
        return nativeGetRemoteSocketAddress();
    }

    /**
     * Returns the local address and port of this socket as a SocketAddress or null if the socket has never been bound.
     *
     * @return
     */
    public SocketAddress getLocalSocketAddress() {
        return nativeGetLocalSocketAddress();
    }

    /**
     * Specifies whether a reuse of a local address is allowed when another socket has not yet been removed the operating system.
     *
     * @param reuse
     */
    public void setReuseAddress(boolean reuse) {
        nativeSetReuseAddress(reuse);
    }

    /**
     * Set SSL certificate.
     * It turns on SSL mode after certificate is set by this method.
     *
     * @param pkcs12    PKCS#12 data
     * @param password  A Password to decrypt PKCS#12 data
     * @return
     */
    public int setCertificate(ByteBuffer pkcs12, String password) {
        if (pkcs12 == null) {
            throw new InvalidParameterException("pkcs12 is null");
        }
        if (password == null) {
            throw new InvalidParameterException("password is null");
        }

        ByteBuffer pkcs12Data = pkcs12;
        if (!pkcs12.isDirect()) {
            pkcs12Data = ByteBuffer.allocateDirect(pkcs12.limit());
            pkcs12Data.put(pkcs12);
            pkcs12Data.flip();
            pkcs12.rewind();
        }
        return nativeSetCertificate(pkcs12Data, pkcs12Data.limit(), password, mServerSide);
    }

    /**
     * Get error description
     *
     * @param err
     * @return
     */
    static public String errorString(int err) {
        switch (err) {
            // lwIP errors
            case ERR_MEM:                   return "Out of memory";
            case ERR_BUF:                   return "Buffer error";
            case ERR_TIMEOUT:               return "Timeout";
            case ERR_RTE:                   return "Routing problem";
            case ERR_INPROGRESS:            return "Operation in progress";
            case ERR_VAL:                   return "Illegal value";
            case ERR_WOULDBLOCK:            return "Operation would block";
            case ERR_USE:                   return "Address in use";
            case ERR_ISCONN:                return "Already connected";
            case ERR_ABRT:                  return "Connection aborted";
            case ERR_RST:                   return "Connection reset";
            case ERR_CLSD:                  return "Connection closed";
            case ERR_CONN:                  return "Not connected";
            case ERR_ARG:                   return "Illegal argument";
            case ERR_IF:                    return "Netif error";

            // SSL errors
            case ERR_SSL_SSL:               return "SSL error";
            case ERR_SSL_WANT_READ:         return "SSL want read";
            case ERR_SSL_WANT_WRITE:        return "SSL want write";
            case ERR_SSL_WANT_X509_LOOKUP:  return "SSL want X509 lookup";
            case ERR_SSL_SYSCALL:           return "SSL syscall";
            case ERR_SSL_ZERO_RETURN:       return "SSL closed";
            case ERR_SSL_WANT_CONNECT:      return "SSL connect not completed";
            case ERR_SSL_WANT_ACCEPT:       return "SSL accept not completed";
            default:                        return "unknown error";
        }
    }

    // Native functions
    private long mNativeInstance;
    private native void nativeSetOwner();
    private native int nativeNew(int protocol);
    private native int nativeRelease();
    private native int nativeBind(InetAddress addr, int port);
    private native long nativeAccept(int timeout, int sslHandshakeTimeout);
    private native int nativeListen(int backlog);
    private native int nativeSend(ByteBuffer buffer, int offset, int limit);
    private native int nativeRecv(ByteBuffer buffer, int timeout);
    private native int nativeConnect(InetAddress addr, int port, int timeout);
    private native int nativeClose();
    private native SocketAddress nativeGetLocalSocketAddress();
    private native SocketAddress nativeGetRemoteSocketAddress();
    private native void nativeSetReuseAddress(boolean reuse);
    private native static void nativeResetStack();

    // TLS support
    private native int nativeSetCertificate(ByteBuffer pkcs12, int length, String password, boolean is_server);

    static {
        System.loadLibrary("tcpip");
    }
}
