package com.smartdevicelink.transport;

import java.util.UUID;
import com.smartdevicelink.BuildConfig;

/**
 * Created on 10/20/2017.
 */

final class MultiplexBluetoothTransportUUID {
    // Lexus UUID
    static final UUID SERVER_UUID;

    static {
        switch (BuildConfig.FLAVOR_region) {
            case "us":
            default:
                SERVER_UUID = new UUID(0x548BDF7CCB0C427CL, 0x9969B556A27B718AL);
                break;
            case "row":
                SERVER_UUID = new UUID(0x93BABF2C7FA74A50L, 0xAD26811E57BBC911L);
                break;
            case "europe":
                SERVER_UUID = new UUID(0x10306B12C4864658L, 0xA478DB83C1A718DEL);
                break;
            case "australia":
                SERVER_UUID = new UUID(0xEB75687B8CEC484FL, 0x9D2B97E36DA409C2L);
                break;
            case "china":
                SERVER_UUID = new UUID(0xE83168AADCF14278L, 0xAD083308892C52C4L);
                break;
        }
    }
}
