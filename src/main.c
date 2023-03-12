#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(keithley615_comm, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/sntp.h>

#include <time.h>

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_PATH(leds, led_run), gpios);

static struct net_mgmt_event_callback mgmt_cb;
K_CONDVAR_DEFINE(net_ready_cond);
K_MUTEX_DEFINE(net_ready_mutex);


static void handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event, struct net_if *iface) {
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

        k_condvar_signal(&net_ready_cond);
    }
}



void main(void) {
    int            ret;
    struct net_if *iface;

    if (!gpio_is_ready_dt(&led)) {
        return;
    }

    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        return;
    }

    LOG_INF("Keithley 615 network interface board");

    net_mgmt_init_event_callback(&mgmt_cb, handler, NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&mgmt_cb);
    
    iface = net_if_get_default();
    net_dhcpv4_start(iface);

    k_condvar_wait(&net_ready_cond, &net_ready_mutex, K_FOREVER); // K_MSEC(15000)
    LOG_INF("DHCP completed, moving on to SNTP");

    {
        struct sntp_ctx    sntp;
        struct sntp_time   sntp_time;
        struct sockaddr_in sntp_addr;

#define NTP_PORT 123
#define NTP_ADDR "129.6.15.29"

        sntp_addr.sin_family = AF_INET;
        sntp_addr.sin_port   = htons(NTP_PORT);
        net_addr_pton(AF_INET, NTP_ADDR, &sntp_addr.sin_addr);

        ret = sntp_init(&sntp, (struct sockaddr *)&sntp_addr, sizeof(struct sockaddr_in));
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
        LOG_INF("UNIX time: %llu.%u", sntp_time.seconds, (unsigned)(((float)sntp_time.fraction * 1000) / ((1ULL << 32) - 1)));

        time_t time_s   = (time_t)sntp_time.seconds;
        struct tm *time = gmtime(&time_s);
        if(time) {
            LOG_INF("UTC: %04d-%02d-%02dT%02d:%02d:%02d",
                    time->tm_year + 1900, time->tm_mon + 1, time->tm_mday,
                    time->tm_hour, time->tm_min, time->tm_sec);
        }
sntp_end:
    }

#define SLEEP_TIME_MS (100)
    while(1) {
        ret = gpio_pin_toggle_dt(&led);
        if(ret < 0) {
            return;
        }
        k_msleep(SLEEP_TIME_MS);
    }
}
