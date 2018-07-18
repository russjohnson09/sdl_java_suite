package com.smartdevicelink.SdlConnection;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.PipedInputStream;
import java.io.PipedOutputStream;
import java.util.HashMap;
import java.util.List;
import java.util.ListIterator;
import java.util.concurrent.CopyOnWriteArrayList;

import android.annotation.SuppressLint;
import android.os.Build;
import android.util.Log;
import android.view.Surface;

import com.smartdevicelink.encoder.SdlEncoder;
import com.smartdevicelink.encoder.VirtualDisplayEncoder;
import com.smartdevicelink.exception.SdlException;
import com.smartdevicelink.protocol.ProtocolMessage;
import com.smartdevicelink.protocol.enums.SessionType;
import com.smartdevicelink.protocol.heartbeat.IHeartbeatMonitor;
import com.smartdevicelink.protocol.heartbeat.IHeartbeatMonitorListener;
import com.smartdevicelink.proxy.LockScreenManager;
import com.smartdevicelink.proxy.RPCRequest;
import com.smartdevicelink.proxy.interfaces.IAudioStreamListener;
import com.smartdevicelink.proxy.interfaces.ISdlServiceListener;
import com.smartdevicelink.proxy.interfaces.IVideoStreamListener;
import com.smartdevicelink.proxy.rpc.VideoStreamingFormat;
import com.smartdevicelink.proxy.rpc.enums.VideoStreamingProtocol;
import com.smartdevicelink.security.ISecurityInitializedListener;
import com.smartdevicelink.security.SdlSecurityBase;
import com.smartdevicelink.streaming.AbstractPacketizer;
import com.smartdevicelink.streaming.IStreamListener;
import com.smartdevicelink.streaming.video.RTPH264Packetizer;
import com.smartdevicelink.streaming.StreamPacketizer;
import com.smartdevicelink.streaming.StreamRPCPacketizer;
import com.smartdevicelink.streaming.video.VideoStreamingParameters;
import com.smartdevicelink.transport.BaseTransportConfig;
import com.smartdevicelink.transport.MultiplexTransport;
import com.smartdevicelink.transport.MultiplexTransportConfig;
import com.smartdevicelink.transport.enums.TransportType;

public class SdlLegacySession  extends SdlSession implements ISdlConnectionListener, IHeartbeatMonitorListener, IStreamListener, ISecurityInitializedListener {
    private static CopyOnWriteArrayList<SdlConnection> shareConnections = new CopyOnWriteArrayList<SdlConnection>();
    private CopyOnWriteArrayList<SessionType> encryptedServices = new CopyOnWriteArrayList<SessionType>();

    SdlConnection _sdlConnection = null;
    private byte wiproProcolVer;
    IHeartbeatMonitor _outgoingHeartbeatMonitor = null;
    IHeartbeatMonitor _incomingHeartbeatMonitor = null;
    private static final String TAG = "SdlLegacySession";
    private SdlSecurityBase sdlSecurity = null;

    private final static int BUFF_READ_SIZE = 1024;
    private int sessionHashId = 0;
    private HashMap<SessionType, CopyOnWriteArrayList<ISdlServiceListener>> serviceListeners;
    private VideoStreamingParameters desiredVideoParams = null;
    private VideoStreamingParameters acceptedVideoParams = null;


    public static SdlLegacySession createSession(byte wiproVersion, ISdlConnectionListener listener, BaseTransportConfig btConfig) {

        SdlLegacySession session =  new SdlLegacySession();
        session.wiproProcolVer = wiproVersion;
        session.sessionListener = listener;
        session.transportConfig = btConfig;

        return session;
    }


    public SdlConnection getSdlConnection() {
        return this._sdlConnection;
    }

    public int getMtu(){
        if(this._sdlConnection!=null){
            return this._sdlConnection.getWiProProtocol().getMtu();
        }else{
            return 0;
        }
    }

    public long getMtu(SessionType type) {
        if (this._sdlConnection != null) {
            return this._sdlConnection.getWiProProtocol().getMtu(type);
        } else {
            return 0;
        }
    }

    public void close() {
        if (sdlSecurity != null)
        {
            sdlSecurity.resetParams();
            sdlSecurity.shutDown();
        }

        if (_sdlConnection != null) { //sessionId == 0 means session is not started.
            _sdlConnection.unregisterSession(this);

            if (_sdlConnection.getRegisterCount() == 0) {
                shareConnections.remove(_sdlConnection);
            }

            _sdlConnection = null;
        }
    }

    public void startStream(InputStream is, SessionType sType, byte rpcSessionID) throws IOException {
        if (sType.equals(SessionType.NAV))
        {
            // protocol is fixed to RAW
            StreamPacketizer packetizer = new StreamPacketizer(this, is, sType, rpcSessionID, this);
            packetizer.sdlConnection = this.getSdlConnection();
            mVideoPacketizer = packetizer;
            mVideoPacketizer.start();
        }
        else if (sType.equals(SessionType.PCM))
        {
            mAudioPacketizer = new StreamPacketizer(this, is, sType, rpcSessionID, this);
            mAudioPacketizer.sdlConnection = this.getSdlConnection();
            mAudioPacketizer.start();
        }
    }

    @SuppressLint("NewApi")
    public OutputStream startStream(SessionType sType, byte rpcSessionID) throws IOException {
        OutputStream os = new PipedOutputStream();
        InputStream is = null;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.GINGERBREAD) {
            is = new PipedInputStream((PipedOutputStream) os, BUFF_READ_SIZE);
        } else {
            is = new PipedInputStream((PipedOutputStream) os);
        }
        if (sType.equals(SessionType.NAV))
        {
            // protocol is fixed to RAW
            StreamPacketizer packetizer = new StreamPacketizer(this, is, sType, rpcSessionID, this);
            packetizer.sdlConnection = this.getSdlConnection();
            mVideoPacketizer = packetizer;
            mVideoPacketizer.start();
        }
        else if (sType.equals(SessionType.PCM))
        {
            mAudioPacketizer = new StreamPacketizer(this, is, sType, rpcSessionID, this);
            mAudioPacketizer.sdlConnection = this.getSdlConnection();
            mAudioPacketizer.start();
        }
        else
        {
            os.close();
            is.close();
            return null;
        }
        return os;
    }

    public IVideoStreamListener startVideoStream() {
        byte rpcSessionID = getSessionId();
        VideoStreamingProtocol protocol = getAcceptedProtocol();
        try {
            switch (protocol) {
                case RAW: {
                    StreamPacketizer packetizer = new StreamPacketizer(this, null, SessionType.NAV, rpcSessionID, this);
                    packetizer.sdlConnection = this.getSdlConnection();
                    mVideoPacketizer = packetizer;
                    mVideoPacketizer.start();
                    return packetizer;
                }
                case RTP: {
                    RTPH264Packetizer packetizer = new RTPH264Packetizer(this, SessionType.NAV, rpcSessionID, this);
                    mVideoPacketizer = packetizer;
                    mVideoPacketizer.start();
                    return packetizer;
                }
                default:
                    Log.e(TAG, "Protocol " + protocol + " is not supported.");
                    return null;
            }
        } catch (IOException e) {
            return null;
        }
    }

    public IAudioStreamListener startAudioStream() {
        byte rpcSessionID = getSessionId();
        try {
            StreamPacketizer packetizer = new StreamPacketizer(this, null, SessionType.PCM, rpcSessionID, this);
            packetizer.sdlConnection = this.getSdlConnection();
            mAudioPacketizer = packetizer;
            mAudioPacketizer.start();
            return packetizer;
        } catch (IOException e) {
            return null;
        }
    }

    public void startService (SessionType serviceType, byte sessionID, boolean isEncrypted) {
        if (_sdlConnection == null)
            return;

        if (isEncrypted)
        {
            if (sdlSecurity != null)
            {
                List<SessionType> serviceList = sdlSecurity.getServiceList();
                if (!serviceList.contains(serviceType))
                    serviceList.add(serviceType);

                sdlSecurity.initialize();
            }
            return;
        }
        _sdlConnection.startService(serviceType, sessionID, isEncrypted);
    }

    public void endService (SessionType serviceType, byte sessionID) {
        if (_sdlConnection == null)
            return;
        _sdlConnection.endService(serviceType, sessionID);
    }

    private void processControlService(ProtocolMessage msg) {
        if (sdlSecurity == null)
            return;
        int ilen = msg.getData().length - 12;
        byte[] data = new byte[ilen];
        System.arraycopy(msg.getData(), 12, data, 0, ilen);

        byte[] dataToRead = new byte[4096];

        Integer iNumBytes = sdlSecurity.runHandshake(data, dataToRead);

        if (iNumBytes == null || iNumBytes <= 0)
            return;

        byte[] returnBytes = new byte[iNumBytes];
        System.arraycopy(dataToRead, 0, returnBytes, 0, iNumBytes);
        ProtocolMessage protocolMessage = new ProtocolMessage();
        protocolMessage.setSessionType(SessionType.CONTROL);
        protocolMessage.setData(returnBytes);
        protocolMessage.setFunctionID(0x01);
        protocolMessage.setVersion(wiproProcolVer);
        protocolMessage.setSessionID(getSessionId());

        //sdlSecurity.hs();

        sendMessage(protocolMessage);
    }

    public String getBroadcastComment(BaseTransportConfig myTransport) {
        SdlConnection connection = null;
        if (myTransport.shareConnection()) {
            connection = findTheProperConnection(myTransport);
        } else {
            connection = this._sdlConnection;
        }

        if (connection != null)
            return connection.getBroadcastComment();

        return "";
    }


    public void startSession() throws SdlException {
        SdlConnection connection = null;
        if (this.transportConfig.shareConnection()) {
            connection = findTheProperConnection(this.transportConfig);

            if (connection == null) {
                connection = new SdlConnection(this.transportConfig);
                shareConnections.add(connection);
            }
        } else {
            connection = new SdlConnection(this.transportConfig);
        }

        this._sdlConnection = connection;
        connection.registerSession(this); //Handshake will start when register.
    }

    private void initialiseSession() {
        if (_outgoingHeartbeatMonitor != null) {
            _outgoingHeartbeatMonitor.start();
        }
        if (_incomingHeartbeatMonitor != null) {
            _incomingHeartbeatMonitor.start();
        }
    }

    public void sendMessage(ProtocolMessage msg) {
        if (_sdlConnection == null)
            return;
        _sdlConnection.sendMessage(msg);
    }

    public TransportType getCurrentTransportType() {
        if (_sdlConnection == null)
            return null;
        return _sdlConnection.getCurrentTransportType();
    }

    public boolean getIsConnected() {
        if (_sdlConnection == null)
            return false;
        return _sdlConnection != null && _sdlConnection.getIsConnected();
    }

    public boolean isServiceProtected(SessionType sType) {
        return encryptedServices.contains(sType);
    }

    @Override
    public void onTransportDisconnected(String info) {
        this.sessionListener.onTransportDisconnected(info);
    }

    @Override
    public void onTransportDisconnected(String info, boolean b, MultiplexTransportConfig config) {

    }

    @Override
    public void onTransportError(String info, Exception e) {
        this.sessionListener.onTransportError(info, e);
    }

    @Override
    public void onProtocolMessageReceived(ProtocolMessage msg) {
        if (msg.getSessionType().equals(SessionType.CONTROL)) {
            processControlService(msg);
            return;
        }

        this.sessionListener.onProtocolMessageReceived(msg);
    }

    @Override
    public void onHeartbeatTimedOut(byte sessionID) {
        this.sessionListener.onHeartbeatTimedOut(sessionID);

    }


    @Override
    public void onProtocolSessionStarted(SessionType sessionType,
                                         byte sessionID, byte version, String correlationID, int hashID, boolean isEncrypted) {
        this.sessionId = sessionID;
        lockScreenMan.setSessionID(sessionID);
        if (isEncrypted)
            encryptedServices.addIfAbsent(sessionType);
        this.sessionListener.onProtocolSessionStarted(sessionType, sessionID, version, correlationID, hashID, isEncrypted);
        if(serviceListeners != null && serviceListeners.containsKey(sessionType)){
            CopyOnWriteArrayList<ISdlServiceListener> listeners = serviceListeners.get(sessionType);
            for(ISdlServiceListener listener:listeners){
                listener.onServiceStarted(this, sessionType, isEncrypted);
            }
        }
        //if (version == 3)
        initialiseSession();
        if (sessionType.eq(SessionType.RPC)){
            sessionHashId = hashID;
        }
    }

    @Override
    public void onProtocolSessionEnded(SessionType sessionType, byte sessionID,
                                       String correlationID) {
        this.sessionListener.onProtocolSessionEnded(sessionType, sessionID, correlationID);
        if(serviceListeners != null && serviceListeners.containsKey(sessionType)){
            CopyOnWriteArrayList<ISdlServiceListener> listeners = serviceListeners.get(sessionType);
            for(ISdlServiceListener listener:listeners){
                listener.onServiceEnded(this, sessionType);
            }
        }
        encryptedServices.remove(sessionType);
    }

    @Override
    public void onProtocolError(String info, Exception e) {
        this.sessionListener.onProtocolError(info, e);
    }

    @Override
    public void sendHeartbeat(IHeartbeatMonitor monitor) {
        Log.d(TAG, "Asked to send heartbeat");
        if (_sdlConnection != null)
            _sdlConnection.sendHeartbeat(this);
    }

    @Override
    public void heartbeatTimedOut(IHeartbeatMonitor monitor) {
        if (_sdlConnection != null)
            _sdlConnection._connectionListener.onHeartbeatTimedOut(this.sessionId);
        close();
    }

    private static SdlConnection findTheProperConnection(BaseTransportConfig config) {
        SdlConnection connection = null;

        int minCount = 0;
        for (SdlConnection c : shareConnections) {
            if (c.getCurrentTransportType() == config.getTransportType()) {
                if (minCount == 0 || minCount >= c.getRegisterCount()) {
                    connection = c;
                    minCount = c.getRegisterCount();
                }
            }
        }

        return connection;
    }

    @Override
    public void onProtocolSessionStartedNACKed(SessionType sessionType,
                                               byte sessionID, byte version, String correlationID, List<String> rejectedParams) {
        this.sessionListener.onProtocolSessionStartedNACKed(sessionType,
                sessionID, version, correlationID, rejectedParams);
        if(serviceListeners != null && serviceListeners.containsKey(sessionType)){
            CopyOnWriteArrayList<ISdlServiceListener> listeners = serviceListeners.get(sessionType);
            for(ISdlServiceListener listener:listeners){
                listener.onServiceError(this, sessionType, "Start "+ sessionType.toString() +" Service NACK'ed");
            }
        }
    }

    @Override
    public void onProtocolSessionEndedNACKed(SessionType sessionType,
                                             byte sessionID, String correlationID) {
        this.sessionListener.onProtocolSessionEndedNACKed(sessionType, sessionID, correlationID);
        if(serviceListeners != null && serviceListeners.containsKey(sessionType)){
            CopyOnWriteArrayList<ISdlServiceListener> listeners = serviceListeners.get(sessionType);
            for(ISdlServiceListener listener:listeners){
                listener.onServiceError(this, sessionType, "End "+ sessionType.toString() +" Service NACK'ed");
            }
        }
    }

    @Override
    public void onProtocolServiceDataACK(SessionType sessionType, int dataSize, byte sessionID) {
        this.sessionListener.onProtocolServiceDataACK(sessionType, dataSize, sessionID);
    }

    @Override
    public void onSecurityInitialized() {

        if (_sdlConnection != null && sdlSecurity != null)
        {
            List<SessionType> list = sdlSecurity.getServiceList();

            SessionType service;
            ListIterator<SessionType> iter = list.listIterator();

            while (iter.hasNext()) {
                service = iter.next();

                if (service != null)
                    _sdlConnection.startService(service, getSessionId(), true);

                iter.remove();
            }
        }
    }

    public void clearConnection(){
        _sdlConnection = null;
    }

    public void checkForOpenMultiplexConnection(SdlConnection connection){
        removeConnection(connection);
        connection.unregisterSession(this);
        _sdlConnection = null;
        for (SdlConnection c : shareConnections) {
            if (c.getCurrentTransportType() == TransportType.MULTIPLEX) {
                if(c.getIsConnected() || ((MultiplexTransport)c._transport).isPendingConnected()){
                    _sdlConnection = c;
                    try {
                        _sdlConnection.registerSession(this);//Handshake will start when register.
                    } catch (SdlException e) {
                        e.printStackTrace();
                    }
                    return;
                }

            }
        }
    }
    public static void removeConnection(SdlConnection connection){
        shareConnections.remove(connection);
    }

    public void addServiceListener(SessionType serviceType, ISdlServiceListener sdlServiceListener){
        if(serviceListeners == null){
            serviceListeners = new HashMap<>();
        }
        if(serviceType != null && sdlServiceListener != null){
            if(!serviceListeners.containsKey(serviceType)){
                serviceListeners.put(serviceType,new CopyOnWriteArrayList<ISdlServiceListener>());
            }
            serviceListeners.get(serviceType).add(sdlServiceListener);
        }
    }

}
