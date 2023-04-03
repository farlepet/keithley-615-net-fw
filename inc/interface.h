#ifndef KEI_INTERFACE_H
#define KEI_INTERFACE_H

#include <stdint.h>

#define KEI_DATAFLAG_OVERLOAD (1U << 0)

typedef struct {
    int16_t value;       /**< Value of reading */
    uint8_t range;       /**< Current range setting */
    uint8_t sensitivity; /**< Current sensitivity setting */
    uint8_t flags;       /**< Flags */
} kei_interface_rawdata_t;

typedef struct {
    int32_t value;       /**< Value of reading, in thousanths/milli */
    uint8_t range;       /**< Current range setting */
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

#endif

