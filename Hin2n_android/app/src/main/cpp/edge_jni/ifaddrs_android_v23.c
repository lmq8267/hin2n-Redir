/*
 * Android API 17 does not export getifaddrs/freeifaddrs.  The v23 core only
 * uses them for optional local interface discovery, so returning failure keeps
 * the upstream code buildable without modifying bundles/n2n_v2_ipv6.
 */

struct ifaddrs;

int getifaddrs(struct ifaddrs **ifap) {
    if (ifap) {
        *ifap = 0;
    }
    return -1;
}

void freeifaddrs(struct ifaddrs *ifa) {
    (void)ifa;
}
