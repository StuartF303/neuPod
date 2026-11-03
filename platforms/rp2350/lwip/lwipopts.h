// SPDX-License-Identifier: MPL-2.0
/*
 * lwIP configuration for the RP2350 + CYW43439 (docs/wifi.md). The FreeRTOS flavour of the SDK's cyw43 arch: the radio
 * worker's async context also runs lwIP (core locking, no separate tcpip thread callbacks needed from our code).
 * Sized for RAM: static pools only (MEM_LIBC_MALLOC off), a modest TCP window, IPv4 only, no sockets, no TLS.
 */
#ifndef NEUPOD_LWIPOPTS_H
#define NEUPOD_LWIPOPTS_H

#define NO_SYS 0
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_TIMEVAL_PRIVATE 0
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_RAW 0
#define LWIP_ICMP 1
#define LWIP_UDP 1
#define LWIP_TCP 1
#define LWIP_DHCP 1
#define LWIP_DNS 1
#define LWIP_AUTOIP 0
#define LWIP_IGMP 0
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_HOSTNAME 1
#define LWIP_NETIF_TX_SINGLE_PBUF 1
#define LWIP_CHKSUM_ALGORITHM 3
#define LWIP_ALTCP 1        /* the HTTP client is built on altcp */
#define LWIP_ALTCP_TLS 0    /* no TLS on the device for now */
#define LWIP_HTTPD_SUPPORT_V09 0

#define MEM_LIBC_MALLOC 0
#define MEMP_MEM_MALLOC 0
#define MEM_ALIGNMENT 4
#define MEM_SIZE 6000
#define MEMP_NUM_TCP_PCB 4
#define MEMP_NUM_TCP_SEG 16
#define MEMP_NUM_UDP_PCB 4
#define MEMP_NUM_SYS_TIMEOUT 16
#define MEMP_NUM_ALTCP_PCB 4
#define PBUF_POOL_SIZE 12
#define TCP_MSS 1460
#define TCP_WND (4 * TCP_MSS)
#define TCP_SND_BUF (2 * TCP_MSS)
#define TCP_SND_QUEUELEN ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define TCP_QUEUE_OOSEQ 0
#define LWIP_TCP_KEEPALIVE 1
#define LWIP_STATS 1
#define LWIP_STATS_DISPLAY 0
#define MEM_STATS 1
#define MEMP_STATS 1
#define SYS_STATS 1
#define LINK_STATS 0
#define IP_STATS 0
#define TCP_STATS 0
#define UDP_STATS 0
#define ICMP_STATS 0
#define IGMP_STATS 0

/* FreeRTOS port (lwip-contrib sys_arch): stack sizes are in BYTES (LWIP_FREERTOS_THREAD_STACKSIZE_IS_STACKWORDS is 0). */
#define LWIP_TCPIP_CORE_LOCKING 1
/* 1024 left only 312 B free after a join on the bench; 2048 keeps >1 KB margin. */
#define TCPIP_THREAD_STACKSIZE 2048
#define TCPIP_THREAD_PRIO 3
#define TCPIP_MBOX_SIZE 8
#define DEFAULT_RAW_RECVMBOX_SIZE 8
#define DEFAULT_UDP_RECVMBOX_SIZE 8
#define DEFAULT_TCP_RECVMBOX_SIZE 8
#define DEFAULT_ACCEPTMBOX_SIZE 4
#define DEFAULT_THREAD_STACKSIZE 1024
#define DEFAULT_THREAD_PRIO 3

/* SNTP: the Wi-Fi service sets the clock from this hook (app/runtime/wifi_service.c). */
#define SNTP_SERVER_DNS 1
#define SNTP_MAX_SERVERS 2
#define SNTP_STARTUP_DELAY 0
#define SNTP_UPDATE_DELAY 3600000
#define SNTP_SET_SYSTEM_TIME(sec) neupod_sntp_set_time(sec)
#ifndef __ASSEMBLER__
#include <stdint.h>
void neupod_sntp_set_time(uint32_t sec);
#endif

#endif
