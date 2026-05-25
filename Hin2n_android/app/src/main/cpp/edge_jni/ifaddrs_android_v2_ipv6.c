/*
 * Android API 17 does not export getifaddrs/freeifaddrs.  The v2_ipv6 core uses
 * them to detect local IPv6 capability, so provide a small Android
 * implementation backed by /proc/net/if_inet6.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void freeifaddrs(struct ifaddrs *ifa);

static unsigned int read_interface_flags(const char *ifname) {
    char path[128];
    char buf[32];
    unsigned int flags = 0;
    FILE *fp;

    if (!ifname || ifname[0] == '\0') {
        return 0;
    }

    snprintf(path, sizeof(path), "/sys/class/net/%s/flags", ifname);
    fp = fopen(path, "r");
    if (!fp) {
        return 0;
    }

    if (fgets(buf, sizeof(buf), fp)) {
        flags = (unsigned int)strtoul(buf, NULL, 0);
    }
    fclose(fp);
    return flags;
}

static int hex_to_ipv6(const char *hex, struct in6_addr *addr) {
    unsigned int bytes[16];
    int i;

    if (!hex || strlen(hex) != 32 || !addr) {
        return -1;
    }

    if (sscanf(hex,
               "%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x",
               &bytes[0], &bytes[1], &bytes[2], &bytes[3],
               &bytes[4], &bytes[5], &bytes[6], &bytes[7],
               &bytes[8], &bytes[9], &bytes[10], &bytes[11],
               &bytes[12], &bytes[13], &bytes[14], &bytes[15]) != 16) {
        return -1;
    }

    for (i = 0; i < 16; ++i) {
        addr->s6_addr[i] = (unsigned char)bytes[i];
    }
    return 0;
}

int getifaddrs(struct ifaddrs **ifap) {
    FILE *fp;
    struct ifaddrs *head = NULL;
    struct ifaddrs *tail = NULL;
    char line[256];

    if (!ifap) {
        errno = EINVAL;
        return -1;
    }
    *ifap = NULL;

    fp = fopen("/proc/net/if_inet6", "r");
    if (!fp) {
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        char addr_hex[33];
        char ifname[IFNAMSIZ];
        unsigned int if_index;
        unsigned int prefix_len;
        unsigned int scope;
        unsigned int flags;
        struct ifaddrs *node;
        struct sockaddr_in6 *addr;

        if (sscanf(line, "%32s %x %x %x %x %15s",
                   addr_hex, &if_index, &prefix_len, &scope, &flags, ifname) != 6) {
            continue;
        }

        node = (struct ifaddrs *)calloc(1, sizeof(struct ifaddrs));
        addr = (struct sockaddr_in6 *)calloc(1, sizeof(struct sockaddr_in6));
        if (!node || !addr) {
            free(node);
            free(addr);
            freeifaddrs(head);
            fclose(fp);
            errno = ENOMEM;
            return -1;
        }

        addr->sin6_family = AF_INET6;
        addr->sin6_scope_id = if_index;
        if (hex_to_ipv6(addr_hex, &addr->sin6_addr) != 0) {
            free(node);
            free(addr);
            continue;
        }

        node->ifa_name = strdup(ifname);
        if (!node->ifa_name) {
            free(node);
            free(addr);
            freeifaddrs(head);
            fclose(fp);
            errno = ENOMEM;
            return -1;
        }

        node->ifa_flags = read_interface_flags(ifname);
        node->ifa_addr = (struct sockaddr *)addr;

        if (!head) {
            head = node;
        } else {
            tail->ifa_next = node;
        }
        tail = node;
    }

    fclose(fp);
    if (!head) {
        return -1;
    }

    *ifap = head;
    return 0;
}

void freeifaddrs(struct ifaddrs *ifa) {
    while (ifa) {
        struct ifaddrs *next = ifa->ifa_next;
        free(ifa->ifa_name);
        free(ifa->ifa_addr);
        free(ifa->ifa_netmask);
        free(ifa->ifa_dstaddr);
        free(ifa->ifa_data);
        free(ifa);
        ifa = next;
    }
}
