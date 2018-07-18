//
// pcap.c
// UIE MultiAccess
//
// Created by Rakuto Furutani on 02/04/2015
// Copyright 2015 UIEVolution Inc. All Rights Reserved.
//

#include "lwip/opt.h"
#include "lwip/pcap.h"
#include "lwip/pbuf.h"
#include <assert.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <libgen.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#ifndef MIN
#define MIN(a, b) (a > b) ? b : a
#endif

#define MAX_FILE_NAME_LENGTH 256

struct pcap_handle_t {
    FILE *fp;
    off_t size;
    char path[MAX_FILE_NAME_LENGTH];

    // Logrotate
    int max_age;
    int max_size;
};

#pragma pack(push, 1)
struct pcap_hdr_s {
    uint32_t magic_number;
    uint16_t version_major;
    uint16_t version_minor;
    int32_t thiszone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;
};

struct pcaprec_hdr_s {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
};
#pragma pack(pop)

static void pcap_file_name(const char *prefix, int age, char *buf, size_t bufsize) {
    LWIP_ERROR("pcap_file_name: prefix == NULL", prefix != NULL, errno = EINVAL; return; );
    LWIP_ERROR("pcap_file_name: handle == NULL", buf != NULL, errno = EINVAL; return; );

    if (age) {
        snprintf(buf, bufsize, "%s_%03d.pcap", prefix, age);
    } else {
        snprintf(buf, bufsize, "%s.pcap", prefix);
    }
}

static int pcap_write_header(struct pcap_handle_t *handle) {
    LWIP_ERROR("pcap_write: handle == NULL", handle != NULL, return -1; );

    if (!handle->fp) {
        LWIP_DEBUGF(PCAP_DEBUG, ("pcap_write_header: file hanlde is NULL\n"));
        return -1;
    }

    // See http://wiki.wireshark.org/Development/LibpcapFileFormat
    // Write out PCAP file header
    struct pcap_hdr_s hdr = {
        .magic_number  = 0xa1b2c3d4,
        .version_major = 2,
        .version_minor = 4,
        .thiszone      = 0,
        .sigfigs       = 0,
        .snaplen       = 65535,
        .network       = PCAP_LINKTYPE_IPV4,
    };
    fwrite(&hdr, 1, sizeof(hdr), handle->fp);
    handle->size += sizeof(hdr);

    return 0;
}

/**
 * Rotate PCAP log file.
 *
 * @return 0 if success, otherwise -1. errno may be set.
 */
static int logrotate(struct pcap_handle_t *handle) {
    int i;
    char buf[MAX_FILE_NAME_LENGTH + 16], buf1[MAX_FILE_NAME_LENGTH + 16]; // _000.pcap

    LWIP_ERROR("logrotate: handle == NULL", handle != NULL, errno = EINVAL; return -1; );

    LWIP_DEBUGF(PCAP_DEBUG, ("perform logrotate"));

    // No log rotation if max_age is less or equal than 0
    if (handle->max_age <= 0) {
        return 0;
    }

    if (!handle->fp) {
        LWIP_DEBUGF(PCAP_DEBUG, ("logrotate: file hanlde is NULL\n"));
        return -1;
    }

    fclose(handle->fp);
    handle->fp = NULL;

    for (i = handle->max_age - 1; i >= 0; --i) {
        pcap_file_name(handle->path, i, buf, sizeof(buf));

        if (access(buf, F_OK) != -1) {
            pcap_file_name(handle->path, i + 1, buf1, sizeof(buf1));

            if (!rename(buf, buf1)) {
                LWIP_DEBUGF(PCAP_DEBUG, ("logrotate: %s -> %s\n", buf, buf1));
            }
        }
    }

    // Open new log file handle
    handle->size = 0;
    pcap_file_name(handle->path, 0, buf, sizeof(buf));

    handle->fp = fopen(buf, "wb");

    if (!handle->fp) {
        LWIP_DEBUGF(PCAP_DEBUG, ("logrotate: cannot open %s\n", buf));
        return -1;
    }

    pcap_write_header(handle);

    return 0;
}

struct pcap_handle_t * pcap_open(const char *file, struct pcap_config_t *config) {
    LWIP_ERROR("pcap_open: file == NULL", file != NULL, return NULL; );

    const char *dir = dirname(file);
    mkdir(dir, 0777);

    struct pcap_config_t cfg = PCAP_CONFIG_INIT;

    if (config) {
        cfg = *config;
    }

    char fname[MAX_FILE_NAME_LENGTH + 16];
    pcap_file_name(file, 0, fname, sizeof(fname));

    FILE *fp = fopen(fname, "ab");

    if (!fp) {
        LWIP_DEBUGF(PCAP_DEBUG, ("pcap_open: cannot open %s\n", fname));
        return NULL;
    }

    LWIP_DEBUGF(PCAP_DEBUG, ("pcap_open: %s\n", file));

    struct pcap_handle_t *handle = (struct pcap_handle_t *)malloc(sizeof(struct pcap_handle_t));

    if (!handle) {
        LWIP_DEBUGF(PCAP_DEBUG, ("pcap_open: OOM on PCAP handle.\n"));
        fclose(fp);
        return NULL;
    }

    // Store current file size.
    fseek(fp, 0, SEEK_END);
    handle->size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    handle->fp = fp;
    handle->max_age = cfg.max_age;
    handle->max_size = cfg.max_size;
    strncpy(handle->path, file, sizeof(handle->path));
    handle->path[sizeof(handle->path) - 1] = '\0';

    if (!handle->size) {
        pcap_write_header(handle);
    }

    LWIP_DEBUGF(PCAP_DEBUG, ("pcap_open: path=%s age=%d max_size=%d", file, cfg.max_age, cfg.max_size));

    return handle;
}

int pcap_write(struct pcap_handle_t *handle, struct pbuf *pbuf) {
    LWIP_ERROR("pcap_write: handle == NULL", handle != NULL, return -1; );
    LWIP_ERROR("pcap_write: pbuf == NULL", pbuf != NULL, return -1; );

    if (!handle->fp) {
        LWIP_DEBUGF(PCAP_DEBUG, ("pcap_write: fp is NULL"));
        return -1;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);

    char buffer[65535 + sizeof(struct pcaprec_hdr_s)]; // u16_t pbuf->tot_len
    char *p = buffer;

    struct pcaprec_hdr_s hdr = {
        .ts_sec   = tv.tv_sec,
        .ts_usec  = tv.tv_usec,
        .incl_len = pbuf->tot_len,
        .orig_len = pbuf->tot_len,
    };

    memcpy(p, &hdr, sizeof(hdr));
    p += sizeof(hdr);

    struct pbuf *pp = pbuf;
    do {
        memcpy(p, pp->payload, pp->len);
        p += pp->len;
    } while ((pp = pp->next) != NULL);

    size_t nwrite = p - buffer;

    if (fwrite(buffer, 1, nwrite, handle->fp) < nwrite) {
        LWIP_DEBUGF(PCAP_DEBUG, ("pcap_write: fwrite failed: %s", strerror(errno)));
        return -1;
    }

    fflush(handle->fp);
    handle->size += nwrite;

    // Run log rotation if current file exceeds threshold
    if (handle->max_size <= handle->size) {
        logrotate(handle);
    }

    return 0;
}

int pcap_close(struct pcap_handle_t *handle) {
    LWIP_ERROR("pcap_close: handle == NULL", handle != NULL, return -1; );

    if (!handle->fp) {
        return -1;
    }

    fclose(handle->fp);
    free(handle);

    return 0;
}
