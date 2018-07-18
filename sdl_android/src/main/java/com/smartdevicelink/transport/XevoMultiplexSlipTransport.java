package com.smartdevicelink.transport;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbManager;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import com.smartdevicelink.protocol.SdlPacket;
import com.smartdevicelink.transport.enums.TransportType;
import com.smartdevicelink.util.DebugTool;
import com.smartdevicelink.xevo.slip.UsbSlipDriver;
import com.smartdevicelink.xevo.transport.NetconnSocket;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;

/**
 * Created by swatanabe on 2017/07/31.
 */

public class XevoMultiplexSlipTransport extends MultiplexBaseTransport {
	private final String TAG = "MultiplexSlipTransport";

	private Handler mHandler;
	private static XevoMultiplexSlipTransport mInstance = null;
	private Context mContext;

	private NetconnSocket mSocket = null;
	private Thread mReaderThread = null;
	private static final int RECONNECT_RETRY_COUNT = 30;
	private static final int RECONNECT_DELAY = 5000;
	private XevoSlipTransportConfig mConfig;
	private Intent mUsbReceiverIntent = null;

	private final int TCPAdapterPort = 15324; // t'was 12345
	private final String SLIP_PEER_ADDRESS = "192.168.3.2";

	// public ctor
	public XevoMultiplexSlipTransport(Handler handler, TransportType type) {
		super(handler, type);
		Log.d(TAG, "XevoMultiplexAOATrasport:ctor");
		//mContext = context;
		mHandler = handler;
		mConfig = new XevoSlipTransportConfig(TCPAdapterPort, SLIP_PEER_ADDRESS, true);
		mState = STATE_NONE;
	}
	/**
	 * Constructor: prepares the new AOA transport session.
	 * @param handler: A hanlder to send message back to UI
	 */
	/**
	 * this was private ctor
	 * @param context
	 * @param handler
	 */
	private void init(Context context, Handler handler) {
		Log.d(TAG, "XevoMultiplexAOATrasport:ctor");
		mContext = context;
		mHandler = handler;
		mConfig = new XevoSlipTransportConfig(TCPAdapterPort, SLIP_PEER_ADDRESS, true);
		mState = STATE_NONE;
	}

	public void setContext(Context context) {
		mContext = context;
	}
	// singleton pattern
	/*--
	public synchronized  static XevoMultiplexSlipTransport getInstance(Context context, Handler handler) {
		if (mInstance == null) {
			mInstance = new XevoMultiplexSlipTransport(context, handler);
		}
		return mInstance;
	} --*/

	public synchronized void start() {
		if (mState != STATE_CONNECTED) {
			setState(STATE_LISTEN);
		}
		if (mContext != null) {
			UsbSlipDriver.init(mContext);
		}
		registerReciever(mContext);
	}

	public synchronized void stop(int state) {
		disconnect("stopping Slip Transport", null, true);
		if (mState != state) {
			setState(state);
		}
		unregisterReceiver(mContext);
	}

	@Override
	public boolean isConnected() {
		return UsbSlipDriver.getInstance().isConnected();
	}

	private void registerReciever(final Context context) {
		if (mUsbReceiverIntent == null) {
			IntentFilter filter = new IntentFilter();
			filter.addAction(TransportConstants.AOA_OPEN_ACCESSORY);
			filter.addAction(UsbManager.ACTION_USB_ACCESSORY_DETACHED);
			mUsbReceiverIntent = context.registerReceiver(mUSBReceiver, filter);
			Log.d(TAG, "registerReceiver");
		}
		// Check to see if SlipDriver is connected, and automatically start ReaderThread.
		if (UsbSlipDriver.getInstance().isConnected()) {
			// let's send broadcast again.
			Handler handler = new Handler(Looper.getMainLooper());
			handler.postDelayed(new Runnable() {
				@Override
				public void run() {
					Intent intent = new Intent(TransportConstants.AOA_OPEN_ACCESSORY);
					context.sendBroadcast(intent);
				}
			}, 500);
		}
	}

	private void unregisterReceiver(Context context) {
		if (mUsbReceiverIntent != null) {
			context.unregisterReceiver(mUSBReceiver);
			mUsbReceiverIntent = null;
		}
	}

	private final BroadcastReceiver mUSBReceiver = new BroadcastReceiver() {
		@Override
		public void onReceive(Context context, Intent intent) {
			String action = intent.getAction();
			DebugTool.logInfo("mUSBReceiver onReceive:action=" + action);
			if (action.equals(TransportConstants.AOA_OPEN_ACCESSORY)) {
				DebugTool.logInfo("AOA_ROUTER_OPEN_ACCESSORY");
				startReaderThread();
			} else if (action.equals(UsbManager.ACTION_USB_ACCESSORY_DETACHED)) {
				DebugTool.logInfo("ACTION_USB_ACCESSORY_DETACHED");
				setState(STATE_NONE); // This will cause onTransportDisconnect on XevoSlipRouterService.
				disconnect("USB has been disconnected", null, true);
			}
		}
	};

	private void startReaderThread() {
		DebugTool.logInfo("startReaderThread");
		if (mReaderThread == null) {
			mReaderThread = new Thread((new MultiplexSlipTransportReader()));
			mReaderThread.setDaemon(true);
			mReaderThread.setName(MultiplexSlipTransportReader.class.getName());
			mReaderThread.start();
		}
	}

	/*--
	public synchronized void setMuxState(int newState) {
		int previousState = mMuxState;
		mMuxState = newState;
		DebugTool.logInfo(String.format("setMusState: %d", newState));
		mHandler.obtainMessage(SdlRouterService.MESSAGE_STATE_CHANGE, newState, 0).sendToTarget();
	} --*/

	protected boolean sendBytesOverTransport(SdlPacket packet) {
		byte[] msgBytes = packet.constructPacket();
		boolean result = false;
		final int state = mState;
		switch(state) {
			case STATE_CONNECTED:
				if (mSocket != null) {
					try {
						mSocket.send(msgBytes, 0, msgBytes.length);
						result = true;

						//DebugTool.logInfo("Bytes successfully sent");
					} catch (IOException e) {
						//final String msg = "Failed to send bytes over USB";
						//DebugTool.logWarning(msg, e);
						e.printStackTrace();
						// unexpected error. disconnect
						disconnect(e.getMessage(), e, true);
					}
				} else {
					//DebugTool.logWarning(msg);
				}
				break;
			default:
				break;
		}
		return result;
	}

	public void write(byte[] out,  int offset, int count) {
		try {
			if (mSocket != null) {
				mSocket.send(out, offset, count);
			}

			Log.d(TAG, String.format("%d bytes successfully sent", count));
		} catch (IOException e) {
			e.printStackTrace();
			// unexpected error. disconnect
			disconnect(e.getMessage(), e, true);
		}
	}

	private class MultiplexSlipTransportReader implements Runnable {
		SdlPsm mPsm;
		boolean isHalted = false;

		@Override
		public void run() {
			mPsm = new SdlPsm();
			mPsm.reset();
			// try delaying somewhat
			if (connect()) {
				readFromTransport();
			} else {
				isHalted = true;
			}
		}

		private boolean connect() {
			if (Thread.interrupted()) {
				return false;
			}
			boolean connected = false;
			int remainingRetry = RECONNECT_RETRY_COUNT;
			synchronized(XevoMultiplexSlipTransport.this) {
				do {
					try {
						if (mSocket != null) {
							mSocket.close();
							mSocket = null;
						}
						Log.d(TAG, String.format("XevoSlipTransport.connect: Socket is closed. Trying to connect"));
						mSocket = new NetconnSocket(NetconnSocket.Protocol.TCP);
						int connectionResult = mSocket.connect(new InetSocketAddress(mConfig.getIPAddress(), mConfig.getPort()));
						if (connectionResult != 0) {
							DebugTool.logError(String.format("NetconnSocket connect failed (%d)", connectionResult));
						}
						connected = (null != mSocket) && (connectionResult == 0);
					} catch(IOException e) {
						e.printStackTrace();
					}
					if (connected) {
						Log.d(TAG, "NetconnSocket connected");
						setState(STATE_CONNECTED);
						mHandler.obtainMessage(SdlRouterService.MESSAGE_STATE_CHANGE, MultiplexBaseTransport.STATE_CONNECTED, 0, TransportType.USB).sendToTarget(); // Let RouterService know HARDWARE_CONNECTED.
					} else {
						if(mConfig.getAutoReconnect()){
							remainingRetry--;
							DebugTool.logInfo(String.format("XevoSlipTransport.connect: Socket not connected. AutoReconnect is ON. retryCount is: %d. Waiting for reconnect delay: %d"
									, remainingRetry, RECONNECT_DELAY));
							waitFor(RECONNECT_DELAY);
						} else {
							DebugTool.logInfo("XevoSlipTransport.connect: Socket not connected. AutoReconnect is OFF");
						}
					}
				} while (!connected && mConfig.getAutoReconnect() && remainingRetry > 0 && !isHalted);
			}
			return connected;
		}

		private void readFromTransport() {
			final int READ_BUFFER_SIZE = 4096;
			ByteBuffer buffer = ByteBuffer.allocateDirect(READ_BUFFER_SIZE);
			int bytesRead;
			boolean stateProgress = false;
			int error = 0;
			while(!isHalted) {
				try {
					bytesRead = mSocket.recv(buffer);
					Log.d(TAG, String.format("readFromTransport: %d bytes", bytesRead));
					if (bytesRead == -1) {
						return;
					} else if (bytesRead > 0){
						for (int i=0; i<bytesRead; i++) {
							byte input = buffer.get(i);
							stateProgress = mPsm.handleByte(input);
							if (!stateProgress) {
								mPsm.reset();
							}
							if (mPsm.getState() == SdlPsm.FINISHED_STATE) {
								synchronized (this) {
									Log.d(TAG, "Packet formed, sending off");
									SdlPacket packet = mPsm.getFormedPacket();
									packet.setTransportType(TransportType.USB);
									mHandler.obtainMessage(SdlRouterService.MESSAGE_READ, packet).sendToTarget();
								}
								mPsm.reset();
							}
						}
					}
				} catch(IOException e) {
					DebugTool.logError(e.getMessage());

					break;
				}
			}
		}
	}

	private void waitFor(long timeMs) {
		long endTime = System.currentTimeMillis() +timeMs;
		while (System.currentTimeMillis() < endTime) {
			synchronized (this) {
				try {
					wait(endTime - System.currentTimeMillis());
				} catch (Exception e) {
					// Nothing To Do, simple wait
				}
			}
		}
	}

	private synchronized void disconnect(String message, Exception exception, boolean stopThread) {
		setState(STATE_LISTEN);
		try {
			if(mReaderThread != null && stopThread) {
				mReaderThread.interrupt();
			}

			if(mSocket != null){
				int err = mSocket.close();
				if (err != 0) {
					DebugTool.logError(String.format("NetconnSocket close: err=%d", err));
				}
			}
			mSocket = null;
			mReaderThread = null;
			setState(STATE_LISTEN);
		} catch (IOException e) {
			Log.e(TAG, "XevoSlipTransport.disconnect: Exception during disconnect: " + e.getMessage());
		} catch (Throwable tr) {
			tr.printStackTrace();
		}
	}
}
