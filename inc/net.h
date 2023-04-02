#ifndef KEI_NET_H
#define KEI_NET_H

/**
 * @brief Initialize network driver
 */
int kei_net_init(void);

/**
 * @brief Get address via DHCP
 */
int kei_net_getaddr(void);

/**
 * @brief Get time via SNTP
 *
 * @param time_s Where to store retrieved time in seconds
 * @param time_us Where to store microsecond time offset, if desired
 */
int kei_net_sntp(time_t *time_s, unsigned *time_us);

#endif

