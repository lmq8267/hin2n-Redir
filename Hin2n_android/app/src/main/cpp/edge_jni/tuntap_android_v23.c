/*
 * Android VpnService-backed tuntap adapter for n2n_v2_ipv6.
 */

#include "n2n.h"

#ifdef __ANDROID_NDK__
#include <unistd.h>
#include <tun2tap/tun2tap.h>
#include <edge_jni/edge_jni.h>

int tuntap_open(tuntap_dev *device, struct tuntap_config *config) {
    if (!device || !config || !g_status || g_status->cmd.vpn_fd < 0) {
        return -1;
    }

    memset(device, 0, sizeof(*device));
    device->fd = g_status->cmd.vpn_fd;
    memcpy(device->mac_addr, config->device_mac, sizeof(device->mac_addr));
    device->ip_addr = config->ip_addr;
    device->ip_prefixlen = config->ip_prefixlen;
    device->ip6_addr = config->ip6_addr;
    device->ip6_prefixlen = config->ip6_prefixlen;
    device->mtu = config->mtu;
    device->routes_count = config->routes_count;
    device->routes = config->routes;
    strncpy(device->dev_name, config->if_name ? config->if_name : "edge_v23", N2N_IFNAMSIZ - 1);

    return device->fd;
}

ssize_t tuntap_read(struct tuntap_dev *tuntap, unsigned char *buf, size_t len) {
    ssize_t rlen;

    if (!tuntap || tuntap->fd < 0 || len <= UIP_LLH_LEN) {
        return -1;
    }

    memset(buf, 0, UIP_LLH_LEN);
    rlen = read(tuntap->fd, buf + UIP_LLH_LEN, len - UIP_LLH_LEN);
    if (rlen < 0) {
        return rlen;
    }

    return rlen + UIP_LLH_LEN;
}

ssize_t tuntap_write(struct tuntap_dev *tuntap, unsigned char *buf, size_t len) {
    ssize_t rlen;

    if (!tuntap || tuntap->fd < 0 || len <= UIP_LLH_LEN) {
        return -1;
    }

    uip_buf = buf;
    uip_len = len;
    if (IPBUF->ethhdr.type != htons(UIP_ETHTYPE_IP) &&
        IPBUF->ethhdr.type != htons(UIP_ETHTYPE_IP6)) {
        return 0;
    }

    rlen = write(tuntap->fd, buf + UIP_LLH_LEN, len - UIP_LLH_LEN);
    if (rlen < 0) {
        return rlen;
    }

    return rlen + UIP_LLH_LEN;
}

void tuntap_close(struct tuntap_dev *tuntap) {
    if (tuntap && tuntap->fd > 0) {
        close(tuntap->fd);
        tuntap->fd = -1;
    }
}

void tuntap_get_address(struct tuntap_dev *tuntap) {
}

int set_ipaddress(const tuntap_dev *device, int static_address) {
    return 0;
}

#endif /* __ANDROID_NDK__ */
