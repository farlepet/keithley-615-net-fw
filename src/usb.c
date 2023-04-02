#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>

LOG_MODULE_REGISTER(kei_usb);

int kei_usb_init(void) {
    if(usb_enable(NULL)) {
        LOG_ERR("Failed to init USB");
        return -1;
    }

    LOG_INF("USB init done");

    return 0;
}
