// Copyright 2017 Xevo Inc. All rights reserved.
package com.smartdevicelink.xevo.slip;

/**
 * Common interface that every SLIP driver must implement.
 *
 * @hide
 */
public interface ISlipDriver {
    /**
     * State for SLIP I/F driver.
     */
    public static enum State {
        IDLE,
        LISTENING,
        WAITING_PERMISSION,     // This state is used only by UsbSlipDriver.
        CONNECTING,
        CONNECTED,
        DISCONNECTED,
        STOPPING,
    }

    /**
     * Interface for monitoring SLIP I/F driver's state.
     */
    public interface StateListener {
        void onStateChanged(State state);
    }

    /**
     * Returns current state of the SPP driver.
     */
    State getState();

    /**
     * Returns whether connection or not.
     */
    boolean isConnected();

    /**
     * Start SLIP driver service.
     *
     * @return true if it is successful, otherwise false
     */
    boolean start();

    /**
     * Stop SLIP driver service.
     *
     * @return true if it is successful, otherwise false.
     */
    boolean stop();

    /**
     * Set event listener monitoring application state change.
     *
     * @param listener
     */
    void setStateListener(StateListener listener);
}
