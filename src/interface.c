#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "interface.h"

LOG_MODULE_REGISTER(kei_int, LOG_LEVEL_DBG);

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

#define N_DATA_BITS        13
#define N_RANGE_BITS        5
#define N_SENSITIVITY_BITS  2

static const struct {
    struct gpio_dt_spec polarity;
    struct gpio_dt_spec overload;
    struct gpio_dt_spec trigger;
    struct gpio_dt_spec print;
    struct gpio_dt_spec hold[2];

    struct gpio_dt_spec data_bcd       [N_DATA_BITS];
    struct gpio_dt_spec range_bcd      [N_RANGE_BITS];
    struct gpio_dt_spec sensitivity_bcd[N_SENSITIVITY_BITS];
} _int_gpios = {
    .polarity = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, polarity_gpios),
    .overload = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, overload_gpios),
    .trigger  = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, trigger_gpios),
    .print    = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, print_gpios),
    .hold     = {
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, hold_gpios, 0),
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, hold_gpios, 1)
    },
    .data_bcd      = {
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, data_gpios,  0),
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, data_gpios,  1),
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, data_gpios,  2),
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, data_gpios,  3),
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, data_gpios,  4),
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, data_gpios,  5),
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, data_gpios,  6),
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, data_gpios,  7),
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, data_gpios,  8),
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, data_gpios,  9),
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, data_gpios, 10),
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, data_gpios, 11),
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, data_gpios, 12)
    },
    .range_bcd      = {
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, range_gpios,  0),
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, range_gpios,  1),
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, range_gpios,  2),
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, range_gpios,  3),
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, range_gpios,  4)
    },
    .sensitivity_bcd      = {
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, sensitivity_gpios,  0),
        GPIO_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, sensitivity_gpios,  1),
    }
};


static struct {
    kei_interface_rawdata_t last_sample;
    kei_interface_mode_e    mode;

    struct gpio_callback print_gpio_callback;
} _data;

/**
 * @brief Read a whole value given a set of BCD inputs
 */
int _bcd_read(const struct gpio_dt_spec *gpios, int count) {
    unsigned value = 0;
    unsigned mul   = 1;
    while(count) {
        for(unsigned bit = 0; (bit < 4) && count; bit++, count--, gpios++) {
            int pin = gpio_pin_get_dt(gpios);
            if(pin < 0) {
                return -1;
            } else if(pin) {
                value += (1U << bit) * mul;
            }
        }
        mul *= 10;
    }
    return value;
}

/**
 * @brief Callback for print line going low
 */
static void _print_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    int data        = _bcd_read(_int_gpios.data_bcd,        N_DATA_BITS);
    int range       = _bcd_read(_int_gpios.range_bcd,       N_RANGE_BITS);
    int sensitivity = _bcd_read(_int_gpios.sensitivity_bcd, N_SENSITIVITY_BITS);

    if((data < 0) || (range < 0) || (sensitivity < 0)) {
        return;
    }

    if(gpio_pin_get_dt(&_int_gpios.polarity) == 1) {
        data *= -1;
    }

    _data.last_sample.value       = data;
    _data.last_sample.range       = range;
    _data.last_sample.sensitivity = sensitivity;
    _data.last_sample.flags       = 0;
    if(gpio_pin_get_dt(&_int_gpios.overload) == 1) {
        _data.last_sample.flags  |= KEI_DATAFLAG_OVERLOAD;
    }

    /* TODO: Timestamp sample */
}

int kei_interface_init(void) {
    memset(&_data, 0, sizeof(_data));

    if(gpio_pin_configure_dt(&_int_gpios.polarity, GPIO_INPUT)           ||
       gpio_pin_configure_dt(&_int_gpios.overload, GPIO_INPUT)           ||
       gpio_pin_configure_dt(&_int_gpios.trigger,  GPIO_OUTPUT_INACTIVE) ||
       gpio_pin_configure_dt(&_int_gpios.print,    GPIO_INPUT)           ||
       gpio_pin_configure_dt(&_int_gpios.hold[0],  GPIO_OUTPUT_INACTIVE) ||
       gpio_pin_configure_dt(&_int_gpios.hold[1],  GPIO_OUTPUT_INACTIVE)) {
        return -1;
    }

    for(unsigned i = 0; i < N_DATA_BITS; i++) {
        if(gpio_pin_configure_dt(&_int_gpios.data_bcd[i], GPIO_INPUT)) {
            return -1;
        }
    }
    for(unsigned i = 0; i < N_RANGE_BITS; i++) {
        if(gpio_pin_configure_dt(&_int_gpios.range_bcd[i], GPIO_INPUT)) {
            return -1;
        }
    }
    for(unsigned i = 0; i < N_SENSITIVITY_BITS; i++) {
        if(gpio_pin_configure_dt(&_int_gpios.sensitivity_bcd[i], GPIO_INPUT)) {
            return -1;
        }
    }

    LOG_INF("GPIOs initialized");


    if(gpio_pin_interrupt_configure_dt(&_int_gpios.print, GPIO_INT_EDGE_TO_INACTIVE)) {
        LOG_ERR("Failed to enable interrupts for print pin.");
        return -1;
    }

	gpio_init_callback(&_data.print_gpio_callback, _print_callback, BIT(_int_gpios.print.pin));
	if(gpio_add_callback(_int_gpios.print.port, &_data.print_gpio_callback)) {
        LOG_ERR("Failed to add callback for print pin.");
        return -1;
    }

    LOG_INF("Print interrupt enabled");

    return 0;
}


static const char *_unit_str[KEI_MODE_MAX] = {
    [KEI_MODE_NONE]     = "U",
    [KEI_MODE_VOLTS]    = "V",
    [KEI_MODE_OHMS]     = "ohms",
    [KEI_MODE_COULOMBS] = "C",
    [KEI_MODE_AMPERES]  = "A"
};

#define ABS(V) (((V) < 0) ? -(V) : (V))
int kei_interface_print(void) {
    kei_interface_data_t data;
    if (kei_interface_get_data(&data)) {
        return -1;
    }
    if(data.flags & KEI_DATAFLAG_OVERLOAD) {
        LOG_INF("data: OVERLOAD");
        return 0;
    }

    unsigned whole = ABS(data.value) / 1000000;
    unsigned frac  = (ABS(data.value) % 1000000) / 100;

    /* NOTE: If -1 < value < 0, the sign will not be present in the whole,
     * so we instead just print it explicitly. Avoiding floating-points to
     * ensure accuracy. Could also just print it as milli-whatever instead.
     * Could also use the polarity bit directly as a flag, to also enable
     * negative zero. */
    LOG_INF("data: %c%03u.%04u x 10^%+02hhd %s", 
            (data.value < 0) ? '-' : '+',
            whole, frac, data.range,
            _unit_str[_data.mode]);

    return 0;
}

int kei_interface_set_mode(kei_interface_mode_e mode) {
    if(mode >= KEI_MODE_MAX) {
        return -1;
    }

    _data.mode = mode;

    return 0;
}

int kei_interface_get_data(kei_interface_data_t *data) {
    if(!data) {
        return -1;
    }

    /* TODO: Ensure raw data doesn't get overwritted while we are reading it */
    memset(data, 0, sizeof(*data));

    /* NOTE: Currently all flags that apply to rawdata apply to data as well */
    data->flags = _data.last_sample.flags;

    if(data->flags & KEI_DATAFLAG_OVERLOAD) {
        return 0;
    }

    unsigned value = ABS(_data.last_sample.value);

    /* Convert base value to micro-units */
    value *= 100; /* Least-significant digit at sensitivity 0 is 100 micro-units */
    for(unsigned i = _data.last_sample.sensitivity; i > 0; i--) {
        value *= 10;
    }
    data->value = (_data.last_sample.value >= 0) ? value : -value;

    /* Determine sign power based on current mode */
    switch(_data.mode) {
        case KEI_MODE_VOLTS:
        case KEI_MODE_OHMS:
            data->range = _data.last_sample.range;
            break;
        case KEI_MODE_NONE:
        case KEI_MODE_COULOMBS:
        case KEI_MODE_AMPERES:
            data->range = -_data.last_sample.range;
            break;
    }

    return 0;
}

