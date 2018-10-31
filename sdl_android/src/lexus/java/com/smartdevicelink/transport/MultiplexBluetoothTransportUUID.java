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
                //Lexus: 8c47102e-6882-11e8-adc0-fa7ae01bbebc
                SERVER_UUID = new UUID(0x8C47102E688211E8L, 0xABC0FA7AE01BBEBCL);
                break;
            case "china":
                SERVER_UUID = new UUID(0xE83168AADCF14278L, 0xAD083308892C52C4L);
                break;
        }
    }
}
