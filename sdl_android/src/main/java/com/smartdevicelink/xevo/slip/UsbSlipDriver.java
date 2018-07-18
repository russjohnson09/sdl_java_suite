

package com.smartdevicelink.xevo.slip;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.res.XmlResourceParser;
import android.hardware.usb.UsbAccessory;
import android.hardware.usb.UsbManager;
import android.os.BatteryManager;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;

import org.xmlpull.v1.XmlPullParser;
import java.io.IOException;
import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.ArrayList;

import com.smartdevicelink.R;

import com.smartdevicelink.transport.TransportConstants;
import com.smartdevicelink.util.DebugTool;
import com.smartdevicelink.xevo.util.PingSender;

/**
 * USB SLIP interface driver.
 *
 * @hide
 */
public class UsbSlipDriver implements ISlipDriver {
    /**
     * Broadcast action: Sent when an user has granted access to the USB accessory.
     *
     * Constant Value: ""com.smartdevicelink.usb.ACTION_USB_PERMISSION"
     */
    public static final String ACTION_USB_PERMISSION =
            "com.smartdevicelink.usb.ACTION_USB_PERMISSION";

    public interface DialogListener {
        enum Result {
            PERMISSION_GRANTED,
            PERMISSION_DENIED_OR_DIALOG_DISMISSED,
        }
        void requestShowDialog(Runnable showDialog);
        void onDialogResult(Result result);
    }

    public interface ConnectionListener {
        // called when ACTION_USB_ACCESSORY_DETACHED is received
        void onDetached();
        // called when ACTION_POWER_DISCONNECTED is received
        void onDisconnected();
    }

    public enum ActivityEvent {
        STARTED,
        RESUMED,
        PAUSED,
        STOPPED,
    }

    // Broadcast action from CAPP app: must be in sync with TCAPP code.
    private static final String ACTION_CAPP_FOREGROUND_NOTIFICATION =
            "com.main.gcapp.action.FOREGROUND_NOTIFICATION";
    // Broadcast action from CAPP app: must be in sync with TCAPP code. Right now, not used.
    private static final String ACTION_CAPP_BACKGROUND_NOTIFICATION =
            "com.main.gcapp.action.BACKGROUND_NOTIFICATION";

    static private UsbSlipDriver sInstance;

    // detect Type-A or Type-B
    private static final boolean SUPPORT_WIFI_TRANSITION
            = false;//PolicyManager.getInstance().getBooleanValue(PolicyManager.RTSP_WIFI_TRANSITION, false);

    private static final long USB_READ_STOP_TIMEOUT_MSEC = 1000;
    private static final long USB_PERMISSION_DIALOG_DELAY_MSEC = 1000;

    private static final long DIALOG_MAY_BE_DISMISSED_NSEC = 1000000000; // 1 sec
    private static final long USB_PERMISSION_DIALOG_RETRY_DELAY_MSEC = 2000;
    private static final long USB_PERMISSION_DIALOG_EXTRA_DELAY_MSEC = 2000;

    // Params for ICMP ping settings
    private static final int PING_INTERVAL_MSEC = 100;
    private static final int PING_DATA_SIZE     = 32;
    private static final int PINGSENDER_STOP_TIMEOUT_MSEC = 500;

    // Cable check timer settings
    private static final int CABLE_CHECK_AFTER_START_RETRY = 20;
    private static final int CABLE_CHECK_INTERVAL_MSEC = 1000;

    private static final int RETRY_AFTER_ERROR_INITIAL_DELAY_MSEC = 3000;
    private static final int RETRY_AFTER_ERROR_INTERVAL_MSEC = 2000;
    private static final int RETRY_AFTER_ERROR_MAX_COUNT = 5;

    private interface ConnectionCheckListener {
        enum Result {
            CHECK_SUCCESS,
            CHECK_FAILURE,
            CHECK_CANCELLED,
        }
        void onComplete(Result result);
    }

    // USB Accessory Configuration which is dynamically loaded from res/xml/accessory_filter.xml
    private class AccessoryConfig {
        private String mAccessoryManufacturer;
        private String mAccessoryModel;
        private String mAccessoryVersion;
    }

    private ArrayList<AccessoryConfig> mAccessoryConfig ;

    private Handler mHandler = new Handler(Looper.getMainLooper());
    private Context mContext;

    private SlipInterface mSLIP;
    private String mHUAddress;
    private StateListener mStateListener;
    private State mState;
    private boolean mPowerConnected;
    private boolean mUsbChattering;
    private long mLastPowerDisconnectedNsec = -1;
    private ConnectionListener mConnectionListener;

    private boolean mHuUsbState = false;
    private boolean mHuUsbBecomesAvailable = false;

    private ConnectionChecker mConnectionChecker;
    private int mConnectionCheckDurationMsec;
    private Runnable mConnectionCheckErrorCallback;
    private Runnable mWriteStuckCallback;

    // Boolean flag to determine continue timer running.
    private UsbAccessoryTimer mAccessoryTimer;

    private CableCheckTimer mCableCheckTimer = new CableCheckTimer();

    private RetryWithoutDisconnectTimer mErrorRetryTimer = new RetryWithoutDisconnectTimer(
            RETRY_AFTER_ERROR_INITIAL_DELAY_MSEC, RETRY_AFTER_ERROR_INTERVAL_MSEC, RETRY_AFTER_ERROR_MAX_COUNT);

    private DialogListener mDialogListener;

    // USB permission dialog will not be shown if this is false
    private boolean mPermissionDialogAllowed;

    /*
     * Workaround for USB permission dialog getting dismissed (VIPER-2267, HUAF-3988)
     * The USB permission dialog can be dismissed with some Activity transitions. The problem is that
     * Android framework does not distinguish "User explicitly denied for permission" and "the dialog
     * is just dismissed" so we cannot know whether the dialog is dismissed.
     *
     * As a workaround, we will request the dialog again if it is "likely" to be dismissed:
     *  - if we receive an Activity stop event and/or CAPP getting foreground notification right after
     *    requesting the dialog, we will request the dialog again some time later (workaround A)
     *  - if we receive an Activity stop event and/or CAPP getting foreground notification
     *    some time before or after "permission denied" notification, we will request the dialog
     *    again (workaround B)
     * Also, if we receive an Activity stop event and/or CAPP getting foreground notification before
     * showing the dialog, then we will defer showing it (workaround C).
     *
     * Note, these workarounds are not perfect. For example, we may get the dialog popping up again
     * if the user explicitly denied for the permission and immediately switch to another app.
     */
    private long mActivitySwitchedTimeNsec = -1;
    private long mPermissionRequestedTimeNsec = -1;
    private long mPermissionDeniedTimeNsec = -1;
    private UsbAccessory mPermissionRequestedAccessory;

    // There is no way to interrupt the call and stopping the thread except for physical USB connection.
    // So this class has related property as static so that these can be reused later.
    static private UsbAccessory mAccessory;
    static private ParcelFileDescriptor mAccessoryFD;

    private BroadcastReceiver mReceiver = new BroadcastReceiver() {
        private Runnable mPowerConnectHandler;
        private Runnable mDisconnectHandler;

        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            DebugTool.logInfo("UsbSlipDriver: Receive Broadcast: " + action.toString());
            UsbAccessory accessory = intent.getParcelableExtra(UsbManager.EXTRA_ACCESSORY);
            DebugTool.logInfo("UsbSlipDriver: Receive Broadcast: " + accessory);
            if (accessory != null) {

                if (ACTION_USB_PERMISSION.equals(action)) {
                    DebugTool.logInfo("USB accessory requested permission " + accessory.toString());
                    boolean granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false);
                    if (granted) {
                        if (mDialogListener != null) {
                            mDialogListener.onDialogResult(DialogListener.Result.PERMISSION_GRANTED);
                        }
                        /*
                            Check if USB cable connection for avoiding to obtain an old USB accessory

                            VIPER-1936
                         */

                        if(isUSBCableConnected()) {
                            DebugTool.logInfo("Permission granted for USB accessory");
                            setState(State.CONNECTING);
                            openAccessory(accessory);
                        } else {
                            DebugTool.logInfo("Permission granted, but cable is not connected");
                            disconnect(false);
                        }
                    } else {
                        DebugTool.logInfo("Permission denied for USB accessory");
                        mPermissionDeniedTimeNsec = System.nanoTime();
                        if (mDialogListener != null) {
                            mDialogListener.onDialogResult(DialogListener.Result.PERMISSION_DENIED_OR_DIALOG_DISMISSED);
                        }

                        if (mState == State.WAITING_PERMISSION &&
                                mActivitySwitchedTimeNsec >= 0 &&
                                mPermissionDeniedTimeNsec - mActivitySwitchedTimeNsec < DIALOG_MAY_BE_DISMISSED_NSEC) {
                            // workaround B
                            DebugTool.logInfo("Retrying USB permission dialog");
                            requestPermissionDialog(accessory, USB_PERMISSION_DIALOG_RETRY_DELAY_MSEC);
                        } else {
                            if (mState == State.CONNECTED) {
                                // This happens when two dialogs are shown by the "retry workaround" and
                                // user taps "OK" button on the first dialog, then "CANCEL" on the second one.
                                stopReading();
                            }
                            disconnect(true);
                        }
                    }
                }
            }
            /*
                ACTION_USB_ACCESSORY_DETACHED launches the monitor task to poll the USB Accessory lists.
                ACTION_POWER_DISCONNECTED launches the sequences performed by USB cable removal
                such as calling disconnect() and notifying it to the UMA app.

                The reason why we use the 2 Intents for USB cable removal is the notification
                ACTION_USB_ACCESSORY_DETACHED is sometimes delayed.
                If we use ACTION_POWER_DISCONNECTED to poll the accessory lists, we would get
                an old accessory, which will be unable to use immediately,
                when ACTION_USB_ACCESSORY_DETACHED notifies.

                VIPER-1649,VIPER-1772
            */
            if (UsbManager.ACTION_USB_ACCESSORY_DETACHED.equals(action)) {
                DebugTool.logInfo("USB accessory has been detached");
                if (mConnectionListener != null) {
                    mConnectionListener.onDetached();
                }

                if (mState == State.CONNECTED || mState == State.CONNECTING || mState == State.WAITING_PERMISSION) {
                    /*
                        In most cases, disconnect() is launched by ACTION_POWER_DISCONNECTED.
                        disconnect() called by ACTION_USB_ACCESSORY_DETACHED is an unusual case.
                        The case is, an old USB Accessory is obtained even though USB cable is
                        CONNECTED when start() is called.
                        In that case, we want to refresh the AccessoryList but we does not expect
                        ACTION_POWER_DISCONNECTED is notified. That is the why calling disconnect()
                        by ACTION_USB_ACCESSORY_DETACHED.

                        VIPER-1913
                     */
                    disconnect(false);
                }
                else {
                    /*
                        We have the issue USB Accessory Permission Dialog does not appear
                        even if Request Permission is invoked. The cause is considered
                        simultaneous starts of Android Activities (TCAPP and Permission Dialog)
                        when USB connects.
                        In that case, we can not do anything but waiting for detached event,
                        which means expecting a user interaction to disconnect and connect USB,

                        ...and changing internal state for a next open accessory.
                    */
                    mState = State.DISCONNECTED;
                }

                if (!SUPPORT_WIFI_TRANSITION) {
                    // Type-A: if the timer is already started (possibly because of POWER_DISCONNECTED) then do nothing
                    if (!(mAccessoryTimer != null && mAccessoryTimer.isRunning())) {
                        DebugTool.logInfo("Starting accessory timer with ACCESSORY_DETACHED");
                        mCableCheckTimer.stop();
                        // when starting accessory timer, stop retry timer
                        mErrorRetryTimer.stop();

                        mAccessoryTimer = new UsbAccessoryTimer().start();
                    }
                } else {
                    mCableCheckTimer.stop();
                    // when starting accessory timer, stop retry timer
                    mErrorRetryTimer.stop();

                    if (mAccessoryTimer != null) {
                        mAccessoryTimer.stop();
                    }
                    mAccessoryTimer = new UsbAccessoryTimer().start();
                }
            }
            else if (Intent.ACTION_POWER_CONNECTED.equals(action)) {
                mUsbChattering = true;
                cancelDisconnectHandler();
                mPowerConnectHandler = new Runnable() {
                    @Override
                    public void run() {
                        mPowerConnected = true;
                        mUsbChattering = false;
                        mPowerConnectHandler = null;
                    }
                };
                mHandler.postDelayed(mPowerConnectHandler, 500);
            }
            else if (Intent.ACTION_POWER_DISCONNECTED.equals(action)) {
                DebugTool.logInfo("UsbSlipDriver: ACTION_POWER_DISCONNECTED; state = " + mState.toString());
                mUsbChattering = true;
                /*
                    Disregarding ACTION_POWER_DISCONNECTED except CONNECTED state.

                    ACTION_POWER_CONNECTED/DISCONNECTED is often chattered.
                    From the perspective of actual user cases, the event is unreliable except the case.
                 */
                /**
                 *  When USB is connected on some devices (e.g. Nexus6), the following action sequence will happen:
                 *  ACTION_POWER_CONNECTED, followed by
                 *  ACTION_POWER_DISCONNECTED, followed by
                 *  ACTION_POWER_CONNECTED
                 *  These three actions will be broadcasted very quickly (within 500 msec or so).
                 *  We need to defer actual disconnect somewhat (500 msec), and make sure USB cable is actually disconnected.
                 */
                cancelPowerConnectHandler();
                cancelDisconnectHandler();
                mDisconnectHandler = new Runnable() {
                    @Override
                    public void run() {
                        mLastPowerDisconnectedNsec = System.nanoTime();
                        mPowerConnected = false;
                        mUsbChattering = false;
                        if (mState == State.CONNECTED || mState == State.CONNECTING || mState == State.WAITING_PERMISSION) {
                            DebugTool.logInfo("UsbSlipDriver: actually disconnect by ACTION_POWER_DISCONNECTED; state = " + mState.toString());
                            if (mConnectionListener != null) {
                                mConnectionListener.onDisconnected();
                            }
                            disconnect(false);
                        }
                        mDisconnectHandler = null;

                        // start accessory timer if it is not started yet (Type-A only)
                        if (!SUPPORT_WIFI_TRANSITION) {
                            if (!(mAccessoryTimer != null && mAccessoryTimer.isRunning())) {
                                DebugTool.logInfo("Starting accessory timer with POWER_DISCONNECTED");
                                mCableCheckTimer.stop();
                                // when starting accessory timer, stop retry timer
                                mErrorRetryTimer.stop();

                                mAccessoryTimer = new UsbAccessoryTimer().start();
                            }
                        }
                    }
                };
                mHandler.postDelayed(mDisconnectHandler, 500);
            }

            // Notification from TCAPP/LCAPP
            if (ACTION_CAPP_FOREGROUND_NOTIFICATION.equals(action)) {
                DebugTool.logInfo("Detect CAPP foreground event");
                retryPermissionDialogIfNecessary();
            }
        }

        private void cancelPowerConnectHandler() {
            if (mPowerConnectHandler != null) {
                mHandler.removeCallbacks(mPowerConnectHandler);
                mPowerConnectHandler = null;
            }
        }

        private void cancelDisconnectHandler() {
            if (mDisconnectHandler != null) {
                //DebugTool.logInfo("UsbSlipDriver: canceling mDisconnectHandler");
                mHandler.removeCallbacks(mDisconnectHandler);
                mDisconnectHandler = null;
            }
        }
    };

    /**
     * Initialize SLIP driver for USB, an application must call {@link #init(android.content.Context)}
     * before call {@link #getInstance()}.
     *
     * @param context
     */
    public static UsbSlipDriver init(Context context) {
        synchronized (UsbSlipDriver.class) {
            if (sInstance == null)
                sInstance = new UsbSlipDriver(context);
        }
        return sInstance;
    }

    /**
     * Obtain singleton instance of the driver.
     *
     * @return
     */
    public static UsbSlipDriver getInstance() {
        if (sInstance == null)
            throw new RuntimeException("Must call init before call getInstance");

        return sInstance;
    }

    @Override
    public void finalize() throws Throwable {
        stop();
        super.finalize();
    }

    // ----------------------------------------------------------------------------------
    // ISlipService
    // ----------------------------------------------------------------------------------

    /** {@inheritDoc} */
    @Override
    public State getState() {
        return mState;
    }

    /** {@inheritDoc} */
    public boolean isConnected() {
        return mState == State.CONNECTED;
    }

    /** {@inheritDoc} */
    @Override
    public boolean start() {
        switch (mState) {
            case IDLE: {
                try {
                    DebugTool.logInfo("Starting USB driver");

                    // Retrieve USB accessory settings
                    if (!loadAccessorySettings(mContext)) {
                        DebugTool.logError("cannot load USB accessory settings");
                        return false;
                    }

                    // Register necessary intent filters
                    registerReceiver();
                    setState(State.LISTENING);

                    // Look for connected accessories and connect it if compatible is found.
                    // This is done in CableCheckTimer.
                    mCableCheckTimer.start();
                    return true;
                } catch (Exception e) {
                    DebugTool.logError(e.toString());
                    return false;
                }
            }
            default:
                DebugTool.logWarning("Invalid state to open USB transport.");
                break;
        }
        return false;
    }

    /** {@inheritDoc} */
    @Override
    public boolean stop() {
        DebugTool.logInfo("UsbSlipDriver stop: current state=" + mState);
        synchronized (this) {
            switch (mState) {
                case LISTENING:
                case WAITING_PERMISSION:
                case CONNECTING:
                case CONNECTED:
                case DISCONNECTED: { // we need to cleanup even if Usb is DISCONNECTED state.
                    DebugTool.logInfo("Stopping USB driver");
                    setState(State.STOPPING);
                    try {
                        mContext.unregisterReceiver(mReceiver);
                    } catch (IllegalArgumentException e) {
                        DebugTool.logWarning("Failed to unregister receiver: " + e.toString());
                    }
                    disconnect(true);
                    mUsbChattering = false; // in case mReceiver is unregistered while chattering

                    // Make sure stop the timer
                    if (mAccessoryTimer != null) {
                        mAccessoryTimer.stop();
                        mAccessoryTimer = null;
                    }
                    mErrorRetryTimer.stop();

                    // Return to IDLE state.
                    setState(State.IDLE);

                    mActivitySwitchedTimeNsec = -1;
                    mPermissionRequestedTimeNsec = -1;
                    mPermissionDeniedTimeNsec = -1;
                    mPermissionRequestedAccessory = null;
                    mHuUsbState = false;
                    mHuUsbBecomesAvailable = false;
                    break;
                }
                default:
                    break;
            }
            return true;
        }
    }

    /** {@inheritDoc} **/
    @Override
    public void setStateListener(StateListener listener) {
        synchronized (this) {
            mStateListener = listener;
        }
    }

    /**
     * Call this method with allowed=true once it is ready to show USB permission dialog.
     *
     * This method has effect only if uie.multiaccess.usb.PREVENT_PERM_DIALOG_UNTIL_PROJECTION is
     * configured to "true".
     *
     * @param allowed set to true when USB permission dialog is ready to show.
     */
    public void setUsbPermissionDialogAllowed(boolean allowed) {
        //if (PolicyManager.getInstance().getBooleanValue(PolicyManager.USB_PREVENT_PERM_DIALOG_UNTIL_PROJECTION, false)) {
            if (mPermissionDialogAllowed != allowed) {
                DebugTool.logInfo("USB permission dialog condition changed to " + allowed);
                mPermissionDialogAllowed = allowed;
            }
        //}
    }

    /**
     * Check connected accessory and establish connection to an accessory.
     * Usually this method is used to make sure reconnect USB when it's attached.
     *
     * @return  true if connected, otherwise false
     */
    public boolean connect() {
        if (mState == State.LISTENING || mState == State.DISCONNECTED) {
            UsbManager usbManager = getUsbManager();
            UsbAccessory[] accessories = usbManager.getAccessoryList();
            if (accessories != null) {
                for (UsbAccessory accessory : accessories) {
                    if (isAccessorySupported(accessory)) {
                        DebugTool.logInfo("Connect to USB accessory: " + accessory);
                        if (connectToAccessory(accessory)) {
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    public void stopReading() {
        if (mSLIP != null) {
            mSLIP.stopReading();
        }
    }

    public void setConnectionListener(ConnectionListener listener) {
        mConnectionListener = listener;
    }

    public void setDialogListener(DialogListener listener) {
        mDialogListener = listener;
    }

    public void setConnectionCheck(int checkDurationMsec, Runnable onError) {
        mConnectionCheckDurationMsec = checkDurationMsec;
        mConnectionCheckErrorCallback = onError;
    }

    public void setWriteTimeoutCallback(Runnable callback) {
        mWriteStuckCallback = callback;
    }

    public void notifyActivityEvents(ActivityEvent ev) {
        switch (ev) {
            case STOPPED:
                DebugTool.logInfo("Activity stop notified");
                retryPermissionDialogIfNecessary();
                break;
            default:
                break;
        }
    }

    public void setHUUsbState(boolean state) {
        // for type-A only
        if (!SUPPORT_WIFI_TRANSITION) {
            if (state) {
                if (mHuUsbState != state) {
                    // edge-trigger event (DISCONNECTED -> CONNECTED)
                    mHuUsbBecomesAvailable = true;

                    if (hasUsbPermission()) {
                        mCableCheckTimer.stop();
                        if (mAccessoryTimer != null) {
                            mAccessoryTimer.stop();
                            mAccessoryTimer = null;
                        }
                        connect();
                    }
                }
            } else {
                mHuUsbBecomesAvailable = false;

                /*
                 * When HU notifies USB DISCONNECT event, stop USB even if state is CONNECTING or CONNECTED.
                 *
                 * We have two cases for such situation:
                 * (1) Local event of POWER_DISCONNECTED or ACCESSORY_DETACHED is delayed. In this case,
                 *     the cable is already disconnected, so there is no problem to stop USB.
                 * (2) HU disabled its USB although the cable is still connected. This is the case that
                 *     HU has two USB ports (using "USB I/F Box") and an iPhone is connected while an
                 *     Android phone is running VPM. In such case, HU disables its USB so write() will
                 *     block on HS side. Since VPM becomes unavailable, we should switch to RM.
                 *     Note: to recover from RM to VPM, we need to disconnect the cable and reconnect.
                 * In either case, we do not run the workaround with ICMP echo since USB communication
                 * is likely unavailable.
                 */
                disconnect(false);
            }
            mHuUsbState = state;
        }
    }

    // ----------------------------------------------------------------------------------
    // private stuff
    // ----------------------------------------------------------------------------------

    private UsbSlipDriver(Context context) {
        mContext = context;
        mState   = State.IDLE;

        //if (PolicyManager.getInstance().getBooleanValue(PolicyManager.USB_PREVENT_PERM_DIALOG_UNTIL_PROJECTION, false)) {
        //    mPermissionDialogAllowed = false;
        //} else {
            mPermissionDialogAllowed = true;
        //}
    }

    /**
     * Load USB Accessory settings from res/xml/accessory_filter.xml.
     * These settings can be customized with build flavor.
     *
     * @param context An application context.
     * @return true if successfully loaded, otherwise false
     */
    private boolean loadAccessorySettings(Context context) {
        try {
            mAccessoryConfig = new ArrayList<> ();
            XmlResourceParser parser = context.getResources().getXml(R.xml.accessory_filter);
            int eventType = parser.getEventType();
            while (eventType != XmlPullParser.END_DOCUMENT) {
                if (eventType == XmlPullParser.START_TAG) {
                    String tag = parser.getName();
                    if (tag.equals("usb-accessory")) {
                        AccessoryConfig config = new AccessoryConfig();
                        config.mAccessoryModel = parser.getAttributeValue(null, "model");
                        config.mAccessoryManufacturer  = parser.getAttributeValue(null, "manufacturer");
                        config.mAccessoryVersion  = parser.getAttributeValue(null, "version");

                        DebugTool.logInfo("USB Accessory settings has been loaded" +
                                config.mAccessoryModel + ", " + config.mAccessoryManufacturer + ", " + config.mAccessoryVersion);
                        mAccessoryConfig.add(config);
                    }
                }
                eventType = parser.next();
            }
            parser.close();
            return true;
        } catch (Exception e) {
            DebugTool.logError(e.toString());
        }
        return false;
    }

    private void registerReceiver() {
        // Note that Android OS does not notify ACTION_USB_ACCESSORY_ATTACHED to BroadcastReceiver.
        // Only Activity can receive that intent. Instead, UMA expects that upper layer calls connect()
        // method making sure establish USB session.
        IntentFilter filter = new IntentFilter();
        filter.addAction(UsbManager.ACTION_USB_ACCESSORY_DETACHED);
        filter.addAction(Intent.ACTION_POWER_CONNECTED);
        filter.addAction(Intent.ACTION_POWER_DISCONNECTED);
        filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
        filter.addAction(ACTION_USB_PERMISSION);
        filter.addAction(ACTION_CAPP_FOREGROUND_NOTIFICATION);
        mContext.registerReceiver(mReceiver, filter);
    }

    private void setState(final State state) {
        setState(state, false);
    }

    private void setState(final State state, boolean forceNotify) {
        if (mState != state || forceNotify) {
            DebugTool.logInfo("USB driver state changed to " + state + " from " + mState);
            mState = state;
            final State s = state;
            mHandler.post(new Runnable() {
                @Override
                public void run() {
                    synchronized (UsbSlipDriver.this) {
                        if (mStateListener != null) {
                            mStateListener.onStateChanged(s);
                        }
                        // also send broadcast
                        if (mState == State.CONNECTED) {
                            DebugTool.logInfo("about sending AOA_ROUTER_OPEN_ACCESSORY broadcast");
                            Intent intent = new Intent(TransportConstants.AOA_OPEN_ACCESSORY);
                            mContext.sendBroadcast(intent);
                        }
                    }
                }
            });
        }
    }

    private UsbManager getUsbManager() {
        return (UsbManager)mContext.getSystemService(Context.USB_SERVICE);
    }

    private void openAccessory(UsbAccessory accessory) {
        switch (mState) {
            case LISTENING:
            case CONNECTING: {
                synchronized (this) {
                    DebugTool.logInfo("Open USB accessory: " + accessory);
                    // UsbAccessory instance can exist until USB disconnected.
                    if (!accessory.equals(mAccessory)) {
                        ParcelFileDescriptor accessoryFD = null;
                        try {
                            accessoryFD = getUsbManager().openAccessory(accessory);
                        } catch (SecurityException e) {
                            // this exception happens when we receive an intent sent by another UMA app
                            // stay in same state and wait for another intent
                            DebugTool.logWarning("Received intent from another app");
                            return;
                        }
                        if (accessoryFD == null) {
                            DebugTool.logError("Cannot open USB accessory");
                            // change the state to DISCONNECTED
                            disconnect(false);
                            return;
                        }
                        mAccessory = accessory;
                        mAccessoryFD = accessoryFD;
                    }

                    // Instantiate SLIP network interface
                    // Next read network settings.
                    //PolicyManager policyManager = PolicyManager.getInstance();
                    String addr = "192.168.3.1";//policyManager.getStringValue(PolicyManager.USB_IP_ADDRESS, null);
                    String mask = "255.255.255.0";//policyManager.getStringValue(PolicyManager.USB_NETMASK, null);
                    mHUAddress = "192.168.3.2";//policyManager.getStringValue(PolicyManager.USB_HU_IP_ADDRESS, null);
                    if (addr == null || mask == null) {
                        DebugTool.logError("Cannot load network settings for USB.");
                        return;
                    }

                    try {
                        // Considering that avoid overhead of frequent calling Java and JNI function,
                        // UsbSlipDriver handles it on native layer instead of handling I/O on Java.
                        // So it only just has to set file descriptor to point USB channel.
                        InetAddress address = InetAddress.getByName(addr);
                        InetAddress netmask = InetAddress.getByName(mask);
                        DebugTool.logInfo("USB SLIP (" + address + ", " + netmask + ")");
                        mSLIP = new SlipInterface(address, netmask, mAccessoryFD.getFileDescriptor());
                        if (!mSLIP.attach()) {
                            DebugTool.logError("Failed to attach SLIP I/F");
                            return;
                        }
                        mSLIP.setErrorListener(new SlipInterface.OnErrorListener() {
                            @Override
                            public void onSlipError(int error) {
                                if (error == SlipInterface.ERROR_USB_WRITE) {
                                    DebugTool.logInfo("SLIP error happened, call disconnect()");
                                    disconnect(false);

                                    // HUAF-6052: try re-initializing USB SLIP transport after error.
                                    // If we already received DETACHED event and the normal timer for
                                    // polling is already running, don't start retry timer.
                                    if (mAccessoryTimer == null && !mErrorRetryTimer.isRunning()) {
                                        mErrorRetryTimer.start();
                                    }
                                } else if (error == SlipInterface.ERROR_USB_STUCK) {
                                    DebugTool.logWarning("USB write timed out, call disconnect()");
                                    disconnect(false);
                                    if (mWriteStuckCallback != null) {
                                        mHandler.post(mWriteStuckCallback);
                                    }
                                }
                            }
                        });
                        UsbRateLimitHelper.getInstance().setUsbSlipInterface(mSLIP);

                        // Make sure stop the timer to check USB accessory connection status
                        if (mAccessoryTimer != null) {
                            mAccessoryTimer.stop();
                            mAccessoryTimer = null;
                        }

                        if (mConnectionCheckDurationMsec > 0) {
                            startConnectionCheck();
                        } else {
                            // don't run connection checking
                            setState(State.CONNECTED);
                        }
                    } catch (UnknownHostException e) {
                        DebugTool.logError(e.toString());
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    private void startConnectionCheck() {
        if (mConnectionChecker != null) {
            DebugTool.logWarning("USB connection check already running");
            return;
        }

        // Ignore write errors while connection checking, for HU native may not set up
        // USB yet and we may get some errors when sending ICMP echo requests.
        // Keep sending until we get a reply from HU.
        mSLIP.setStopOnUsbWriteError(false);

        final State currentState = mState;
        mConnectionChecker = new ConnectionChecker(mHUAddress, PING_INTERVAL_MSEC,
                0, mConnectionCheckDurationMsec, new ConnectionCheckListener() {
            @Override
            public void onComplete(Result result) {
                if (mSLIP != null) {
                    mSLIP.setStopOnUsbWriteError(true);
                }
                switch (result) {
                    case CHECK_SUCCESS:
                        if (mState == currentState) {
                            DebugTool.logInfo("USB connection check succeeded.");
                            mErrorRetryTimer.stop();
                            setState(State.CONNECTED);
                        }
                        break;

                    case CHECK_FAILURE:
                        DebugTool.logInfo("USB connection check failed after attach.");
                        if (mConnectionCheckErrorCallback != null) {
                            mHandler.post(mConnectionCheckErrorCallback);
                        }
                        disconnect(false);
                        break;

                    case CHECK_CANCELLED:
                        // same as failure case except that we don't call error callback
                        DebugTool.logInfo("USB connection check cancelled.");
                        disconnect(false);
                        break;

                    default:
                        break;
                }
                mConnectionChecker = null;
            }
        });
        DebugTool.logInfo("Running USB connection check (" + mConnectionCheckDurationMsec + " msec)");
        mConnectionChecker.start();
    }

    private void disconnect(boolean runWorkaround) {
        mCableCheckTimer.stop();
        if (mConnectionChecker != null) {
            mConnectionChecker.stop();
            mConnectionChecker = null;
        }

        if (mState == State.IDLE) {
            // VIPER-2558: prevent the state goes back to DISCONNECTED
            return;
        }

        /*
         * Workaround of Android's USB AOA issue: https://code.google.com/p/android/issues/detail?id=20545
         *
         * Wait for some time until read() operation on USB file descriptor is finished. If we close the file descriptor
         * while someone is read()-ing on it, we cannot open USB AOA again without disconnecting & reconnecting USB cable.
         * Please note that for this we need HU to send some data to HS. We need to ask HU to include implementation of
         * sending data periodically (such as SLIP's END byte code), or we need HS to send out some request packet and
         * get response from HU (such as ICMP PING).
         */
        PingSender pingSender = null;

        if (runWorkaround && mSLIP != null && mHUAddress != null) {
            // don't notify SLIP errors any more
            mSLIP.setErrorListener(null);

            try {
                pingSender = new PingSender(mHUAddress, PING_INTERVAL_MSEC, PING_DATA_SIZE, null);
            } catch (IllegalArgumentException e) {
                DebugTool.logError(e.toString());
            }

            if (pingSender != null) {
                pingSender.start();
            }

            long timeoutMsec = USB_READ_STOP_TIMEOUT_MSEC;
            long intervalMsec = 100;
            while (!mSLIP.readStopped() && timeoutMsec > 0) {
                try {
                    Thread.sleep(intervalMsec, 0);
                    timeoutMsec -= intervalMsec;
                } catch (InterruptedException e) {
                    // do nothing
                }
            }
            if (mSLIP.readStopped()) {
                DebugTool.logInfo("read stopped");
            } else {
                // The workaround using ICMP does not work if HU is powered off, or cable is
                // disconnected.
                DebugTool.logWarning("Could not stop reading from USB AOA connection. " +
                             "VPM may not start next time unless USB cable is reconnected.");
            }

            if (pingSender != null) {
                // PingSender.stop() may fail if HU does not initialize AOA connection and
                // tcpip_thread gets blocked by write() in SlipInterface::slipOutput(). In this case,
                // force close AOA connection with mAccessoryFD.close() and try again.
                boolean success = pingSender.stop(PINGSENDER_STOP_TIMEOUT_MSEC);
                if (success) {
                    pingSender = null;
                }
            }
        }

        // Close connection to the connected accessory
        synchronized (this) {
            if (mAccessory != null) {
                if (mAccessoryFD != null) {
                    try {
                        mAccessoryFD.close();
                    } catch (IOException e) {
                        DebugTool.logInfo(e.toString());
                        mAccessoryFD = null;
                    }
                }
                mAccessory = null;
            }
            if (mSLIP != null) {
                mSLIP.detach();
                // set null after native thread is terminated by detach()
                UsbRateLimitHelper.getInstance().setUsbSlipInterface(null);
                mSLIP = null;
            }
        }

        if (pingSender != null) {
            pingSender.stop(PINGSENDER_STOP_TIMEOUT_MSEC);
            pingSender = null;
        }

        // Ensure that unregister receiver
        setState(State.DISCONNECTED);
    }

    /**
     * Return whether accessory is compatible with an application or not.
     *
     * @param accessory USB accessory to be checked.
     * @return  true if it's compatible, otherwise false
     */
    private boolean isAccessorySupported(UsbAccessory accessory) {
        if(mAccessoryConfig != null) {
            for (AccessoryConfig config : mAccessoryConfig) {
                if (config.mAccessoryManufacturer != null && config.mAccessoryModel != null && config.mAccessoryVersion != null) {
                    if (config.mAccessoryManufacturer.equals(accessory.getManufacturer()) &&
                            config.mAccessoryModel.equals(accessory.getModel()) &&
                            config.mAccessoryVersion.equals(accessory.getVersion())) {
                        return true;
                    }
                } else {
                    return false;
                }
            }
        }
        return false;
    }

    // returns true if connection to the accessory is initiated
    private boolean connectToAccessory(UsbAccessory accessory) {
        switch (mState) {
            case LISTENING:
            case DISCONNECTED: {
                // ETNG-4366: Pioneer T0 and Panasonic T1 HUs initialize their USB layer only after
                // Bluetooth connection is established between HU and HS. For a workaround, we wait
                // until Bluetooth is connected (Type-A only)
                /*
                if (!SUPPORT_WIFI_TRANSITION && !SppSlipDriver.getInstance().isConnected()) {
                    DebugTool.logInfo("Bluetooth is not connected yet, stop opening USB accessory");
                    return false;
                }
                */
                /*
                 * VIPER-2724: wait for HU's USB CONNECTED event before opening AOA
                 * We cannot use mHuUsbState here. We need to use edge-trigger event instead.
                 * Consider a case where the user pulls out the USB cable then reconnect it quickly.
                 * Updating mHuUsbState may be delayed and may stay in true (CONNECTED) state. If
                 * Android framework detects HU and connectToAccessory() is called, we may refer to
                 * the old state and try to open AOA, although HU is not actually ready yet!
                 * To avoid such timing issue, we wait for mHuUsbState value to be false (DISCONNECTED)
                 * and then true again. This makes sure we are not referring to the old CONNECTED value.
                 */
                /*
                if (!SUPPORT_WIFI_TRANSITION) {
                    if (!mHuUsbBecomesAvailable) {
                        DebugTool.logInfo("Wait for HU USB state before opening USB accessory");
                        return false;
                    } else {
                        mHuUsbBecomesAvailable = false;
                    }
                }
                */
                final UsbManager usbManager = getUsbManager();
                if (usbManager.hasPermission(accessory)) {
                    DebugTool.logInfo("Already has permission to use USB accessory");
                    setState(State.CONNECTING);
                    openAccessory(accessory);
                } else {
                    if (mPermissionDialogAllowed) {
                        // Prevent the case opening USB accessory permission dialog twice.
                        setState(State.WAITING_PERMISSION);

                        /*
                         * Delay showing the USB permission dialog.
                         * This is a workaround of TCAPP's accessory launch. When USB cable is connected,
                         * TCAPP will be automatically launched and there will be some startActivity() calls for
                         * activity transition. If the permission dialog is shown during such time, it will be
                         * dismissed and user cannot access to the dialog.
                         * As a simple workaround, we delay the permission dialog to avoid such activity transitions.
                         */
                        requestPermissionDialog(accessory, USB_PERMISSION_DIALOG_DELAY_MSEC);
                    } else {
                        // It is not a good time to show the dialog. Stay in the original state and do nothing.
                        return false;
                    }
                }
                return true;
            }
            default:
                DebugTool.logWarning("connectToAccessory() called on invalid state:" + mState);
                return false;
        }
    }

    private void requestPermissionDialog(final UsbAccessory accessory, long delayMsec) {
        mHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                long currentTimeNsec = System.nanoTime();
                if (mActivitySwitchedTimeNsec >= 0 &&
                        currentTimeNsec - mActivitySwitchedTimeNsec < DIALOG_MAY_BE_DISMISSED_NSEC) {
                    // workaround C
                    DebugTool.logInfo("Request permission to use USB accessory after " +
                            USB_PERMISSION_DIALOG_EXTRA_DELAY_MSEC + " msec");
                    requestPermissionDialog(accessory, USB_PERMISSION_DIALOG_EXTRA_DELAY_MSEC);
                    return;
                }

                // Note: this check is not perfect. It is possible that the second dialog is requested
                // and then the user taps "OK" button on the first dialog.
                if (!getUsbManager().hasPermission(accessory)) {
                    if (mState == State.WAITING_PERMISSION || mState == State.CONNECTING) {
                        // Request permission to use accessory
                        DebugTool.logInfo("Request permission to use USB accessory");
                        Runnable showDialog = new Runnable() {
                            @Override
                            public void run() {
                                PendingIntent permissionIntent = PendingIntent.getBroadcast(
                                        mContext, 0, new Intent(ACTION_USB_PERMISSION), 0);
                                getUsbManager().requestPermission(accessory, permissionIntent);
                                mPermissionRequestedTimeNsec = System.nanoTime();
                                mPermissionRequestedAccessory = accessory;
                            }
                        };

                        if (mDialogListener != null) {
                            mDialogListener.requestShowDialog(showDialog);
                        } else {
                            showDialog.run();
                        }
                    } else {
                        DebugTool.logInfo("USB already disconnected: " + mState);
                    }
                } else {
                    DebugTool.logInfo("Already has permission with previous dialog, cancel showing another one");
                    // This case most likely caused by wrong state. Let's get back to LISTENNING
                    ensureConnectionState(accessory);
                }
            }
        }, delayMsec);
    }

    private void ensureConnectionState(final UsbAccessory accessory) {
        mHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (getState() != State.CONNECTED && !getUsbManager().hasPermission(accessory)) {
                    // we will restart from LISTENING
                    setState(State.LISTENING);
                    connectToAccessory(accessory);
                }
            }
        }, 1000);
    }
    // Right now, "synchronized" is not necessary since this method is called from main thread only.
    private synchronized void retryPermissionDialogIfNecessary() {
        mActivitySwitchedTimeNsec = System.nanoTime();

        if ((mState == State.WAITING_PERMISSION || mState == State.CONNECTING) && mPermissionRequestedAccessory != null) {
            // workaround A
            if (mPermissionRequestedTimeNsec >= 0 &&
                    mActivitySwitchedTimeNsec - mPermissionRequestedTimeNsec < DIALOG_MAY_BE_DISMISSED_NSEC) {
                DebugTool.logInfo("Retrying permission dialog (activity switch after request)");
                requestPermissionDialog(mPermissionRequestedAccessory, USB_PERMISSION_DIALOG_RETRY_DELAY_MSEC);
            }

            // workaround B
            if (mPermissionDeniedTimeNsec >= 0 &&
                    mActivitySwitchedTimeNsec - mPermissionDeniedTimeNsec < DIALOG_MAY_BE_DISMISSED_NSEC) {
                DebugTool.logInfo("Retrying permission dialog (activity switch after denied)");
                requestPermissionDialog(mPermissionRequestedAccessory, USB_PERMISSION_DIALOG_RETRY_DELAY_MSEC);
            }

            // clear the value so that we will retry only once
            mPermissionRequestedAccessory = null;
        } else if (mState == State.WAITING_PERMISSION && mPermissionRequestedAccessory == null) {
            // In this case, permission dialog may be accepted, but we anyhow do not open accessory yet.
            mState = State.LISTENING;
            UsbManager usbManager = getUsbManager();
            UsbAccessory[] accessories = usbManager.getAccessoryList();
            if (accessories != null) {
                for (UsbAccessory accessory : accessories) {
                    if (isAccessorySupported(accessory)) {
                        connectToAccessory(accessory);
                    }
                }
            }
        }

    }

    private boolean isUSBCableConnected() {
        IntentFilter filter = new IntentFilter(Intent.ACTION_BATTERY_CHANGED);
        Intent batteryChangedIntent = mContext.registerReceiver(null, filter);
        int batteryStatus = batteryChangedIntent.getIntExtra(BatteryManager.EXTRA_STATUS, -1);
        return batteryStatus != BatteryManager.BATTERY_STATUS_DISCHARGING &&
                batteryStatus != BatteryManager.BATTERY_STATUS_NOT_CHARGING;
    }

    private boolean hasUsbPermission() {
        UsbManager usbManager = getUsbManager();
        UsbAccessory[] accessories = usbManager.getAccessoryList();
        for (UsbAccessory accessory : accessories) {
            if (isAccessorySupported(accessory)) {
                return usbManager.hasPermission(accessory);
            }
        }
        return false;
    }

    // Note that Android OS only notify ACTION_USB_ACCESSORY_ATTACHED to Activity.
    // As a result, Service cannot receive that intent. So our approach is to schedule timer to check
    // USB connection status.
    private class UsbAccessoryTimer {
        private boolean mRunning;

        public UsbAccessoryTimer start() {
            DebugTool.logInfo("Start USB connection timer");
            mRunning = true;
            mHandler.postDelayed(new Runnable() {
                @Override
                public void run() {
                    if (!mUsbChattering && isUSBCableConnected() && connect()) {
                        stop();
                    }
                    if (mRunning) {
                        mHandler.postDelayed(this, 1000);
                    }
                }
            }, 1000);

            return this;
        }

        public UsbAccessoryTimer stop() {
            DebugTool.logInfo("Stop USB connection timer");
            mRunning = false;
            return this;
        }

        public boolean isRunning() {
            return mRunning;
        }
    }

    // This timer is started when UsbSlipDriver.start() is called. It will continue running until
    // isUSBCableConnected() returns true or we reach to the max. iteration.
    // By design, UsbAccessoryTimer and this timer should not run at the same time.
    // HUAF-6162
    private class CableCheckTimer implements Runnable {
        private boolean mRunning;
        private int mCount;

        // It is expected that start() and stop() are called by the main thread.
        public void start() {
            if (!mRunning) {
                mRunning = true;
                mCount = 0;
                mHandler.post(this);
            }
        }

        public void stop() {
            if (mRunning) {
                DebugTool.logInfo("Stop USB cable checking timer");
                stopInternal();
            }
        }

        @Override
        public void run() {
            if (mState != State.LISTENING) {
                DebugTool.logInfo("Cable checking timer stopped (state=" + mState + ")");
                stopInternal();
                return;
            }

            UsbManager usbManager = getUsbManager();
            UsbAccessory[] accessories = usbManager.getAccessoryList();
            if (accessories != null) {
                for (UsbAccessory accessory : accessories) {
                    if (isAccessorySupported(accessory)) {
                        /*
                            If a USB Accessory is obtained even though the USB cable is
                            disconnected, the accessory is assumed as old one,
                            which is derived from previous connection.
                            Thus the accessory is ignored and Accessory retriever is
                            launched by the next DETACHED event.

                            VIPER-1913
                        */
                        if(isUSBCableConnected()) {
                            DebugTool.logInfo("Connect to USB accessory: " + accessory);
                            if (connectToAccessory(accessory)) {
                                stopInternal();
                                return;
                            }
                        } else {
                            if (mCount < CABLE_CHECK_AFTER_START_RETRY) {
                                if (mCount == 0) {
                                    DebugTool.logInfo("Cable checking timer started");
                                }
                                mCount++;
                                mHandler.postDelayed(this, CABLE_CHECK_INTERVAL_MSEC);
                                return;
                            } else {
                                DebugTool.logInfo("Cable checking timer stopped (timeout)");
                                stopInternal();
                                return;
                            }
                        }
                    }
                }
            }

            // If there was no compatible accessory connected on the phone
            // schedule timer to check USB accessory state.
            if (mCount > 0) {
                DebugTool.logInfo("Cable checking timer stopped, switching to accessory timer");
            }
            stopInternal();
            mAccessoryTimer = new UsbAccessoryTimer().start();
        }

        private void stopInternal() {
            mHandler.removeCallbacks(this);
            mRunning = false;
        }
    }

    // Check whether USB AOA connection is working using ICMP echo/reply.
    // It is expected that start() and stop() are called on the main thread.
    private class ConnectionChecker implements Runnable {
        private PingSender mPingSender;
        private long mStartTimeNsec;
        private int mIntervalMsec;
        private int mStartDelayMsec;
        private int mTimeoutMsec;
        private boolean mStarted = false;
        private ConnectionCheckListener.Result mResult = ConnectionCheckListener.Result.CHECK_CANCELLED;
        private ConnectionCheckListener mListener;

        public ConnectionChecker(String huAddress, int intervalMsec, int startDelayMsec, int timeoutMsec,
                                 ConnectionCheckListener listener) {
            mIntervalMsec = intervalMsec;
            mStartDelayMsec = startDelayMsec;
            mTimeoutMsec = timeoutMsec;
            mPingSender = new PingSender(huAddress, intervalMsec, PING_DATA_SIZE, new PingSender.PingSenderListener() {
                @Override
                public void onReply() {
                    mResult = ConnectionCheckListener.Result.CHECK_SUCCESS;
                    mHandler.post(new Runnable() {
                        @Override
                        public void run() {
                            stop();
                        }
                    });
                }

                @Override
                public void onTimeout() {
                    long elapsedMsec = (System.nanoTime() - mStartTimeNsec) / 1000000;
                    if (elapsedMsec > mTimeoutMsec) {
                        mResult = ConnectionCheckListener.Result.CHECK_FAILURE;
                        mHandler.post(new Runnable() {
                            @Override
                            public void run() {
                                stop();
                            }
                        });
                    }
                }
            });
            mListener = listener;
        }

        public void start() {
            mHandler.postDelayed(this, mStartDelayMsec);
        }

        public void stop() {
            mHandler.removeCallbacks(this);

            // if stop() is called more than once, do nothing
            if (mPingSender == null) {
                return;
            }

            // if stop() is called during startDelay, this means checking is cancelled.
            if (!mStarted) {
                mResult = ConnectionCheckListener.Result.CHECK_CANCELLED;
            }

            mHandler.post(new Runnable() {
                @Override
                public void run() {
                    if (mListener != null) {
                        mListener.onComplete(mResult);
                    }
                }
            });
            mPingSender.stop(PINGSENDER_STOP_TIMEOUT_MSEC);
            mPingSender = null;
        }

        @Override
        public void run() {
            if (!mStarted) {
                // first time
                mStarted = true;

                if (mState != State.CONNECTING) {
                    // possibly cable is removed, cancel checking
                    mResult = ConnectionCheckListener.Result.CHECK_CANCELLED;
                    stop();
                    return;
                }

                mStartTimeNsec = System.nanoTime();
                mPingSender.start();

                return;
            }
        }
    }

    // HUAF-6052: if we see SLIP error but USB cable seems connected, try re-initializing the SLIP
    // transport again. This is for a short-time disconnection caused by Android 6's "USB chooser" dialog.
    private class RetryWithoutDisconnectTimer implements Runnable {
        private int mInitialDelayMsec;
        private int mIntervalMsec;
        private int mMaxCount;
        private long mStartTimeNsec;
        private int mCount;
        private boolean mIsRunning = false;

        public RetryWithoutDisconnectTimer(int initialDelayMsec, int intervalMsec, int maxCount) {
            mInitialDelayMsec = initialDelayMsec > 0 ? initialDelayMsec : 0;
            mIntervalMsec = intervalMsec > 0 ? intervalMsec : 1000;
            mMaxCount = maxCount;   // put a negative value for infinitive loop
        }

        public void start() {
            // If we have seen USB cable disconnection, then the SLIP error was generated by ordinal
            // cable disconnection. In this case we don't run retries.
            if (!mPowerConnected) {
                DebugTool.logInfo("Not starting retry timer");
                return;
            }

            DebugTool.logInfo("Starting retry timer");
            mStartTimeNsec = System.nanoTime();
            mCount = 0;
            mIsRunning = true;
            mHandler.postDelayed(this, mInitialDelayMsec);
        }

        public void stop() {
            DebugTool.logInfo("Stopping retry timer");
            stopInternal();
        }

        public boolean isRunning() {
            return mIsRunning;
        }

        @Override
        public void run() {
            // After SLIP error, we call disconnect(). So we expect that the state is still DISCONNECTED
            // when we run retry.
            if (mState != State.DISCONNECTED) {
                DebugTool.logInfo("Quit retry because state is already changed");
                stopInternal();
                return;
            }
            // If we see USB cable disconnection, don't retry. (See the comment in start())
            if (mLastPowerDisconnectedNsec >= mStartTimeNsec) {
                DebugTool.logInfo("Quit retry because USB disconnect detected");
                stopInternal();
                return;
            }

            // Retry connection and verify with ICMP echo. If retry succeeds, stop() will be called.
            connect();

            mCount++;
            if (mMaxCount >= 0 && mCount >= mMaxCount) {
                DebugTool.logInfo("Quit retry because of timeout");
                stopInternal();
            } else {
                mHandler.postDelayed(this, mIntervalMsec);
            }
        }

        private void stopInternal() {
            mIsRunning = false;
            mHandler.removeCallbacks(this);
        }
    }
}
