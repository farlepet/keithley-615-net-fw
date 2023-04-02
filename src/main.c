#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(keithley615_comm, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <time.h>

#include "interface.h"
#include "net.h"
#include "usb.h"

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_PATH(leds, led_run), gpios);


void main(void) {
    int ret;

    if (!gpio_is_ready_dt(&led)) {
        return;
    }

    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        return;
    }

    LOG_INF("Keithley 615 network interface board");

    if(kei_interface_init()) {
        LOG_ERR("Interface failure");
    }
    kei_interface_read();

    kei_usb_init();

    time_t   time_s;
    unsigned time_us;
    if(kei_net_init()    ||
       kei_net_getaddr() ||
       kei_net_sntp(&time_s, &time_us)) {
        LOG_ERR("Net failure");
    } else {
        LOG_INF("UNIX time: %llu.%06u", time_s, time_us);
    
        struct tm *time = gmtime(&time_s);
        if(time) {
            LOG_INF("UTC: %04d-%02d-%02dT%02d:%02d:%02d",
                    time->tm_year + 1900, time->tm_mon + 1, time->tm_mday,
                    time->tm_hour, time->tm_min, time->tm_sec);
        }
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
