#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/sntp.h>
#include <zephyr/shell/shell.h>

#include <time.h>

#include "net.h"

LOG_MODULE_REGISTER(kei_net);

static struct net_mgmt_event_callback _mgmt_cb;
K_CONDVAR_DEFINE(_net_ready_cond);
K_MUTEX_DEFINE(_net_ready_mutex);

struct net_if *_net_iface;

static void _net_ev_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event, struct net_if *iface) {
    int i = 0;

    if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
        return;
    }

    for (i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
        char buf[NET_IPV4_ADDR_LEN];

        if (iface->config.ip.ipv4->unicast[i].addr_type !=
                            NET_ADDR_DHCP) {
            continue;
        }

        LOG_INF("Your address: %s",
                net_addr_ntop(AF_INET,
                              &iface->config.ip.ipv4->unicast[i].address.in_addr,
                              buf, sizeof(buf)));
        LOG_INF("Lease time: %u seconds", iface->config.dhcpv4.lease_time);
        LOG_INF("Subnet: %s",
                net_addr_ntop(AF_INET,
                              &iface->config.ip.ipv4->netmask,
                              buf, sizeof(buf)));
        LOG_INF("Router: %s",
                net_addr_ntop(AF_INET,
                              &iface->config.ip.ipv4->gw,
                              buf, sizeof(buf)));

        k_condvar_signal(&_net_ready_cond);
    }
}

static void _net_thread_main(void *p1, void *p2, void *p3) {
    (void)p1; (void)p2; (void)p3;

    LOG_INF("Init");

    net_mgmt_init_event_callback(&_mgmt_cb, _net_ev_handler, NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&_mgmt_cb);
    
    _net_iface = net_if_get_default();
    if(_net_iface == NULL) {
        LOG_ERR("Net init failre");
        return;
    }
    
    time_t   time_s;
    unsigned time_us;
    
    if(kei_net_getaddr()) {
        LOG_ERR("Net failure");
        return;
    }

#if (CONFIG_SNTP)
    if (kei_net_sntp(&time_s, &time_us)) {
        LOG_ERR("SNTP failure");
        goto main_sntp_end;
    } else {
        LOG_INF("UNIX time: %llu.%06u", time_s, time_us);
    
        struct tm *time = gmtime(&time_s);
        if(time) {
            LOG_INF("UTC: %04d-%02d-%02dT%02d:%02d:%02d",
                    time->tm_year + 1900, time->tm_mon + 1, time->tm_mday,
                    time->tm_hour, time->tm_min, time->tm_sec);
        }
    }

main_sntp_end:
#endif /* (CONFIG_SNTP) */

    while(1) {
        k_msleep(1000);
    }
}
static K_THREAD_DEFINE(kei_net, 1536, _net_thread_main, NULL, NULL, NULL, 7, 0, 500);

int kei_net_getaddr(void) {
    LOG_INF("Starting DHCP");

    net_dhcpv4_start(_net_iface);
    int attempts = 0;
    while(++attempts < 4) {
        if(k_condvar_wait(&_net_ready_cond, &_net_ready_mutex, K_MSEC(16000))) {
            LOG_INF("Restarting DHCP");
            net_dhcpv4_restart(_net_iface);
        } else {
            break;
        }
    }
    if(attempts == 4) {
        LOG_ERR("DHCP failed");
        return -1;
    }

    LOG_INF("DHCP completed");

    return 0;
}

#if (CONFIG_SNTP)
int kei_net_sntp(time_t *time_s, unsigned *time_us) {
    int ret = 0;

    struct sntp_ctx    sntp;
    struct sntp_time   sntp_time;
    struct sockaddr_in sntp_addr;

    /* TODO: Make configurable */
#  define NTP_PORT 123
#  define NTP_ADDR "129.6.15.29"

    sntp_addr.sin_family = AF_INET;
    sntp_addr.sin_port   = htons(NTP_PORT);
    net_addr_pton(AF_INET, NTP_ADDR, &sntp_addr.sin_addr);

    ret = sntp_init(&sntp, (struct sockaddr *)&sntp_addr, sizeof(sntp_addr));
    if(ret < 0) {
        LOG_ERR("SNTP init failed: %d", ret);
        goto sntp_end;
    }

    int attempts = 0;
    while(++attempts < 6) {
        LOG_INF("Querying SNTP");
        ret = sntp_query(&sntp, 5000, &sntp_time);
        if(ret < 0) {
            LOG_ERR("SNTP request failed: %d", ret);
        } else {
            break;
        }
    }
    if(attempts == 6) {
        goto sntp_end;
    }

    LOG_INF("SNTP complete");

    if(time_s) {
        *time_s  = (time_t)sntp_time.seconds;
    }
    if(time_us) {
        *time_us = (unsigned)(((float)sntp_time.fraction * 1000000) / ((1ULL << 32) - 1));
    }

sntp_end:
    sntp_close(&sntp);

    return ret;
}
#endif /* (CONFIG_SNTP) */

static int _cmdhdlr_net_info(const struct shell *sh, size_t argc, char **argv);

SHELL_STATIC_SUBCMD_SET_CREATE(_subcmd_net,
    SHELL_CMD(info, NULL, "Print network info.", _cmdhdlr_net_info),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(kei_net, &_subcmd_net, "Network subcommands", NULL);

static int _cmdhdlr_net_info(const struct shell *sh, size_t argc, char **argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "test");

    return 0;
}

