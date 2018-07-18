package com.smartdevicelink.transport;

import android.content.Context;

import com.smartdevicelink.transport.enums.TransportType;

public class XevoSlipTransportConfig extends TCPTransportConfig {
    /**
     * Constructor. Objects of this class must be created for known port and IP address value.
     *
     * @param port          Port for TCP connection.
     * @param ipAddress     IP address for TCP connection.
     * @param autoReconnect Flag which must be set to true if tcp connection must be automatically reestablished in
     */
    public XevoSlipTransportConfig(int port, String ipAddress, boolean autoReconnect) {
        super(port, ipAddress, autoReconnect);
    }

    /**
     * Overridden abstract method which returns specific type of this transport configuration.
     *
     * @return Constant value TransportType.TCP.
     *
     * @see TransportType
     */
    public TransportType getTransportType() {
        return TransportType.USB;
    }
}
