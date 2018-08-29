package com.smartdevicelink.transport;

import java.util.UUID;
import com.smartdevicelink.BuildConfig;

/**
 * Created on 10/20/2017.
 */

final class MultiplexBluetoothTransportUUID {
    // Toyota UUID
    static final UUID SERVER_UUID;

    static {
        switch (BuildConfig.FLAVOR_region) {
            case "us":
            default:
                SERVER_UUID = new UUID(0xCD6A760D4539441CL, 0xB04235FEB02E2765L);
                break;
            case "row":
                SERVER_UUID = new UUID(0xDDAFEE4507A54BD7L, 0xAA52E5DE35EA6ABFL);
                break;
            case "europe":
                SERVER_UUID = new UUID(0x20AD4E0C71484C0BL, 0xB69A33542D731192L);
                break;
            case "australia":
                SERVER_UUID = new UUID(0xC84E79CCC1D8444FL, 0x9F5278E5858D051AL);
                break;
            case "china":
                SERVER_UUID = new UUID(0xBEB5A53BF5974ACBL, 0x9CAEC3193489A109L);
                break;
        }
    }
}
