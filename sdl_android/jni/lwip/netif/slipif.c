/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *        may be used to endorse or promote products derived from this software
 *        without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.    IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Modified by Rakuto Furutani <rfurutani@uievolution.com>
 * Copyright 2012 UIEvolution Inc. All Rights Reserved.
 *
 * This is an arch independent SLIP netif. The specific serial hooks must be
 * provided by another file. They are sio_open, sio_read/sio_tryread and sio_send
 */


#include "netif/slipif.h"
#include "lwip/pcap.h"
#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include "lwip/stats.h"
#include "lwip/snmp.h"
#include "lwip/sio.h"
#include <pthread.h>

#define SLIP_END     0300     /* 0xC0 */
#define SLIP_ESC     0333     /* 0xDB */
#define SLIP_ESC_END 0334     /* 0xDC */
#define SLIP_ESC_ESC 0335     /* 0xDD */

#if LWIP_HAVE_SLIPIF

#ifndef unlikely
#if __GNUC__
#define unlikely(_expr) __builtin_expect(_expr, 0)
#else
#define unlikely(_expr) (_expr)
#endif
#endif

#define SLIP_BUFFER_SIZE 65536

enum slipif_recv_state {
    SLIP_RECV_NORMAL,
    SLIP_RECV_ESCAPE,
};

struct slipif_priv {
    sio_fd_t sd;
    /* q is the whole pbuf chain for a packet, p is the current pbuf in the chain */
    struct pbuf *p, *q;
    enum slipif_recv_state state;
    u16_t i, recved;
};

#if PCAP_LOG_OUTPUT
static struct pcap_handle_t *pcap_handle = NULL;
static pthread_mutex_t pcap_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif  // PCAP_LOG_OUTPUT

/**
 * Send a pbuf doing the necessary SLIP encapsulation
 *
 * Uses the serial layer's sio_send()
 *
 * @param netif the lwip network interface structure for this slipif
 * @param p the pbuf chaing packet to send
 * @param ipaddr the ip address to send the packet to (not used for slipif)
 * @return always returns ERR_OK since the serial layer does not provide return values
 */
err_t
slipif_output(struct netif *netif, struct pbuf *p, ip_addr_t *ipaddr) {
    /* Send pbuf out on the serial I/O device. */
    unsigned short index = 1;
    struct pbuf *q;
    struct slipif_priv *priv;
    unsigned char buf[8192]; // 4096 bytes SLIP frame + escaped character overhead.

    buf[0] = SLIP_END;

    priv = netif->state;

#define SLIP_COPY                           \
    c = ((u8_t *)q->payload)[i];            \
    if (unlikely(c == SLIP_END)) {          \
        buf[index] = SLIP_ESC;              \
        buf[++index] = SLIP_ESC_END;        \
    }                                       \
    else if (unlikely(c == SLIP_ESC)) {     \
        buf[index] = SLIP_ESC;              \
        buf[++index] = SLIP_ESC_ESC;        \
    }                                       \
    else {                                  \
        buf[index] = c;                     \
    }                                       \
    ++index;                                \
    ++i;

#if PCAP_LOG_OUTPUT

    if (pcap_handle) {
        pthread_mutex_lock(&pcap_mutex); {
            pcap_write(pcap_handle, p);
        }
        pthread_mutex_unlock(&pcap_mutex);
    }

#endif

    for (q = p; q != NULL; q = q->next) {
        u8_t c;
        int i = 0;
        int count = q->len;
        int n = (q->len + 7) / 8;

        switch (count % 8) {
            case 0: do { SLIP_COPY;

                         case 7:      SLIP_COPY;

                         case 6:      SLIP_COPY;

                         case 5:      SLIP_COPY;

                         case 4:      SLIP_COPY;

                         case 3:      SLIP_COPY;

                         case 2:      SLIP_COPY;

                         case 1:      SLIP_COPY; } while (--n > 0);
        }
    }

    LWIP_DEBUGF(SLIP_DEBUG, ("slipif_output: sends %d bytes\n", index + 1));

    buf[index] = SLIP_END;
    netif->slip_output_fn(netif, buf, ++index);

    return ERR_OK;
}

/**
 * Handle the incoming SLIP stream character by character
 *
 * Poll the serial layer by calling sio_read() or sio_tryread().
 */
err_t
slipif_input(struct netif *netif, void *data, size_t nbyte) {
    err_t err;
    struct slipif_priv *priv;
    struct pbuf *t = NULL;
    u8_t *p = data;
    u8_t *pe = ((u8_t *)data) + nbyte;

    LWIP_ASSERT("netif != NULL", (netif != NULL));
    LWIP_ASSERT("netif->state != NULL", (netif->state != NULL));

    priv = netif->state;

    while (p != pe) {
        u8_t c = *p++;

        switch (priv->state) {
            case SLIP_RECV_NORMAL:
                switch (c) {
                    case SLIP_END:

                        if (priv->recved > 0) {
                            /* Received whole packet. */
                            /* Trim the pbuf to the size of the received packet. */
                            pbuf_realloc(priv->q, priv->recved);

                            LINK_STATS_INC(link.recv);

                            LWIP_DEBUGF(SLIP_DEBUG, ("slipif: Got packet\n"));
                            t = priv->q;
                            priv->p = priv->q = NULL;
                            priv->i = priv->recved = 0;

#if PCAP_LOG_OUTPUT

                            if (pcap_handle) {
                                pthread_mutex_lock(&pcap_mutex); {
                                    pcap_write(pcap_handle, t);
                                }
                                pthread_mutex_unlock(&pcap_mutex);
                            }

#endif

                            err = netif->input(t, netif);

                            if (err != ERR_OK) {
                                pbuf_free(t);
                            }
                        }

                        continue;

                    case SLIP_ESC:
                        priv->state = SLIP_RECV_ESCAPE;
                        continue;
                }
                break;

            case SLIP_RECV_ESCAPE:
                switch (c) {
                    case SLIP_ESC_END:
                        c = SLIP_END;
                        break;

                    case SLIP_ESC_ESC:
                        c = SLIP_ESC;
                        break;
                }
                priv->state = SLIP_RECV_NORMAL;
                /* FALLTHROUGH */
        }

        /* byte received, packet not yet completely received */
        if (priv->p == NULL) {
            /* allocate a new pbuf */
            LWIP_DEBUGF(SLIP_DEBUG, ("slipif_input: alloc\n"));
            priv->p = pbuf_alloc(PBUF_LINK, (PBUF_POOL_BUFSIZE - PBUF_LINK_HLEN), PBUF_POOL);

            if (priv->p == NULL) {
                LINK_STATS_INC(link.drop);
                LWIP_DEBUGF(SLIP_DEBUG, ("slipif_input: no new pbuf! (DROP)\n"));
                /* don't process any further since we got no pbuf to receive to */
                break;
            }

            if (priv->q != NULL) {
                /* 'chain' the pbuf to the existing chain */
                pbuf_cat(priv->q, priv->p);
            } else {
                /* p is the first pbuf in the chain */
                priv->q = priv->p;
            }
        }

        /* this automatically drops bytes if > SLIP_MAX_SIZE */
        if ((priv->p != NULL) && (priv->recved <= SLIP_MTU)) {
            ((u8_t *)priv->p->payload)[priv->i] = c;
            priv->recved++;
            priv->i++;

            if (priv->i >= priv->p->len) {
                /* on to the next pbuf */
                priv->i = 0;

                if (priv->p->next != NULL && priv->p->next->len > 0) {
                    /* p is a chain, on to the next in the chain */
                    priv->p = priv->p->next;
                } else {
                    /* p is a single pbuf, set it to NULL so next time a new
                     * pbuf is allocated */
                    priv->p = NULL;
                }
            }
        }
    }

    return ERR_OK;
}

/**
 * SLIP netif initialization
 *
 * Call the arch specific sio_open and remember
 * the opened device in the state field of the netif.
 *
 * @param netif the lwip network interface structure for this slipif
 * @return ERR_OK if serial line could be opened,
 *                 ERR_MEM if no memory could be allocated,
 *                 ERR_IF is serial line couldn't be opened
 *
 * @note netif->num must contain the number of the serial port to open
 *             (0 by default)
 */
err_t
slipif_init(struct netif *netif, const char* pcap_file_path, slipif_output_fn out_fn) {
    struct slipif_priv *priv;

    LWIP_DEBUGF(SLIP_DEBUG, ("slipif_init: netif->num=%"U16_F "\n", (u16_t)netif->num));
    LWIP_ASSERT("slipif_output_fn != NULL", (out_fn != NULL));

#if PCAP_LOG_OUTPUT

    if (!pcap_handle && pcap_file_path != NULL) {
        struct pcap_config_t config = { PCAP_DEFAULT_MAX_SIZE, PCAP_DEFAULT_MAX_AGE };
        pcap_handle = pcap_open(pcap_file_path, &config);
    }

#endif

    /* Allocate private data */
    priv = mem_malloc(sizeof(struct slipif_priv));

    if (!priv) {
        return ERR_MEM;
    }

    netif->name[0] = 's';
    netif->name[1] = 'l';
    netif->name[2] = '0' + netif->num;
    netif->output = slipif_output;
    netif->mtu = SLIP_MTU;
    netif->flags |= NETIF_FLAG_POINTTOPOINT;
    netif->slip_output_fn = out_fn;

    /* Try to open the serial port (netif->num contains the port number). */

    /* Initialize private data */
    priv->p = NULL;
    priv->q = NULL;
    priv->state = SLIP_RECV_NORMAL;
    priv->i = 0;
    priv->recved = 0;

    netif->state = priv;

    /* initialize the snmp variables and counters inside the struct netif
     * ifSpeed: no assumption can be made without knowing more about the
     * serial line!
     */
    NETIF_INIT_SNMP(netif, snmp_ifType_slip, 0);

    /* Create a thread to pull the serial line. */
    /*
     * sys_thread_new(SLIPIF_THREAD_NAME, slipif_loop, netif,
     *                SLIPIF_THREAD_STACKSIZE, SLIPIF_THREAD_PRIO);
     */

    return ERR_OK;
}

#endif /* LWIP_HAVE_SLIPIF */
