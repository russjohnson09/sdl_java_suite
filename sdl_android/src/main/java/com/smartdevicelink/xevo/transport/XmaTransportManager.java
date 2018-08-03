package com.smartdevicelink.xevo.transport;

import java.util.HashMap;

public class XmaTransportManager {
    private HashMap<Integer, XmaSocket> mSocketMap;

    public XmaTransportManager() {
        mSocketMap = new HashMap<Integer, XmaSocket>();
    }

    public Integer addSocket(XmaSocket socket) {
        int hash = socket.hashCode();
        mSocketMap.put(hash, socket);
        return hash;
    }

    public Integer newSocket(NetconnSocket.Protocol protocol) {
        XmaSocket socket = new XmaSocket(protocol);
        return addSocket(socket);
    }

    public XmaSocket getSocket(Integer hash) {
        if (mSocketMap.containsKey(hash)) {
            return mSocketMap.get(hash);
        }
        return null;
    }

    public void releaseSocket(Integer hash) {
        XmaSocket socket = getSocket(hash);
        if (socket != null) {
            socket.close();
            mSocketMap.remove(hash);
        }
    }
}
