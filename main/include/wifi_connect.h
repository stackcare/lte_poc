#ifndef __WIFI_CONNECT_H__
#define __WIFI_CONNECT_H__


#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_netif.h"

#ifdef CONFIG_CONNECT_ETHERNET
#define EXAMPLE_INTERFACE get_example_netif()
#endif

#ifdef CONFIG_CONNECT_WIFI
#define EXAMPLE_INTERFACE get_example_netif()
#endif

/**
 * @brief Configure Wi-Fi or Ethernet, connect, wait for IP
 *
 * This all-in-one helper function is used in protocols examples to
 * reduce the amount of boilerplate in the example.
 *
 * It is not intended to be used in real world applications.
 * See examples under examples/wifi/getting_started/ and examples/ethernet/
 * for more complete Wi-Fi or Ethernet initialization code.
 *
 * Read "Establishing Wi-Fi or Ethernet Connection" section in
 * examples/protocols/README.md for more information about this function.
 *
 * @return ESP_OK on successful connection
 */
esp_err_t _connect(void);

/**
 * Counterpart to example_connect, de-initializes Wi-Fi or Ethernet
 */
esp_err_t _disconnect(void);


/**
 * @brief Returns esp-netif pointer created by example_connect()
 *
 */
esp_netif_t *get_netif(void);

esp_netif_t *get_netif_from_desc(const char *desc);
#ifdef __cplusplus
}
#endif

#endif // !__WIFI_CONNECT_H__
