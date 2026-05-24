/*
 * Android VpnService-backed tuntap adapter for n2n_v2_ipv6.
 */

#include "n2n.h"

#ifdef __ANDROID_NDK__
#include <arpa/inet.h>
#include <fcntl.h>
#include <jni.h>
#include <string.h>
#include <unistd.h>
#include <tun2tap/tun2tap.h>
#include <edge_jni/edge_jni.h>

static int clear_nonblock(int fd) {
    int val;

    if (fd < 0) {
        return -1;
    }

    val = fcntl(fd, F_GETFL);
    if (val == -1) {
        return -1;
    }
    if ((val & O_NONBLOCK) == O_NONBLOCK) {
        val &= ~O_NONBLOCK;
        if (fcntl(fd, F_SETFL, val) == -1) {
            return -1;
        }
    }

    return 0;
}

static int establish_vpn_service(const struct tuntap_config *config) {
    JNIEnv *env = NULL;
    jclass vpn_service_cls;
    jmethodID mid;
    jstring j_ip;
    char ip_str[INET_ADDRSTRLEN];
    int vpn_fd;
    int attached = 0;

    if (!g_status || !g_status->jvm || !g_status->jobj_service || !config) {
        return -1;
    }

    if ((*g_status->jvm)->GetEnv(g_status->jvm, (void **)&env, JNI_VERSION_1_1) != JNI_OK || !env) {
        if ((*g_status->jvm)->AttachCurrentThread(g_status->jvm, &env, NULL) != JNI_OK || !env) {
            return -1;
        }
        attached = 1;
    }

    vpn_service_cls = (*env)->GetObjectClass(env, g_status->jobj_service);
    if (!vpn_service_cls) {
        if (attached) {
            (*g_status->jvm)->DetachCurrentThread(g_status->jvm);
        }
        return -1;
    }

    mid = (*env)->GetMethodID(env, vpn_service_cls, "EstablishVpnService", "(Ljava/lang/String;I)I");
    if (!mid) {
        (*env)->DeleteLocalRef(env, vpn_service_cls);
        if (attached) {
            (*g_status->jvm)->DetachCurrentThread(g_status->jvm);
        }
        return -1;
    }

    if (!inet_ntop(AF_INET, &config->ip_addr, ip_str, sizeof(ip_str))) {
        strncpy(ip_str, g_status->cmd.ip_addr, sizeof(ip_str) - 1);
        ip_str[sizeof(ip_str) - 1] = '\0';
    }

    j_ip = (*env)->NewStringUTF(env, ip_str);
    if (!j_ip) {
        (*env)->DeleteLocalRef(env, vpn_service_cls);
        if (attached) {
            (*g_status->jvm)->DetachCurrentThread(g_status->jvm);
        }
        return -1;
    }

    vpn_fd = (*env)->CallIntMethod(env, g_status->jobj_service, mid, j_ip, (jint)config->ip_prefixlen);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        vpn_fd = -1;
    }

    (*env)->DeleteLocalRef(env, j_ip);
    (*env)->DeleteLocalRef(env, vpn_service_cls);
    if (attached) {
        (*g_status->jvm)->DetachCurrentThread(g_status->jvm);
    }

    return vpn_fd;
}

int tuntap_open(tuntap_dev *device, struct tuntap_config *config) {
    if (!device || !config || !g_status) {
        return -1;
    }

    if (g_status->cmd.vpn_fd < 0) {
        g_status->cmd.vpn_fd = establish_vpn_service(config);
    }
    if (g_status->cmd.vpn_fd < 0 || clear_nonblock(g_status->cmd.vpn_fd) < 0) {
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
