//
// pcap.h
// UIE MultiAccess
//
// Created by Rakuto Furutani on 02/04/2015
// Copyright 2015 UIEVolution Inc. All Rights Reserved.
//

#pragma once

// See http://www.tcpdump.org/linktypes.html
#define PCAP_LINKTYPE_SLIP 8
#define PCAP_LINKTYPE_IPV4 228

struct pbuf;

struct pcap_config_t {
    off_t max_size;   // Log files are rotated when they grow bigger than max_size bytes.
    int max_age;      // Remove rotated logs older than max_age.
};

#define PCAP_DEFAULT_MAX_SIZE (10 * 1024 * 1024) // 10MB
#define PCAP_DEFAULT_MAX_AGE  (10)
#define PCAP_CONFIG_INIT      { PCAP_DEFAULT_MAX_SIZE, PCAP_DEFAULT_MAX_AGE }

/**
 * Open new file handle.
 *
 * @param path      A file path
 * @param config    A logging configuration
 * @return A new PCAP file handle.
 *
 * @note Not thread safe.
 */
struct pcap_handle_t * pcap_open(const char *path, struct pcap_config_t *config);

/**
 * Log packets.
 * Not thread safe.
 *
 * @param handle
 * @param pbuf
 * @return
 */
int pcap_write(struct pcap_handle_t *handle, struct pbuf *pbuf);

/**
 * Close PCAP file handle.
 *
 * @param handle
 * @return 0 if success, otherwise -1.
 *
 * @note Not thread safe.
 */
int pcap_close(struct pcap_handle_t *handle);
