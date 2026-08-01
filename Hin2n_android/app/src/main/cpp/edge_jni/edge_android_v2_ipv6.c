/*
 * JNI bridge glue for the n2n_v2_ipv6 edge core.
 */

#include <android/log.h>
#include <arpa/inet.h>
#include <edge_jni/edge_jni.h>
#include <fcntl.h>
#include <setjmp.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "n2n.h"

#define V2_IPV6_MGMT_PORT 5664

extern int edge_v2_ipv6_main(int argc, char *argv[]);
extern void edge_v2_ipv6_request_stop(void);
extern int __real_socket(int domain, int type, int protocol);
extern void __real_exit(int status);
extern void __real_abort(void);

n2n_edge_status_t *g_status;
static volatile int stop_requested = 0;

static int protect_socket_v2_ipv6(int fd);
static void log_edge_v2_ipv6_command_line(int argc, char *argv[]);
static int android_stop_requested(const char *stage);
static int gateway_address_family(const char *gateway);
static jmp_buf exit_jmp;
static volatile int exit_trap_enabled = 0;
static volatile int exit_trap_status = 1;

void __wrap_exit(int status) {
    if (exit_trap_enabled) {
        exit_trap_status = status == 0 ? 1 : status;
        __android_log_print(ANDROID_LOG_ERROR, "edge_v2_ipv6", "Intercepted exit(%d)", status);
        longjmp(exit_jmp, 1);
    }
    __real_exit(status);
}

void __wrap_abort(void) {
    if (exit_trap_enabled) {
        exit_trap_status = 134;
        __android_log_write(ANDROID_LOG_ERROR, "edge_v2_ipv6", "Intercepted abort()");
        longjmp(exit_jmp, 1);
    }
    __real_abort();
}

int __wrap_setuid(uid_t uid) {
    __android_log_print(ANDROID_LOG_WARN, "edge_v2_ipv6", "Skipped setuid(%u) on Android", (unsigned int)uid);
    return 0;
}

int __wrap_setgid(gid_t gid) {
    __android_log_print(ANDROID_LOG_WARN, "edge_v2_ipv6", "Skipped setgid(%u) on Android", (unsigned int)gid);
    return 0;
}

int __wrap_setreuid(uid_t ruid, uid_t euid) {
    __android_log_print(ANDROID_LOG_WARN, "edge_v2_ipv6", "Skipped setreuid(%u, %u) on Android",
                        (unsigned int)ruid, (unsigned int)euid);
    return 0;
}

int __wrap_setregid(gid_t rgid, gid_t egid) {
    __android_log_print(ANDROID_LOG_WARN, "edge_v2_ipv6", "Skipped setregid(%u, %u) on Android",
                        (unsigned int)rgid, (unsigned int)egid);
    return 0;
}

int __wrap_socket(int domain, int type, int protocol) {
    int fd = __real_socket(domain, type, protocol);

    if (fd >= 0) {
        protect_socket_v2_ipv6(fd);
    }

    return fd;
}

static int protect_socket_v2_ipv6(int fd) {
    JNIEnv *env = NULL;

    if (!g_status || !g_status->jvm || !g_status->jobj_service || fd < 0) {
        return -1;
    }

    if ((*g_status->jvm)->GetEnv(g_status->jvm, (void **)&env, JNI_VERSION_1_1) != JNI_OK || !env) {
        return -1;
    }

    jclass vpn_service_cls = (*env)->GetObjectClass(env, g_status->jobj_service);
    if (!vpn_service_cls) {
        return -1;
    }

    jmethodID protect = (*env)->GetMethodID(env, vpn_service_cls, "protect", "(I)Z");
    if (!protect) {
        (*env)->DeleteLocalRef(env, vpn_service_cls);
        return -1;
    }

    jboolean ok = (*env)->CallBooleanMethod(env, g_status->jobj_service, protect, fd);
    (*env)->DeleteLocalRef(env, vpn_service_cls);
    return ok ? 0 : -1;
}

static const char *encryption_mode_arg(const char *mode, const char *key) {
    if (!key || key[0] == '\0') {
        return "1";
    }
    if (mode && strcmp(mode, "Disable") == 0) {
        return "1";
    }
    if (mode && strcmp(mode, "AES-CBC") == 0) {
        return "3";
    }
    if (mode && strcmp(mode, "ChaCha20") == 0) {
        return "4";
    }
    if (mode && strcmp(mode, "Speck-CTR") == 0) {
        return "5";
    }
    return "4";
}

static int prefix_from_netmask(const char *netmask) {
    struct in_addr addr;
    uint32_t mask;
    int prefix = 0;

    if (!netmask || inet_aton(netmask, &addr) == 0) {
        return 24;
    }

    mask = ntohl(addr.s_addr);
    while (mask & 0x80000000U) {
        prefix++;
        mask <<= 1;
    }

    return prefix;
}

static int gateway_address_family(const char *gateway) {
    struct in_addr addr4;
    struct in6_addr addr6;

    if (!gateway || gateway[0] == '\0') {
        return AF_UNSPEC;
    }
    if (inet_pton(AF_INET, gateway, &addr4) == 1) {
        return AF_INET;
    }
    if (inet_pton(AF_INET6, gateway, &addr6) == 1) {
        return AF_INET6;
    }
    return AF_UNSPEC;
}

static void log_edge_v2_ipv6_command_line(int argc, char *argv[]) {
    char line[1024];
    size_t used = 0;
    int i;

    line[0] = '\0';
    for (i = 0; i < argc && used < sizeof(line); ++i) {
        const char *arg = argv[i] ? argv[i] : "";
        int written;

        if (i > 0 && argv[i - 1] &&
            (strcmp(argv[i - 1], "-c") == 0 || strcmp(argv[i - 1], "-k") == 0)) {
            arg = "***";
        }
        written = snprintf(line + used, sizeof(line) - used, "%s%s", i == 0 ? "" : " ", arg);
        if (written < 0) {
            break;
        }
        if ((size_t)written >= sizeof(line) - used) {
            used = sizeof(line) - 1;
            break;
        }
        used += (size_t)written;
    }
    traceEvent(TRACE_NORMAL, "command: %s", line);
}

int start_edge_v2_ipv6(n2n_edge_status_t *status) {
    char ip_arg[64];
    char mtu_arg[16];
    char local_port_arg[16];
    char route_arg[2][96];
    char trace_args[4][3] = {{0}};
    char *argv[48];
    int argc = 0;
    int i;
    int gateway_family;
    n2n_edge_cmd_t *cmd;
    FILE *log_file;

    if (!status) {
        return 1;
    }
    g_status = status;
    stop_requested = 0;
    cmd = &status->cmd;
    log_file = fopen(cmd->logpath, "a");
    if (log_file) {
        setvbuf(log_file, NULL, _IONBF, 0);
        dup2(fileno(log_file), STDOUT_FILENO);
        dup2(fileno(log_file), STDERR_FILENO);
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
    }

    pthread_mutex_lock(&g_status->mutex);
    g_status->running_status = EDGE_STAT_CONNECTING;
    pthread_mutex_unlock(&g_status->mutex);
    g_status->report_edge_status();

    /* v2.3_7.x: -a auto-detects address family, the "static:" mode prefix is no longer
     * parseable (it contains ':' and would be treated as an IPv6 address). */
    snprintf(ip_arg, sizeof(ip_arg), "%s/%d", cmd->ip_addr, prefix_from_netmask(cmd->ip_netmask));
    if (cmd->mtu > 0) {
        snprintf(mtu_arg, sizeof(mtu_arg), "%u", cmd->mtu);
    }
    snprintf(local_port_arg, sizeof(local_port_arg), "%u", cmd->local_port);

    argv[argc++] = "edge_v2_ipv6";
    argv[argc++] = "-f";
    argv[argc++] = "-d";
    argv[argc++] = "edge_ipv6";
    if (cmd->ip_mode == 0) {
        argv[argc++] = "-a";
        argv[argc++] = ip_arg;
    }
    if (cmd->local_ip[0] != '\0') {
        /* v2.3_7.x: IPv6 address is passed via -a (contains ':' so it is auto-detected);
         * -A was repurposed as the encryption mode selector. */
        argv[argc++] = "-a";
        argv[argc++] = cmd->local_ip;
    }
    argv[argc++] = "-c";
    argv[argc++] = cmd->community;
    if (cmd->http_tunnel) {
        argv[argc++] = "-6";
    } else if (cmd->re_resolve_supernode_ip) {
        argv[argc++] = "-4";
    }
    if (cmd->header_encryption) {
        argv[argc++] = "-w";
    }
    if (cmd->gaming_mode) {
        argv[argc++] = "-G";
    }
    argv[argc++] = "-A";
    argv[argc++] = (char *)encryption_mode_arg(cmd->encryption_mode, cmd->enc_key);
    /* v2.3_7.x bypass: -b enables the default port, or accepts an explicit port. */
    if (cmd->bypass_enabled) {
        argv[argc++] = "-b";
        if (cmd->bypass_port[0] != '\0') {
            argv[argc++] = cmd->bypass_port;
        }
    }
    if (cmd->enc_key && cmd->enc_key[0]) {
        argv[argc++] = "-k";
        argv[argc++] = cmd->enc_key;
    }
    for (i = 0; i < EDGE_CMD_SUPERNODES_NUM; ++i) {
        if (cmd->supernodes[i][0] != '\0') {
            argv[argc++] = "-l";
            argv[argc++] = cmd->supernodes[i];
        }
    }
    if (cmd->mac_addr[0] != '\0') {
        argv[argc++] = "-m";
        argv[argc++] = cmd->mac_addr;
    }
    if (cmd->mtu > 0) {
        argv[argc++] = "-M";
        argv[argc++] = mtu_arg;
    }
    if (cmd->local_port > 0) {
        argv[argc++] = "-p";
        argv[argc++] = local_port_arg;
    }
    gateway_family = gateway_address_family(cmd->gateway_ip);
    if (gateway_family == AF_INET) {
        snprintf(route_arg[0], sizeof(route_arg[0]), "0.0.0.0/1,%s", cmd->gateway_ip);
        snprintf(route_arg[1], sizeof(route_arg[1]), "128.0.0.0/1,%s", cmd->gateway_ip);
        argv[argc++] = "-R";
        argv[argc++] = route_arg[0];
        argv[argc++] = "-R";
        argv[argc++] = route_arg[1];
    } else if (gateway_family == AF_INET6) {
        snprintf(route_arg[0], sizeof(route_arg[0]), "::/1,%s", cmd->gateway_ip);
        snprintf(route_arg[1], sizeof(route_arg[1]), "8000::/1,%s", cmd->gateway_ip);
        argv[argc++] = "-R";
        argv[argc++] = route_arg[0];
        argv[argc++] = "-R";
        argv[argc++] = route_arg[1];
    }
    if (cmd->allow_routing) {
        argv[argc++] = "-r";
    }
    if (!cmd->drop_multicast) {
        argv[argc++] = "-E";
    }
    for (i = 2; i < cmd->trace_vlevel && i < 6; ++i) {
        strcpy(trace_args[i - 2], "-v");
        argv[argc++] = trace_args[i - 2];
    }
    argv[argc] = NULL;
    log_edge_v2_ipv6_command_line(argc, argv);
    if (android_stop_requested("command line prepared")) {
        if (log_file) {
            fclose(log_file);
        }
        return 0;
    }

    pthread_mutex_lock(&g_status->mutex);
    g_status->running_status = EDGE_STAT_CONNECTED;
    pthread_mutex_unlock(&g_status->mutex);
    g_status->report_edge_status();

    optind = 1;
    optarg = NULL;
    int ret;
    exit_trap_status = 1;
    exit_trap_enabled = 1;
    if (setjmp(exit_jmp) == 0) {
        ret = edge_v2_ipv6_main(argc, argv);
    } else {
        ret = exit_trap_status;
    }
    exit_trap_enabled = 0;
    pthread_mutex_lock(&g_status->mutex);
    g_status->running_status = ret ? EDGE_STAT_FAILED : EDGE_STAT_DISCONNECT;
    pthread_mutex_unlock(&g_status->mutex);
    g_status->report_edge_status();

    if (log_file) {
        fclose(log_file);
    }

    return ret;
}

int stop_edge_v2_ipv6(void) {
    stop_requested = 1;
    edge_v2_ipv6_request_stop();
    __android_log_write(ANDROID_LOG_INFO, "edge_v2_ipv6", "Stop requested.");

    int fd = open_socket(0, 0 /* bind LOOPBACK*/);
    struct sockaddr_in peer_addr;

    if (fd < 0) {
        return -1;
    }

    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_family = PF_INET;
    peer_addr.sin_port = htons(V2_IPV6_MGMT_PORT);
    peer_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendto(fd, "stop", 4, 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
    close(fd);
    return 0;
}

static int android_stop_requested(const char *stage) {
    if (!stop_requested) {
        return 0;
    }
    __android_log_print(ANDROID_LOG_INFO, "edge_v2_ipv6",
                        "Stop requested while connecting%s%s.",
                        stage ? " at " : "",
                        stage ? stage : "");
    return 1;
}
