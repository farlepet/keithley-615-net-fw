#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

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
    kei_interface_rawdata_t last_sample;    /**< Last sample received from instrument */
    kei_interface_mode_e    mode;           /**< Current instrument mode/units */

    struct {
        kei_interface_trigmode_e mode;      /**< Current trigger mode */
        uint32_t                 period_ms; /**< Trigger period, in ms, when using periodic trigger */
    } trig;

    struct gpio_callback print_gpio_callback;

    struct k_thread  thread;

    struct k_condvar data_ready_cond;
    struct k_mutex   data_ready_mutex;
} _data;

static int kei_interface_trigger(int wait);

/**
 * @brief Read a whole value given a set of BCD inputs
 */
static int _bcd_read(const struct gpio_dt_spec *gpios, int count) {
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

    k_condvar_signal(&_data.data_ready_cond);
}

K_THREAD_STACK_DEFINE(_kei_thread_stack, 1024);
static void _kei_thread_main(void *p1, void *p2, void *p3);

int kei_interface_init(void) {
    memset(&_data, 0, sizeof(_data));

    _data.trig.mode      = KEI_TRIGMODE_FREERUNNING;
    _data.trig.period_ms = 1000;

    k_condvar_init(&_data.data_ready_cond);
    k_mutex_init(&_data.data_ready_mutex);

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

    k_thread_create(&_data.thread, _kei_thread_stack, K_THREAD_STACK_SIZEOF(_kei_thread_stack),
                    _kei_thread_main, NULL, NULL, NULL, 6, 0, K_NO_WAIT);

    return 0;
}


static void _kei_thread_main(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("KEI thread start");

    int64_t next_trig = 0;

    while(1) {
        if(_data.trig.mode == KEI_TRIGMODE_PERIODIC) {
            int64_t uptime = k_uptime_get();
            if(uptime >= next_trig) {
                kei_interface_trigger(0);
                next_trig = next_trig + _data.trig.period_ms;
                if(uptime >= next_trig) {
                    next_trig = uptime + _data.trig.period_ms;
                }
            }

            k_msleep(next_trig - uptime);
        } else {
            k_msleep(1000);
        }
    }
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

/**
 * @brief Trigger a reading
 *
 * @param wait If non-zero, wait for data to come in before returning
 */
static int kei_interface_trigger(int wait) {
    if(_data.trig.mode == KEI_TRIGMODE_FREERUNNING) {
        return -1;
    }

    if(gpio_pin_set_dt(&_int_gpios.trigger, 1)) {
        return -1;
    }

    /* TODO: Determine minimum trigger time */
    k_msleep(5);

    if(gpio_pin_set_dt(&_int_gpios.trigger, 0)) {
        return -1;
    }

    if (wait) {
        if(k_mutex_lock(&_data.data_ready_mutex, K_MSEC(100))) {
            return -1;
        }

        if (k_condvar_wait(&_data.data_ready_cond, &_data.data_ready_mutex, K_MSEC(100))) {
            k_mutex_unlock(&_data.data_ready_mutex);
            return -1;
        }

        k_mutex_unlock(&_data.data_ready_mutex);
    }

    return 0;
}

int kei_interface_get_data(kei_interface_data_t *data) {
    if(!data) {
        return -1;
    }

    if(_data.trig.mode == KEI_TRIGMODE_MANUAL) {
        if(kei_interface_trigger(1)) {
            return -1;
        }
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

int kei_interface_set_trigmode(kei_interface_trigmode_e mode) {
    if((mode <= KEI_TRIGMODE_NONE) || (mode >= KEI_TRIGMODE_MAX)) {
        return -1;
    }

    if(mode == KEI_TRIGMODE_FREERUNNING) {
        if(gpio_pin_set_dt(&_int_gpios.trigger, 0) ||
           gpio_pin_set_dt(&_int_gpios.hold[0], 0) ||
           gpio_pin_set_dt(&_int_gpios.hold[1], 0)) {
            return -1;
        }
    } else {
        /* Need some more testing to determine whether to use hold 1, 2, or both */
        if(gpio_pin_set_dt(&_int_gpios.trigger, 0) ||
           gpio_pin_set_dt(&_int_gpios.hold[0], 0) ||
           gpio_pin_set_dt(&_int_gpios.hold[1], 1)) {
            return -1;
        }
    }

    _data.trig.mode = mode;

    return 0;
}

int kei_interface_set_trigperiod(uint32_t period_ms) {
    if(period_ms < KEI_TRIG_PERIOD_MIN) {
        return -1;
    }

    if(_data.trig.mode == KEI_TRIGMODE_FREERUNNING) {
        return -1;
    }

    _data.trig.period_ms = period_ms;

    return 0;
}



/*
 * COMMAND HANDLERS
 */

static int _cmdhdlr_kei_read(const struct shell *sh, size_t argc, char **argv);
static int _cmdhdlr_kei_mode(const struct shell *sh, size_t argc, char **argv);
static int _cmdhdlr_kei_trig_mode(const struct shell *sh, size_t argc, char **argv);
static int _cmdhdlr_kei_trig_period(const struct shell *sh, size_t argc, char **argv);

SHELL_STATIC_SUBCMD_SET_CREATE(_subcmd_kei_trig,
    SHELL_CMD(mode, NULL, "Get/set trigger mode\n"
                          "  F: Free-running, P: Periodic, M: Manual",
                          _cmdhdlr_kei_trig_mode),
    SHELL_CMD(period, NULL, "Get/set trigger period",
                            _cmdhdlr_kei_trig_period),
    SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(_subcmd_kei,
    SHELL_CMD(read, NULL, "Get and display a reading from the instrument",
                          _cmdhdlr_kei_read),
    SHELL_CMD(mode, NULL, "Get/set electrometer mode\n"
                          "  V: Volts, O: Ohms, C: Coulombs, A: Amperes",
                          _cmdhdlr_kei_mode),
    SHELL_CMD(trig, &_subcmd_kei_trig, "Trigger config sub-commands", NULL),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(kei, &_subcmd_kei, "Electrometer subcommands", NULL);

static int _cmdhdlr_kei_read(const struct shell *sh, size_t argc, char **argv) {
    ARG_UNUSED(argv);

    if(argc != 1) {
        shell_print(sh, "This command does not expect any arguments");
        return -1;
    }

    kei_interface_data_t data;
    if (kei_interface_get_data(&data)) {
        return -1;
    }
    if(data.flags & KEI_DATAFLAG_OVERLOAD) {
        shell_print(sh, "OVERLOAD");
        return 0;
    }

    unsigned whole = ABS(data.value) / 1000000;
    unsigned frac  = (ABS(data.value) % 1000000) / 100;

    /* NOTE: If -1 < value < 0, the sign will not be present in the whole,
     * so we instead just print it explicitly. Avoiding floating-points to
     * ensure accuracy. Could also just print it as milli-whatever instead.
     * Could also use the polarity bit directly as a flag, to also enable
     * negative zero. */
    shell_print(sh, "%c%03u.%04u x 10^%+02hhd %s",
                (data.value < 0) ? '-' : '+',
                whole, frac, data.range,
                _unit_str[_data.mode]);

    return 0;
}

static const char *_mode_stringify(kei_interface_mode_e mode) {
    switch(mode) {
        case KEI_MODE_NONE:
            return "None";
        case KEI_MODE_VOLTS:
            return "Volts";
        case KEI_MODE_OHMS:
            return "Ohms";
        case KEI_MODE_COULOMBS:
            return "Coulombs";
        case KEI_MODE_AMPERES:
            return "Amperes";
        default:
            return "Invalid";
    }
}

static int _cmdhdlr_kei_mode(const struct shell *sh, size_t argc, char **argv) {
    if(argc == 1) {
        shell_print(sh, "Current mode: %s (%d)", _mode_stringify(_data.mode), _data.mode);
    } else if(argc == 2) {
        if(strlen(argv[1]) > 1) {
            shell_print(sh, "Unsupported value for mode");
            return -1;
        }
        switch(argv[1][0]) {
            case 'V':
                _data.mode = KEI_MODE_VOLTS;
                break;
            case 'O':
                _data.mode = KEI_MODE_OHMS;
                break;
            case 'C':
                _data.mode = KEI_MODE_COULOMBS;
                break;
            case 'A':
                _data.mode = KEI_MODE_AMPERES;
                break;
            default:
                shell_print(sh, "Unsupported value for mode");
                return -1;
        }
    } else {
        shell_print(sh, "Too many arguments!");
        return -1;
    }

    return 0;
}

static const char *_trigmode_stringify(kei_interface_trigmode_e mode) {
    switch(mode) {
        case KEI_TRIGMODE_FREERUNNING:
            return "free-running";
        case KEI_TRIGMODE_PERIODIC:
            return "periodic";
        case KEI_TRIGMODE_MANUAL:
            return "manual";
        default:
            return "invalid";
    }
}

static int _cmdhdlr_kei_trig_mode(const struct shell *sh, size_t argc, char **argv) {
    if(argc == 1) {
        shell_print(sh, "Current trigger mode: %s (%d)",
                    _trigmode_stringify(_data.trig.mode), _data.trig.mode);
    } else if(argc == 2) {
        if(strlen(argv[1]) > 1) {
            shell_print(sh, "Unsupported value for mode");
            return -1;
        }

        kei_interface_trigmode_e mode;

        switch(argv[1][0]) {
            case 'F':
                mode = KEI_TRIGMODE_FREERUNNING;
                break;
            case 'P':
                mode = KEI_TRIGMODE_PERIODIC;
                break;
            case 'M':
                mode = KEI_TRIGMODE_MANUAL;
                break;
            default:
                shell_print(sh, "Unsupported value for mode");
                return -1;
        }

        if(kei_interface_set_trigmode(mode)) {
            return -1;
        }
    } else {
        shell_print(sh, "Too many arguments!");
        return -1;
    }

    return 0;
}

static int _cmdhdlr_kei_trig_period(const struct shell *sh, size_t argc, char **argv) {
    if(_data.trig.mode != KEI_TRIGMODE_PERIODIC) {
        shell_print(sh, "Only valid in periodic mode");
        return -1;
    }

    if(argc == 1) {
        shell_print(sh, "Current trigger period: %u ms", _data.trig.period_ms);
    } else if(argc == 2) {
        if(!isdigit(argv[1][0])) {
            shell_print(sh, "Period must be a number");
        }

        unsigned period_ms = strtoul(argv[1], NULL, 10);
        if(period_ms < KEI_TRIG_PERIOD_MIN) {
            shell_print(sh, "Period cannot be lower than %u", KEI_TRIG_PERIOD_MIN);
            return -1;
        }

        if (kei_interface_set_trigperiod(period_ms)) {
            return -1;
        }
    } else {
        shell_print(sh, "Too many arguments!");
        return -1;
    }

    return 0;
}

