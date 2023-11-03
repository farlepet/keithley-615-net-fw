#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(keithley615_comm, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "interface.h"
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

    //kei_usb_init();

#define SLEEP_TIME_MS (500)
    while(1) {
        /* TODO: Create separete thread, disable printing by default. */
        //kei_interface_print();

        ret = gpio_pin_toggle_dt(&led);
        if(ret < 0) {
            return;
        }

        k_msleep(SLEEP_TIME_MS);
    }
}
