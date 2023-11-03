#ifndef KEI_INTERFACE_H
#define KEI_INTERFACE_H

#include <stdint.h>

#define KEI_DATAFLAG_OVERLOAD (1U << 0)

/* Manual states a maximum rate of 24 readings per second (may be slightly
 * lower on 50 Hz units). */
#define KEI_TRIG_PERIOD_MIN 42

/**< Mode the electrometer is in. This is not available via the 50-pin connector */
typedef enum {
    KEI_MODE_NONE  = 0,
    KEI_MODE_VOLTS,
    KEI_MODE_OHMS,
    KEI_MODE_COULOMBS,
    KEI_MODE_AMPERES,
    KEI_MODE_MAX
} kei_interface_mode_e;

typedef struct {
    int16_t value;       /**< Value of reading */
    uint8_t range;       /**< Current range (power) setting - absolute value*/
    uint8_t sensitivity; /**< Current sensitivity setting */
    uint8_t flags;       /**< Flags */
} kei_interface_rawdata_t;

typedef struct {
    int32_t value;       /**< Value of reading, in micro-units, not accounting for power */
    int8_t  range;       /**< Current range (power) setting */
    uint8_t flags;       /**< Flags */
} kei_interface_data_t;

typedef enum {
    KEI_TRIGMODE_NONE  = 0,
    KEI_TRIGMODE_FREERUNNING, /**< Instrument is allowed to trigger itself */
    KEI_TRIGMODE_PERIODIC,    /**< We periodically trigger the instrument */
    KEI_TRIGMODE_MANUAL,      /**< We only trigger the instrument when a read is requested */
    KEI_TRIGMODE_MAX
} kei_interface_trigmode_e;

/**
 * @brief Initialize interface GPIOs
 */
int kei_interface_init(void);

/**
 * @brief Print the data from the last electrometer reading
 */
int kei_interface_print(void);

/**
 * @brief Configure which mode the electrometer is set to
 *
 * This mode determines how values are interpreted, in particular the range value
 *
 * @param mode Mode to which the electrometer is set to
 */
int kei_interface_set_mode(kei_interface_mode_e mode);

/**
 * @brief Get most recent data received from the electrometer
 *
 * @param data Where to store data
 */
int kei_interface_get_data(kei_interface_data_t *data);

/**
 * @brief Set trigger mode
 *
 * @param mode Desired trigger mode
 */
int kei_interface_set_trigmode(kei_interface_trigmode_e mode);

/**
 * @brief Set trigger period (not applicable in free-running mode)
 *
 * @param period_ms Desired trigger period, in milliseconds, >= 42 ms
 */
int kei_interface_set_trigperiod(uint32_t period_ms);

#endif

