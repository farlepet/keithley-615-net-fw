#ifndef KEI_INTERFACE_H
#define KEI_INTERFACE_H

/**
 * @brief Initialize interface GPIOs
 */
int kei_interface_init(void);

/**
 * @brief Force a read of the electrometer
 *
 * @todo Use interrupts triggered by the print signal
 */
int kei_interface_read(void);

#endif

