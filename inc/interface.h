#ifndef KEI_INTERFACE_H
#define KEI_INTERFACE_H

#include <stdint.h>

#define KEI_DATAFLAG_OVERLOAD (1U << 0)

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

#endif

