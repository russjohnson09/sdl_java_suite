package com.smartdevicelink.xevo.transport;

import android.os.ConditionVariable;
import android.util.Log;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.InetSocketAddress;
import java.net.SocketAddress;
import java.nio.ByteBuffer;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Very thin wrapper class that is used for UMA Library for RTP/RTCP data
 */
public class XmaSocket {
    private NetconnSocket mNetconnSocket;
    private ByteBuffer mSendBuffer;
    private ByteBuffer mRecvBuffer;
    private ConditionVariable mAcceptCond = new ConditionVariable();
    private final String TAG= "XmaSocket";
    private AcceptorTask mAcceptorTask;
    private ExecutorService mReaderExecutor;
    private ConditionVariable mReaderTerminateCond;
    private final int READER_TERMINATE_TIMEOUT_MSEC = 2000;
    private XmaSocketListener mListener;

    // ctor
    public XmaSocket(NetconnSocket.Protocol protocol) {
        mNetconnSocket = new NetconnSocket(protocol);
        mSendBuffer = ByteBuffer.allocateDirect(65535);
        mRecvBuffer = ByteBuffer.allocateDirect(4096);
    }

    public XmaSocket(NetconnSocket socket) {
        mNetconnSocket = socket;
        mSendBuffer = ByteBuffer.allocateDirect(65535);
        mRecvBuffer = ByteBuffer.allocateDirect(4096);
    }

    public int getLocalPort() {
        if (mNetconnSocket != null) {
            InetSocketAddress addr = (InetSocketAddress)mNetconnSocket.getLocalSocketAddress();
            return addr.getPort();
        }
        return -1;
    }

    public SocketAddress getLocalSocketAddress() {
        if (mNetconnSocket != null) {
            return mNetconnSocket.getLocalSocketAddress();
        }
        return null;
    }

    public SocketAddress getRemoteSocketAddress() {
        if (mNetconnSocket != null) {
            return mNetconnSocket.getRemoteSocketAddress();
        }
        return null;
    }

    public void setListener(XmaSocketListener listener) {
        mListener = listener;
    }

    public void receive(DatagramPacket packet) {
        try {
            if (mNetconnSocket != null) {
                int readsz = mNetconnSocket.recv(mRecvBuffer);
                byte[] data = new byte[readsz];
                mRecvBuffer.get(data);
                packet.setData(data);
            }
        } catch(IOException e) {
            e.printStackTrace();
        }
    }

    public void connect(InetSocketAddress addr) {
        if (mNetconnSocket != null) {
            try {
                mNetconnSocket.connect(addr);
                Log.d(TAG, "XmaSocket:connect " + addr.toString());
            } catch(IOException e) {
                e.printStackTrace();
            }
        } else {
            Log.d(TAG, "XmaSocket:connect failed; NetconnSocket is null");
        }
    }

    public void send(DatagramPacket packet) {
        if (mNetconnSocket != null) {
            try {
                mSendBuffer.clear();
                mSendBuffer.put(packet.getData(), 0, packet.getLength());
                mSendBuffer.flip();
                //Log.d(TAG, "XmaSocket:send " + packet.getLength() + " bytes");
                mNetconnSocket.send(mSendBuffer);
            } catch(IOException e) {
                //e.printStackTrace();
            }
        }
    }

    public void close() {
        if (mNetconnSocket != null) {
            try {
                mNetconnSocket.close();
            } catch(IOException e) {
                e.printStackTrace();
            }
        }
    }

    public void bind(InetSocketAddress addr) {
        if (mNetconnSocket != null) {
            try {
                mNetconnSocket.bind(addr.getAddress(), addr.getPort());
                Log.d(TAG, "XmaSocket:bind " + addr.toString());
            } catch(IOException e) {
                e.printStackTrace();
            }
        }
    }

    public void startAcceptorTask(InetSocketAddress address) {
        if (mAcceptorTask == null) {
            Log.d(TAG, "xmaProvider: about creating AcceptorTask; address=" + address.toString());
            // make sure socket exists
            if (mNetconnSocket != null) {
                try {
                    mNetconnSocket.bind(address.getAddress(), address.getPort());
                    mAcceptCond.close();
                    mAcceptorTask = new AcceptorTask();
                    mAcceptorTask.start();
                    mAcceptCond.block();
                } catch(IOException e) {
                    e.printStackTrace();
                }
            } else {
                Log.d(TAG, "NetconnSocket is null");
            }
        }
    }

    public void stopAcceptorTask() {
        if (mNetconnSocket != null) {
            try {
                mNetconnSocket.close();
            } catch(IOException e) {
                e.printStackTrace();
            }
        }
        if (mAcceptorTask != null) {
            mAcceptorTask.interrupt();

        }
        mNetconnSocket = null;
        mAcceptorTask = null;
    }

    private class AcceptorTask extends Thread {
        private boolean mFirstAccept = true;

        public AcceptorTask() {
            setName("ServerAcceptor");
        }

        @Override
        public void run() {
            try {
                while (!Thread.currentThread().isInterrupted()) {
                    // accept() void param is blocking function. We have to specify timeout here.
                    // For the first time, timeout is set to 1 msec. This is to call mAcceptCond.open() quickly.
                    //NetconnSocket socket = mNetconnSocket.accept(mFirstAccept ? 1 : 100, 0);
                    NetconnSocket socket = mNetconnSocket.accept(500, 0);
                    //Log.d(TAG, "xmaProvider: accept NetconnSocket");
                    if (mFirstAccept) {
                        mAcceptCond.open();
                        mFirstAccept = false;
                    }

                    if (socket != null) {
                        Log.d(TAG, "xmaProvider: Accept new connection :" + socket);
                        if (mListener != null) {
                            mListener.onAccept(new XmaSocket(socket));
                        }
                    } else {
                        //Log.d(TAG, "xmaProvider: accept timed out");
                    }
                }
                Log.d(TAG, "xmaProvider: AcceptorTask end");
            } catch (IOException e) {
                //sLogger.info("", e);
            } finally {
                // in case an exception happened or thread was interrupted before accept()
                mAcceptCond.open();

                if (mNetconnSocket != null) {
                    try {
                        mNetconnSocket.close();
                    } catch (IOException e) {}
                }
            }
        }
    }

    public void startReaderTask() {
        if (mReaderExecutor == null) {
            mReaderExecutor = Executors.newCachedThreadPool();
            mReaderExecutor.submit(new Runnable() {
                @Override
                public void run() {
                    ByteBuffer buffer = ByteBuffer.allocate(512);
                    mReaderTerminateCond = new ConditionVariable();
                    DatagramPacket packet = new DatagramPacket(buffer.array(), 512);
                    while (!Thread.currentThread().isInterrupted()) {
                        try {
                            int readsz = mNetconnSocket.recv(mRecvBuffer);
                            byte[] data = new byte[readsz];
                            mRecvBuffer.get(data);
                            packet.setData(data);
                            if (packet.getLength() == 0)
                                continue;
                            if (mListener != null) {
                                mListener.onReceive(packet);
                            }
                        } catch (IOException e) {
                            e.printStackTrace();
                            break;
                        }
                    }
                    mReaderTerminateCond.open();
                }
            });
        }
    }

    public void stopReaderTask() {
        if (mNetconnSocket != null) {
            try { mNetconnSocket.close(); } catch (IOException e) {}
            if (mReaderExecutor != null) {
                // wait until receive thread stops, then put null to mRtcpSocket
                mReaderExecutor.shutdownNow();
                mReaderTerminateCond.block(READER_TERMINATE_TIMEOUT_MSEC);
                mReaderExecutor = null;
            }
            mNetconnSocket = null;
        }

    }

    public interface XmaSocketListener {
        void onAccept(XmaSocket socket);

        void onReceive(DatagramPacket packet);
    }

}
