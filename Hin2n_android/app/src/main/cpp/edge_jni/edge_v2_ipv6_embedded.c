/*
 * Android embeds the v2_ipv6 edge main loop in-process. Reset the edge.c file-scope
 * state before each run so a reconnect uses the latest Java-side configuration.
 */

#include <arpa/inet.h>
#include <stddef.h>
#include <ifaddrs.h>
#include <tun2tap/tun2tap.h>

#define tuntap_write edge_v2_ipv6_android_tuntap_write
#define main edge_v2_ipv6_main_impl
#include "edge.c"
#undef main
#undef tuntap_write

extern ssize_t tuntap_write(tuntap_dev *tuntap, unsigned char *buf, size_t len);

static void edge_v2_ipv6_reset_runtime_state(void) {
    default_ip_assignment = 0;
    initial_connection_complete = 0;
    g_edge_running = 1;
    traceLevel = 2;
    useSyslog = false;
    useSystemd = false;
    optind = 1;
    optarg = NULL;
}

int edge_v2_ipv6_main(int argc, char *argv[]) {
    edge_v2_ipv6_reset_runtime_state();
    return edge_v2_ipv6_main_impl(argc, argv);
}

void edge_v2_ipv6_request_stop(void) {
    g_edge_running = 0;
}

ssize_t edge_v2_ipv6_android_tuntap_write(tuntap_dev *tuntap, unsigned char *buf, size_t len) {
    n2n_edge_t *eee;

    if (!tuntap || !buf || len <= UIP_LLH_LEN) {
        return -1;
    }

    uip_buf = buf;
    uip_len = len;
    if (IPBUF->ethhdr.type == htons(UIP_ETHTYPE_ARP)) {
        uip_arp_arpin();
        if (uip_len > 0) {
            eee = (n2n_edge_t *)((char *)tuntap - offsetof(n2n_edge_t, device));
            traceEvent(TRACE_DEBUG, "v2_ipv6 Android ARP reply packet prepared for n2n");
            send_packet2net(eee, uip_buf, uip_len);
        }
        return (ssize_t)len;
    }

    return tuntap_write(tuntap, buf, len);
}
