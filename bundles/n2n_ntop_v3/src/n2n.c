/**
 * (C) 2007-21 - ntop.org and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not see see <http://www.gnu.org/licenses/>
 *
 */
#include <curl/curl.h>
#include <regex.h> 
#include <ares.h>
#include <strings.h>
#include "n2n.h"

#include "sn_selection.h"

#include "minilzo.h"

#include <assert.h>

#define ns_c_in 1 
#define ns_t_txt 16

/* ************************************** */

SOCKET open_socket (int local_port, in_addr_t address, int type /* 0 = UDP, TCP otherwise */) {

    SOCKET sock_fd;
    struct sockaddr_in local_address;
    int sockopt;

    if((int)(sock_fd = socket(PF_INET, ((type == 0) ? SOCK_DGRAM : SOCK_STREAM) , 0)) < 0) {
        traceEvent(TRACE_ERROR, "Unable to create socket [%s][%d]\n",
                   strerror(errno), sock_fd);
        return(-1);
    }

#ifndef WIN32
    /* fcntl(sock_fd, F_SETFL, O_NONBLOCK); */
#endif

    sockopt = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&sockopt, sizeof(sockopt));

    memset(&local_address, 0, sizeof(local_address));
    local_address.sin_family = AF_INET;
    local_address.sin_port = htons(local_port);
    local_address.sin_addr.s_addr = htonl(address);

    if(bind(sock_fd,(struct sockaddr*) &local_address, sizeof(local_address)) == -1) {
        traceEvent(TRACE_ERROR, "Bind error on local port %u [%s]\n", local_port, strerror(errno));
        return(-1);
    }

    return(sock_fd);
}


static int traceLevel = 2 /* NORMAL */;
static int useSyslog = 0, syslog_opened = 0;
static FILE *traceFile = NULL;

int getTraceLevel () {

    return(traceLevel);
}

void setTraceLevel (int level) {

    traceLevel = level;
}

void setUseSyslog (int use_syslog) {

    useSyslog = use_syslog;
}

void setTraceFile (FILE *f) {

    traceFile = f;
}

void closeTraceFile () {

    if((traceFile != NULL) && (traceFile != stdout)) {
        fclose(traceFile);
    }
#ifndef WIN32
    if(useSyslog && syslog_opened) {
        closelog();
        syslog_opened = 0;
    }
#endif
}

#define N2N_TRACE_DATESIZE 32
void traceEvent (int eventTraceLevel, char* file, int line, char * format, ...) {

    va_list va_ap;

    if(traceFile == NULL) {
        traceFile = stdout;
    }

    if(eventTraceLevel <= traceLevel) {
        char buf[1024];
        char out_buf[1280];
        char theDate[N2N_TRACE_DATESIZE];
        char *extra_msg = "";
        time_t theTime = time(NULL);
        int i;

        /* We have two paths - one if we're logging, one if we aren't
         * Note that the no-log case is those systems which don't support it(WIN32),
         * those without the headers !defined(USE_SYSLOG)
         * those where it's parametrically off...
         */

        memset(buf, 0, sizeof(buf));
        strftime(theDate, N2N_TRACE_DATESIZE, "%d/%b/%Y %H:%M:%S", localtime(&theTime));

        va_start(va_ap, format);
        vsnprintf(buf, sizeof(buf) - 1, format, va_ap);
        va_end(va_ap);

        if(eventTraceLevel == 0 /* TRACE_ERROR */) {
            extra_msg = "ERROR: ";
        } else if(eventTraceLevel == 1 /* TRACE_WARNING */) {
            extra_msg = "WARNING: ";
        }

        while(buf[strlen(buf) - 1] == '\n') {
            buf[strlen(buf) - 1] = '\0';
        }

#ifndef WIN32
        if(useSyslog) {
            if(!syslog_opened) {
                openlog("n2n", LOG_PID, LOG_DAEMON);
                syslog_opened = 1;
            }

            snprintf(out_buf, sizeof(out_buf), "%s%s", extra_msg, buf);
            syslog(LOG_INFO, "%s", out_buf);
        } else {
            for(i = strlen(file) - 1; i > 0; i--) {
                if(file[i] == '/') {
                    i++;
                    break;
                }
            }
            snprintf(out_buf, sizeof(out_buf), "%s [%s:%d] %s%s", theDate, &file[i], line, extra_msg, buf);
            fprintf(traceFile, "%s\n", out_buf);
            fflush(traceFile);
        }
#else
        /* this is the WIN32 code */
        for(i = strlen(file) - 1; i > 0; i--) {
            if(file[i] == '\\') {
                i++;
                break;
            }
        }
        snprintf(out_buf, sizeof(out_buf), "%s [%s:%d] %s%s", theDate, &file[i], line, extra_msg, buf);
        fprintf(traceFile, "%s\n", out_buf);
        fflush(traceFile);
#endif
    }

}

/* *********************************************** */

/* addr should be in network order. Things are so much simpler that way. */
char* intoa (uint32_t /* host order */ addr, char* buf, uint16_t buf_len) {

    char *cp, *retStr;
    uint8_t byteval;
    int n;

    cp = &buf[buf_len];
    *--cp = '\0';

    n = 4;
    do {
        byteval = addr & 0xff;
        *--cp = byteval % 10 + '0';
        byteval /= 10;
        if(byteval > 0) {
            *--cp = byteval % 10 + '0';
            byteval /= 10;
            if(byteval > 0) {
                *--cp = byteval + '0';
            }
        }
        *--cp = '.';
        addr >>= 8;
    } while(--n > 0);

    /* Convert the string to lowercase */
    retStr = (char*)(cp + 1);

    return(retStr);
}


/** Convert subnet prefix bit length to host order subnet mask. */
uint32_t bitlen2mask (uint8_t bitlen) {

    uint8_t i;
    uint32_t mask = 0;

    for (i = 1; i <= bitlen; ++i) {
        mask |= 1 << (32 - i);
    }

    return mask;
}


/** Convert host order subnet mask to subnet prefix bit length. */
uint8_t mask2bitlen (uint32_t mask) {

    uint8_t i, bitlen = 0;

    for (i = 0; i < 32; ++i) {
        if((mask << i) & 0x80000000) {
            ++bitlen;
        } else {
            break;
        }
    }

    return bitlen;
}


/* *********************************************** */

char * macaddr_str (macstr_t buf,
                    const n2n_mac_t mac) {

    snprintf(buf, N2N_MACSTR_SIZE, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0] & 0xFF, mac[1] & 0xFF, mac[2] & 0xFF,
             mac[3] & 0xFF, mac[4] & 0xFF, mac[5] & 0xFF);

    return(buf);
}

/* *********************************************** */

/** Resolve the supernode IP address.
 *
 */
static struct {
            int done;             
            int success;          
            char txt_record[256]; 
} ctx = {0};
static void txt_query_callback(void *arg, int status, int timeouts, unsigned char *abuf, int alen) {
            if (status == ARES_SUCCESS) {
                struct ares_txt_reply *txt_out = NULL;
                if (ares_parse_txt_reply(abuf, alen, &txt_out) == ARES_SUCCESS && txt_out != NULL) {
                    snprintf(ctx.txt_record, sizeof(ctx.txt_record), "%s", txt_out->txt);
                    ctx.success = 1; 
                    ares_free_data(txt_out); 
                }
            }
            ctx.done = 1;
}
// 回调函数，处理 curl 获取到的数据
size_t write_callback(void *ptr, size_t size, size_t nmemb, char *data) {
    size_t total_size = size * nmemb;
    strncat(data, ptr, total_size);
    return total_size;
}

// 去除字符串开头的 "http://" 或 "https://" 和随后的 "/"
void strip_http_prefix(char *url) {
    if (strncasecmp(url, "http://", 7) == 0) {
        memmove(url, url + 7, strlen(url + 7) + 1);
    } else if (strncasecmp(url, "https://", 8) == 0) {
        memmove(url, url + 8, strlen(url + 8) + 1);
    }
    
    // 去除字符串开头的 /
    if (url[0] == '/') {
        memmove(url, url + 1, strlen(url + 1) + 1);
    }

    // 去除最后的 /
    size_t len = strlen(url);
    if (len > 1 && url[len - 1] == '/') {
        url[len - 1] = '\0';
    }
}

// 安全拷贝字符串
static void safe_strncpy(char *dst, const char *src, size_t dst_size) {
    if (dst_size == 0) return;
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}
int supernode2sock (n2n_sock_t *sn, const n2n_sn_name_t addrIn) {

    n2n_sn_name_t addr;
    char *supernode_host;
    char *supernode_port;
    int rv = 0;
    int nameerr;
    const struct addrinfo aihints = {0, PF_INET, 0, 0, 0, NULL, NULL, NULL};
    struct addrinfo * ainfo = NULL;
    struct sockaddr_in * saddr;

    sn->family = AF_INVALID;

    memcpy(addr, addrIn, N2N_EDGE_SN_HOST_SIZE);
    if (strncasecmp(addr, "txt:", 4) == 0) {
        // 处理txt查询
        const char *domain = addr + (strncasecmp(addr, "txt://", 6) == 0 ? 6 : 4);
        ares_channel channel;
        struct ares_options options;
        int optmask = 0;
        int status;

        if ((status = ares_library_init(ARES_LIB_INIT_ALL)) != ARES_SUCCESS) {
            traceEvent(TRACE_ERROR, "Failed to initialize c-ares: %d", status);
            return -1;
        }

        if ((status = ares_init_options(&channel, &options, optmask)) != ARES_SUCCESS) {
            traceEvent(TRACE_ERROR, "Failed to initialize c-ares channel: %d", status);
            ares_library_cleanup();
            return -1;
        }

        const char *servers = "119.29.29.29,114.114.114.114,8.8.8.8";
        ares_set_servers_csv(channel, servers);

        ares_query(channel, domain, ns_c_in, ns_t_txt, txt_query_callback, &ctx);
        // traceEvent(TRACE_NORMAL, "Initiate TXT query: '%s'", domain);

        fd_set read_fds, write_fds;
        struct timeval *tvp, tv, timeout;
        int nfds;
        int max_wait_ms = 5000; // 最大等待5秒

        timeout.tv_sec = max_wait_ms / 1000;
        timeout.tv_usec = (max_wait_ms % 1000) * 1000;

        while (!ctx.done) {
            FD_ZERO(&read_fds);
            FD_ZERO(&write_fds);
            nfds = ares_fds(channel, &read_fds, &write_fds);
            if (nfds == 0) {
                break; 
            }

            tvp = ares_timeout(channel, &timeout, &tv);
            int res = select(nfds, &read_fds, &write_fds, NULL, tvp);
            if (res < 0) {
                traceEvent(TRACE_ERROR, "Select failed, terminating TXT query");
                break;
            }
            ares_process(channel, &read_fds, &write_fds);
        }

        ares_destroy(channel);
        ares_library_cleanup();

        if (ctx.success) {
            snprintf(addr, sizeof(addr), "%s", ctx.txt_record);
            // traceEvent(TRACE_NORMAL, "TXT record query successful, the address is: %s", addr);
        } else {
            traceEvent(TRACE_ERROR, "TXT record query failed");
            return -1;
        }
    }
    // 检查是否以 http 或 https 开头
    if (strncasecmp(addr, "http:", 5) == 0 || strncasecmp(addr, "https:", 6) == 0) {
        char result[8192] = {0};
        CURL *curl;
        CURLcode res;
        char redirect_url[512] = {0};
	// 如果 addr 以 http: 或 https: 开头但没有 //
	if (strncasecmp(addr, "http:", 5) == 0 && strncasecmp(addr, "http://", 7) != 0) {
    		char fixed[512] = {0};
    		snprintf(fixed, sizeof(fixed), "http://%s", addr + 5); // 添加 //
    		safe_strncpy(addr, fixed, sizeof(addr));
	} else if (strncasecmp(addr, "https:", 6) == 0 && strncasecmp(addr, "https://", 8) != 0) {
    		char fixed[512] = {0};
    		snprintf(fixed, sizeof(fixed), "https://%s", addr + 6); // 添加 //
    		safe_strncpy(addr, fixed, sizeof(addr));
	}
        // 初始化 libcurl
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl = curl_easy_init();
        if (!curl) {
            traceEvent(TRACE_ERROR, "Unable to initialize curl");
            curl_global_cleanup();
            return -1;
        }
	
        // 设置 curl 请求参数（优先使用 IPv4）
    curl_easy_setopt(curl, CURLOPT_URL, addr); // 设置 URL
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L); // 不自动跟随重定向
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback); // 设置写入回调函数
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, result); // 设置写入结果的缓冲区
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4); // 强制使用 IPv4
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // 等同 curl -k，不校验证书链
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L); // 等同 curl -k，不校验主机名
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L); // 设置超时时间为 5 秒

    // 执行 IPv4 请求
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        traceEvent(TRACE_ERROR, "IPv4 request failed: %s, attempting IPv6", curl_easy_strerror(res));
        curl_easy_cleanup(curl); // 清理 curl 对象

        // ---------- 尝试使用 IPv6 ----------
        curl = curl_easy_init(); // 重新初始化 curl
        if (!curl) {
            traceEvent(TRACE_ERROR, "Unable to initialize curl (IPv6)");
            curl_global_cleanup();
            return -1;
        }

        // 设置 curl 请求参数（IPv6）
        curl_easy_setopt(curl, CURLOPT_URL, addr);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, result);
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6); // 改为 IPv6
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // 等同 curl -k，不校验证书链
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L); // 等同 curl -k，不校验主机名
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L); // 设置超时时间为 5 秒

        // 执行 IPv6 请求
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            traceEvent(TRACE_ERROR, "IPv6 request also failed: %s", curl_easy_strerror(res));
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return -1;
        }
    }

        // 获取HTTP状态码
        long status_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);

        if (status_code >= 300 && status_code < 400) {
            // 处理 3xx 重定向
            char *location = NULL;
            res = curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &location); // 获取重定向URL
            if (res != CURLE_OK || location == NULL) {
                traceEvent(TRACE_ERROR, "Failed to retrieve the redirect address");
                curl_easy_cleanup(curl);
                curl_global_cleanup();
                return -1;
            }

            strncpy(redirect_url, location, sizeof(redirect_url) - 1);
            redirect_url[sizeof(redirect_url) - 1] = '\0'; // 保证字符串以 '\0' 结尾
            safe_strncpy(addr, redirect_url, sizeof(addr));
            strip_http_prefix(addr); // 去掉http://或https://前缀
            //traceEvent(TRACE_NORMAL, "edirect address detected: %s", addr);
        } else if (status_code == 200) {
            // 处理 200 OK，获取正文内容
	    char *body = strstr(result, "\r\n\r\n");
	    if (!body) {
    		body = strstr(result, "\n\n");
    		if (body) {
        		body += 2;
    		} else {
        		body = result; // 没有HTTP头部，用全体内容
    		}
	    } else {
    		body += 4;
	    }

            if (body) {
                // 将正文内容去掉所有换行符，存入 clean_addr
                char clean_addr[512] = {0};
                int j = 0;
                for (int i = 0; body[i] != '\0' && j < sizeof(clean_addr) - 1; i++) {
                    if (body[i] != '\r' && body[i] != '\n') {
                        clean_addr[j++] = body[i];
                    }
                }
                clean_addr[sizeof(clean_addr) - 1] = '\0'; 
                // 更新 addr 变量
                safe_strncpy(addr, clean_addr, sizeof(addr));

                // 去掉 http:// 或 https:// 前缀
                strip_http_prefix(addr);
                // 打印成功日志
                //traceEvent(TRACE_NORMAL, "HTTP 200 webpage body address: %s", addr);
            } else {
                traceEvent(TRACE_ERROR, "HTTP body content not found");
                curl_easy_cleanup(curl);
                curl_global_cleanup();
                return -1;
            }
        } else {
            traceEvent(TRACE_ERROR, "Unexpected status code: %ld", status_code);
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return -1;
        }

        // 清理curl
        curl_easy_cleanup(curl);
        curl_global_cleanup();
    }
    supernode_host = strtok(addr, ":");

    if(supernode_host) {
        supernode_port = strtok(NULL, ":");
        traceEvent(TRACE_NORMAL, "Server address: %s:%s", supernode_host, supernode_port);
        if(supernode_port) {
            sn->port = atoi(supernode_port);
            nameerr = getaddrinfo(supernode_host, NULL, &aihints, &ainfo);
            if(0 == nameerr) {
               /* ainfo s the head of a linked list if non-NULL. */
                if(ainfo && (PF_INET == ainfo->ai_family)) {
                    /* It is definitely and IPv4 address -> sockaddr_in */
                    saddr = (struct sockaddr_in *)ainfo->ai_addr;
                    memcpy(sn->addr.v4, &(saddr->sin_addr.s_addr), IPV4_SIZE);
                    sn->family = AF_INET;
                    traceEvent(TRACE_INFO, "supernode2sock successfully resolves supernode IPv4 address for %s", supernode_host);
                    rv = 0;
                } else {
                    /* Should only return IPv4 addresses due to aihints. */
                    traceEvent(TRACE_WARNING, "supernode2sock fails to resolve supernode IPv4 address for %s", supernode_host);
                    rv = -1;
                }
                freeaddrinfo(ainfo); /* free everything allocated by getaddrinfo(). */
            } else {
                traceEvent(TRACE_WARNING, "supernode2sock fails to resolve supernode host %s, %d: %s", supernode_host, nameerr, gai_strerror(nameerr));
                rv = -2;
            }
        } else {
            traceEvent(TRACE_WARNING, "supernode2sock sees malformed supernode parameter (-l <host:port>) %s", addrIn);
            rv = -3;
        }
    } else {
        traceEvent(TRACE_WARNING, "supernode2sock sees malformed supernode parameter (-l <host:port>) %s",
                   addrIn);
        rv = -4;
    }

    ainfo = NULL;

    return rv;
}


N2N_THREAD_RETURN_DATATYPE resolve_thread(N2N_THREAD_PARAMETER_DATATYPE p) {

#ifdef HAVE_PTHREAD
    n2n_resolve_parameter_t *param = (n2n_resolve_parameter_t*)p;
    n2n_resolve_ip_sock_t   *entry, *tmp_entry;
    time_t                  rep_time = N2N_RESOLVE_INTERVAL / 10;
    time_t                  now;

    while(1) {
        sleep(N2N_RESOLVE_INTERVAL / 60); /* wake up in-between to check for signaled requests */

        // what's the time?
        now = time(NULL);

        // lock access
        pthread_mutex_lock(&param->access);

        // is it time to resolve yet?
        if(((param->request)) || ((now - param->last_resolved) > rep_time)) {
            HASH_ITER(hh, param->list, entry, tmp_entry) {
                // resolve
                entry->error_code = supernode2sock(&entry->sock, entry->org_ip);
                // if socket changed and no error
                if(!sock_equal(&entry->sock, entry->org_sock)
                  && (!entry->error_code)) {
                    // flag the change
                    param->changed = 1;
               }
            }
            param->last_resolved = now;

            // any request fulfilled
            param->request = 0;

            // determine next resolver repetition (shorter time if resolver errors occured)
            rep_time = N2N_RESOLVE_INTERVAL;
            HASH_ITER(hh, param->list, entry, tmp_entry) {
                if(entry->error_code) {
                    rep_time = N2N_RESOLVE_INTERVAL / 10;
                    break;
                }
            }
        }

        // unlock access
        pthread_mutex_unlock(&param->access);
    }
#endif
}


int resolve_create_thread (n2n_resolve_parameter_t **param, struct peer_info *sn_list) {

#ifdef HAVE_PTHREAD
    struct peer_info        *sn, *tmp_sn;
    n2n_resolve_ip_sock_t   *entry;
    int                     ret;

    // create parameter structure
    *param = (n2n_resolve_parameter_t*)calloc(1, sizeof(n2n_resolve_parameter_t));
    if(*param) {
        HASH_ITER(hh, sn_list, sn, tmp_sn) {
            // create entries for those peers that come with ip_addr string (from command-line)
            if(sn->ip_addr) {
                entry = (n2n_resolve_ip_sock_t*)calloc(1, sizeof(n2n_resolve_ip_sock_t));
                if(entry) {
                    entry->org_ip = sn->ip_addr;
                    entry->org_sock = &(sn->sock);
                    memcpy(&(entry->sock), &(sn->sock), sizeof(n2n_sock_t));
                    HASH_ADD(hh, (*param)->list, org_ip, sizeof(char*), entry);
                } else
                    traceEvent(TRACE_WARNING, "resolve_create_thread was unable to add list entry for supernode '%s'", sn->ip_addr);
            }
        }
        (*param)->check_interval = N2N_RESOLVE_CHECK_INTERVAL;
    } else {
        traceEvent(TRACE_WARNING, "resolve_create_thread was unable to create list of supernodes");
        return -1;
    }

    // create thread
    ret = pthread_create(&((*param)->id), NULL, resolve_thread, (void *)*param);
    if(ret) {
        traceEvent(TRACE_WARNING, "resolve_create_thread failed to create resolver thread with error number %d", ret);
        return -1;
    }

    pthread_mutex_init(&((*param)->access), NULL);

    return 0;
#endif
}


void resolve_cancel_thread (n2n_resolve_parameter_t *param) {

#ifdef HAVE_PTHREAD
    pthread_cancel(param->id);
    free(param);
#endif
}


uint8_t resolve_check (n2n_resolve_parameter_t *param, uint8_t requires_resolution, time_t now) {

    uint8_t ret = requires_resolution; /* if trylock fails, it still requires resolution */

#ifdef HAVE_PTHREAD    
    n2n_resolve_ip_sock_t   *entry, *tmp_entry;
    n2n_sock_str_t sock_buf;

    if(NULL == param)
        return ret;
    
    // check_interval and last_check do not need to be guarded by the mutex because
    // their values get changed and evaluated only here

    if((now - param->last_checked > param->check_interval) || (requires_resolution)) {
        // try to lock access
        if(pthread_mutex_trylock(&param->access) == 0) {
            // any changes?
            if(param->changed) {
                // reset flag
                param->changed = 0;
                // unselectively copy all socks (even those with error code, that would be the old one because
                // sockets do not get overwritten in case of error in resolve_thread) from list to supernode list
                HASH_ITER(hh, param->list, entry, tmp_entry) {
                    memcpy(entry->org_sock, &entry->sock, sizeof(n2n_sock_t));
                    traceEvent(TRACE_INFO, "resolve_check renews ip address of supernode '%s' to %s",
                                           entry->org_ip, sock_to_cstr(sock_buf, &(entry->sock)));
               }
            }

            // let the resolver thread know eventual difficulties in reaching the supernode
            if(requires_resolution) {
                param->request = 1;
                ret = 0;
            }

            param->last_checked = now;

            // next appointment
            if(param->request)
                // earlier if resolver still working on fulfilling a request
                param->check_interval = N2N_RESOLVE_CHECK_INTERVAL / 10;
            else
                param->check_interval = N2N_RESOLVE_CHECK_INTERVAL;

            // unlock access
            pthread_mutex_unlock(&param->access);
        }
    }
#endif

    return ret;
}


/* ************************************** */


struct peer_info* add_sn_to_list_by_mac_or_sock (struct peer_info **sn_list, n2n_sock_t *sock, const n2n_mac_t mac, int *skip_add) {

    struct peer_info *scan, *tmp, *peer = NULL;

    if(!is_null_mac(mac)) { /* not zero MAC */
        HASH_FIND_PEER(*sn_list, mac, peer);
    }

    if(peer == NULL) { /* zero MAC, search by socket */
        HASH_ITER(hh, *sn_list, scan, tmp) {
            if(memcmp(&(scan->sock), sock, sizeof(n2n_sock_t)) == 0) {
                // update mac if appropriate, needs to be deleted first because it is key to the hash list
                if(!is_null_mac(mac)) {
                    HASH_DEL(*sn_list, scan);
                    memcpy(scan->mac_addr, mac, sizeof(n2n_mac_t));
                    HASH_ADD_PEER(*sn_list, scan);
                }
                peer = scan;
                break;
            }
        }

        if((peer == NULL) && (*skip_add == SN_ADD)) {
            peer = (struct peer_info*)calloc(1, sizeof(struct peer_info));
            if(peer) {
                sn_selection_criterion_default(&(peer->selection_criterion));
                peer->last_valid_time_stamp = initial_time_stamp();
                memcpy(&(peer->sock), sock, sizeof(n2n_sock_t));
                memcpy(peer->mac_addr, mac, sizeof(n2n_mac_t));
                HASH_ADD_PEER(*sn_list, peer);
                *skip_add = SN_ADD_ADDED;
            }
        }
    }

    return peer;
}

/* ************************************************ */


/* http://www.faqs.org/rfcs/rfc908.html */
uint8_t is_multi_broadcast (const n2n_mac_t dest_mac) {

    int is_broadcast = (memcmp(broadcast_mac, dest_mac, N2N_MAC_SIZE) == 0);
    int is_multicast = (memcmp(multicast_mac, dest_mac, 3) == 0) && !(dest_mac[3] >> 7);
    int is_ipv6_multicast = (memcmp(ipv6_multicast_mac, dest_mac, 2) == 0);

    return is_broadcast || is_multicast || is_ipv6_multicast;
}


uint8_t is_broadcast (const n2n_mac_t dest_mac) {

    int is_broadcast = (memcmp(broadcast_mac, dest_mac, N2N_MAC_SIZE) == 0);

    return is_broadcast;
}


uint8_t is_null_mac (const n2n_mac_t dest_mac) {

    int is_null_mac = (memcmp(null_mac, dest_mac, N2N_MAC_SIZE) == 0);

    return is_null_mac;
}


/* *********************************************** */

char* msg_type2str (uint16_t msg_type) {

    switch(msg_type) {
        case MSG_TYPE_REGISTER: return("MSG_TYPE_REGISTER");
        case MSG_TYPE_DEREGISTER: return("MSG_TYPE_DEREGISTER");
        case MSG_TYPE_PACKET: return("MSG_TYPE_PACKET");
        case MSG_TYPE_REGISTER_ACK: return("MSG_TYPE_REGISTER_ACK");
        case MSG_TYPE_REGISTER_SUPER: return("MSG_TYPE_REGISTER_SUPER");
        case MSG_TYPE_REGISTER_SUPER_ACK: return("MSG_TYPE_REGISTER_SUPER_ACK");
        case MSG_TYPE_REGISTER_SUPER_NAK: return("MSG_TYPE_REGISTER_SUPER_NAK");
        case MSG_TYPE_FEDERATION: return("MSG_TYPE_FEDERATION");
        default: return("???");
    }

    return("???");
}

/* *********************************************** */

void hexdump (const uint8_t *buf, size_t len) {

    size_t i;

    if(0 == len) {
        return;
    }

    printf("-----------------------------------------------\n");
    for(i = 0; i < len; i++) {
        if((i > 0) && ((i % 16) == 0)) {
            printf("\n");
        }
        printf("%02X ", buf[i] & 0xFF);
    }
    printf("\n");
    printf("-----------------------------------------------\n");
}


/* *********************************************** */

void print_n2n_version () {

    printf("Welcome to n2n v.%s for %s\n"
           "Built on %s\n"
           "Copyright 2007-2021 - ntop.org and contributors\n\n",
           GIT_RELEASE, PACKAGE_OSNAME, PACKAGE_BUILDDATE);
}

/* *********************************************** */

size_t purge_expired_nodes (struct peer_info **peer_list,
                            SOCKET socket_not_to_close,
                            n2n_tcp_connection_t **tcp_connections,
                            time_t *p_last_purge,
                            int frequency, int timeout) {

    time_t now = time(NULL);
    size_t num_reg = 0;

    if((now - (*p_last_purge)) < frequency) {
        return 0;
    }

    traceEvent(TRACE_DEBUG, "Purging old registrations");

    num_reg = purge_peer_list(peer_list, socket_not_to_close, tcp_connections, now - timeout);

    (*p_last_purge) = now;
    traceEvent(TRACE_DEBUG, "Remove %ld registrations", num_reg);

    return num_reg;
}

/** Purge old items from the peer_list, eventually close the related socket, and
  * return the number of items that were removed. */
size_t purge_peer_list (struct peer_info **peer_list,
                        SOCKET socket_not_to_close,
                        n2n_tcp_connection_t **tcp_connections,
                        time_t purge_before) {

    struct peer_info *scan, *tmp;
    n2n_tcp_connection_t *conn;
    size_t retval = 0;

    HASH_ITER(hh, *peer_list, scan, tmp) {
        if((scan->purgeable == SN_PURGEABLE) && (scan->last_seen < purge_before)) {
            if((scan->socket_fd >=0) && (scan->socket_fd != socket_not_to_close)) {
                if(tcp_connections) {
                    HASH_FIND_INT(*tcp_connections, &scan->socket_fd, conn);
                    if(conn) {
                        HASH_DEL(*tcp_connections, conn);
                        free(conn);
                    }
                    shutdown(scan->socket_fd, SHUT_RDWR);
                    closesocket(scan->socket_fd);
                }
            }
            HASH_DEL(*peer_list, scan);
            retval++;
            free(scan);
        }
    }

    return retval;
}

/** Purge all items from the peer_list and return the number of items that were removed. */
size_t clear_peer_list (struct peer_info ** peer_list) {

    struct peer_info *scan, *tmp;
    size_t retval = 0;

    HASH_ITER(hh, *peer_list, scan, tmp) {
        HASH_DEL(*peer_list, scan);
        retval++;
        free(scan);
    }

    return retval;
}

static uint8_t hex2byte (const char * s) {

    char tmp[3];
    tmp[0] = s[0];
    tmp[1] = s[1];
    tmp[2] = 0; /* NULL term */

    return((uint8_t)strtol(tmp, NULL, 16));
}

extern int str2mac (uint8_t * outmac /* 6 bytes */, const char * s) {

    size_t i;

    /* break it down as one case for the first "HH", the 5 x through loop for
     * each ":HH" where HH is a two hex nibbles in ASCII. */

    *outmac = hex2byte(s);
    ++outmac;
    s += 2; /* don't skip colon yet - helps generalise loop. */

    for(i = 1; i < 6; ++i) {
        s += 1;
        *outmac = hex2byte(s);
        ++outmac;
        s += 2;
    }

    return 0; /* ok */
}

extern char * sock_to_cstr (n2n_sock_str_t out,
                            const n2n_sock_t * sock) {

    if(NULL == out) {
        return NULL;
    }
    memset(out, 0, N2N_SOCKBUF_SIZE);

    if(AF_INET6 == sock->family) {
        /* INET6 not written yet */
        snprintf(out, N2N_SOCKBUF_SIZE, "XXXX:%hu", sock->port);
        return out;
    } else {
        const uint8_t * a = sock->addr.v4;

        snprintf(out, N2N_SOCKBUF_SIZE, "%hu.%hu.%hu.%hu:%hu",
                 (unsigned short)(a[0] & 0xff),
                 (unsigned short)(a[1] & 0xff),
                 (unsigned short)(a[2] & 0xff),
                 (unsigned short)(a[3] & 0xff),
                 (unsigned short)sock->port);
        return out;
    }
}

char *ip_subnet_to_str (dec_ip_bit_str_t buf, const n2n_ip_subnet_t *ipaddr) {

    snprintf(buf, sizeof(dec_ip_bit_str_t), "%hhu.%hhu.%hhu.%hhu/%hhu",
             (uint8_t) ((ipaddr->net_addr >> 24) & 0xFF),
             (uint8_t) ((ipaddr->net_addr >> 16) & 0xFF),
             (uint8_t) ((ipaddr->net_addr >> 8) & 0xFF),
             (uint8_t) (ipaddr->net_addr & 0xFF),
             ipaddr->net_bitlen);

    return buf;
}


/* @return 1 if the two sockets are equivalent. */
int sock_equal (const n2n_sock_t * a,
                const n2n_sock_t * b) {

    if(a->port != b->port) {
        return(0);
    }

    if(a->family != b->family) {
        return(0);
    }

    switch(a->family) {
        case AF_INET:
            if(memcmp(a->addr.v4, b->addr.v4, IPV4_SIZE)) {
                return(0);
            }
            break;

        default:
            if(memcmp(a->addr.v6, b->addr.v6, IPV6_SIZE)) {
                return(0);
            }
            break;
    }

    /* equal */
    return(1);
}


/* *********************************************** */

// fills a specified memory area with random numbers
int memrnd (uint8_t *address, size_t len) {

    for(; len >= 4; len -= 4) {
        *(uint32_t*)address = n2n_rand();
        address += 4;
    }

    for(; len > 0; len--) {
        *address = n2n_rand();
        address++;
    }

    return 0;
}


// exclusive-ors a specified memory area with another
int memxor (uint8_t *destination, const uint8_t *source, size_t len) {

    for(; len >= 4; len -= 4) {
        *(uint32_t*)destination ^= *(uint32_t*)source;
        source += 4;
        destination += 4;
    }

    for(; len > 0; len--) {
        *destination ^= *source;
        source++;
        destination++;
    }

    return 0;
}

/* *********************************************** */

#if defined(WIN32)
int gettimeofday (struct timeval *tp, void *tzp) {

    time_t clock;
    struct tm tm;
    SYSTEMTIME wtm;

    GetLocalTime(&wtm);
    tm.tm_year = wtm.wYear - 1900;
    tm.tm_mon = wtm.wMonth - 1;
    tm.tm_mday = wtm.wDay;
    tm.tm_hour = wtm.wHour;
    tm.tm_min = wtm.wMinute;
    tm.tm_sec = wtm.wSecond;
    tm.tm_isdst = -1;
    clock = mktime(&tm);
    tp->tv_sec = clock;
    tp->tv_usec = wtm.wMilliseconds * 1000;

    return 0;
}
#endif


// stores the previously issued time stamp
static uint64_t previously_issued_time_stamp = 0;


// returns a time stamp for use with replay protection (branchless code)
//
// depending on the self-detected accuracy, it has the following format
//
// MMMMMMMMCCCCCCCF or
//
// MMMMMMMMSSSSSCCF
//
// with M being the 32-bit second time stamp
//      S       the 20-bit sub-second (microsecond) time stamp part, if applicable
//      C       a counter (8 bit or 24 bit) reset to 0 with every MMMMMMMM(SSSSS) turn-over
//      F       a 4-bit flag field with
//      ...c    being the accuracy indicator (if set, only counter and no sub-second accuracy)
//
uint64_t time_stamp (void) {

    struct timeval tod;
    uint64_t micro_seconds;
    uint64_t co, mask_lo, mask_hi, hi_unchanged, counter, new_co;

    gettimeofday(&tod, NULL);

    // (roughly) calculate the microseconds since 1970, leftbound
    micro_seconds = ((uint64_t)(tod.tv_sec) << 32) + ((uint64_t)tod.tv_usec << 12);
    // more exact but more costly due to the multiplication:
    // micro_seconds = ((uint64_t)(tod.tv_sec) * 1000000ULL + tod.tv_usec) << 12;

    // extract "counter only" flag (lowest bit)
    co = (previously_issued_time_stamp << 63) >> 63;
    // set mask accordingly
    mask_lo   = -co;
    mask_lo >>= 32;
    // either 0x00000000FFFFFFFF (if co flag set) or 0x0000000000000000 (if co flag not set)

    mask_lo  |= (~mask_lo) >> 52;
    // either 0x00000000FFFFFFFF (unchanged)      or 0x0000000000000FFF (lowest 12 bit set)

    mask_hi   = ~mask_lo;

    hi_unchanged = ((previously_issued_time_stamp & mask_hi) == (micro_seconds & mask_hi));
    // 0 if upper bits unchanged (compared to previous stamp), 1 otherwise

    // read counter and shift right for flags
    counter   = (previously_issued_time_stamp & mask_lo) >> 4;

    counter  += hi_unchanged;
    counter  &= -hi_unchanged;
    // either counter++ if upper part of timestamp unchanged, 0 otherwise

    // back to time stamp format
    counter <<= 4;

    // set new co flag if counter overflows while upper bits unchanged or if it was set before
    new_co   = (((counter & mask_lo) == 0) & hi_unchanged) | co;

    // in case co flag changed, masks need to be recalculated
    mask_lo   = -new_co;
    mask_lo >>= 32;
    mask_lo  |= (~mask_lo) >> 52;
    mask_hi   = ~mask_lo;

    // assemble new timestamp
    micro_seconds &= mask_hi;
    micro_seconds |= counter;
    micro_seconds |= new_co;

    previously_issued_time_stamp = micro_seconds;

    return micro_seconds;
}


// returns an initial time stamp for use with replay protection
uint64_t initial_time_stamp (void) {

    return time_stamp() - TIME_STAMP_FRAME;
}


// checks if a provided time stamp is consistent with current time and previously valid time stamps
// and, in case of validity, updates the "last valid time stamp"
int time_stamp_verify_and_update (uint64_t stamp, uint64_t *previous_stamp, int allow_jitter) {

    int64_t diff; /* do not change to unsigned */
    uint64_t co;  /* counter only mode (for sub-seconds) */

    co = (stamp << 63) >> 63;

    // is it around current time (+/- allowed deviation TIME_STAMP_FRAME)?
    diff = stamp - time_stamp();
    // abs()
    diff = (diff < 0 ? -diff : diff);
    if(diff >= TIME_STAMP_FRAME) {
        traceEvent(TRACE_DEBUG, "time_stamp_verify_and_update found a timestamp out of allowed frame.");
        return 0; // failure
    }

    // if applicable: is it higher than previous time stamp (including allowed deviation of TIME_STAMP_JITTER)?
    if(NULL != previous_stamp) {
        diff = stamp - *previous_stamp;
        if(allow_jitter) {
            // 8 times higher jitter allowed for counter-only flagged timestamps ( ~ 1.25 sec with 160 ms default jitter)
            diff += TIME_STAMP_JITTER << (co << 3);
        }

        if(diff <= 0) {
            traceEvent(TRACE_DEBUG, "time_stamp_verify_and_update found a timestamp too old compared to previous.");
            return 0; // failure
        }
        // for not allowing to exploit the allowed TIME_STAMP_JITTER to "turn the clock backwards",
        // set the higher of the values
        *previous_stamp = (stamp > *previous_stamp ? stamp : *previous_stamp);
    }

    return 1; // success
}
