/*
 * connect.h
 *
 *  Created on: Jan 23, 2022
 *      Author: dig
 */

#ifndef WIFI_CONNECT_H_
#define WIFI_CONNECT_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_netif.h"


#ifdef __cplusplus
extern "C" {
#endif
#define MAX_STORAGEVERSIONSIZE 16


#define ESP_WIFI_SSID "test"
#define ESP_WIFI_PASS "Yellowstone"

typedef struct {
	char SSID[33];
	char pwd[64];
	esp_ip4_addr_t ip4Address;
	esp_ip4_addr_t gw;
	char upgradeServer[32] ; 
	char upgradeURL[128]; 	 
	char upgradeFileName[32]; 
	char firmwareVersion[MAX_STORAGEVERSIONSIZE]; // holding current app version
	char SPIFFSversion[MAX_STORAGEVERSIONSIZE];	// holding current spiffs version
	bool updated;
}wifiSettings_t;


extern wifiSettings_t wifiSettings;
extern wifiSettings_t wifiSettingsDefaults;
extern char myIpAddress[];

#define STATIC_NETMASK_ADDR "255.255.255.0"
#define DEFAULT_IPADDRESS 	"192.168.2.50"
#define DEFAULT_GW		 	"192.168.2.255"

extern bool DHCPoff;
extern bool DNSoff;
extern bool fileServerOff;
typedef enum {
	CONNECTING,
	CONNECT_TIMEOUT,
	CONNECTED,
	SMARTCONFIG_ACTIVE,
	WPS_ACTIVE,
	WPS_SUCCESS,
	WPS_FAILED,
	WPS_TIMEOUT,
	IP_RECEIVED
} connectStatus_t;

extern volatile  connectStatus_t connectStatus;

void wifiConnect (void);

#if !CONFIG_IDF_TARGET_LINUX
#if CONFIG_EXAMPLE_CONNECT_WIFI
#define EXAMPLE_NETIF_DESC_STA "netif_sta"
#endif

#if CONFIG_EXAMPLE_CONNECT_ETHERNET
#define EXAMPLE_NETIF_DESC_ETH "netif_eth"
#endif

/* Example default interface, prefer the ethernet one if running in example-test (CI) configuration */
#if CONFIG_EXAMPLE_CONNECT_ETHERNET
#define EXAMPLE_INTERFACE get_netif_from_desc(EXAMPLE_NETIF_DESC_ETH)
#define get_netif() get_netif_from_desc(EXAMPLE_NETIF_DESC_ETH)
#elif CONFIG_EXAMPLE_CONNECT_WIFI
#define EXAMPLE_INTERFACE get_netif_from_desc(EXAMPLE_NETIF_DESC_STA)
#define get_netif() get_netif_from_desc(EXAMPLE_NETIF_DESC_STA)
#endif


int getRssi(void);  
esp_err_t disconnect(void);

/**
 * @brief Configure stdin and stdout to use blocking I/O
 *
 * This helper function is used in ASIO examples. It wraps installing the
 * UART driver and configuring VFS layer to use UART driver for console I/O.
 */
esp_err_t configure_stdin_stdout(void);

/**
 * @brief Returns esp-netif pointer created by connect() described by
 * the supplied desc field
 *
 * @param desc Textual interface of created network interface, for example "sta"
 * indicate default WiFi station, "eth" default Ethernet interface.
 *
 */
esp_netif_t *get_netif_from_desc(const char *desc);

#if CONFIG_EXAMPLE_PROVIDE_WIFI_CONSOLE_CMD
/**
 * @brief Register wifi connect commands
 *
 * Provide a simple wifi_connect command in esp_console.
 * This function can be used after esp_console is initialized.
 */
void register_wifi_connect_commands(void);
#endif

#if CONFIG_EXAMPLE_CONNECT_ETHERNET
/**
 * @brief Get the example Ethernet driver handle
 *
 * @return esp_eth_handle_t
 */
esp_eth_handle_t get_eth_handle(void);
#endif // CONFIG_EXAMPLE_CONNECT_ETHERNET

#else
static inline esp_err_t connect(void) {return ESP_OK;}
#endif // !CONFIG_IDF_TARGET_LINUX

#define CONFIG_EXAMPLE_WIFI_CONN_MAX_RETRY 	10

#if CONFIG_EXAMPLE_CONNECT_IPV6
#define MAX_IP6_ADDRS_PER_NETIF (5)

#if defined(CONFIG_EXAMPLE_CONNECT_IPV6_PREF_LOCAL_LINK)
#define EXAMPLE_CONNECT_PREFERRED_IPV6_TYPE ESP_IP6_ADDR_IS_LINK_LOCAL
#elif defined(CONFIG_EXAMPLE_CONNECT_IPV6_PREF_GLOBAL)
#define EXAMPLE_CONNECT_PREFERRED_IPV6_TYPE ESP_IP6_ADDR_IS_GLOBAL
#elif defined(CONFIG_EXAMPLE_CONNECT_IPV6_PREF_SITE_LOCAL)
#define EXAMPLE_CONNECT_PREFERRED_IPV6_TYPE ESP_IP6_ADDR_IS_SITE_LOCAL
#elif defined(CONFIG_EXAMPLE_CONNECT_IPV6_PREF_UNIQUE_LOCAL)
#define EXAMPLE_CONNECT_PREFERRED_IPV6_TYPE ESP_IP6_ADDR_IS_UNIQUE_LOCAL
#endif // if-elif CONFIG_EXAMPLE_CONNECT_IPV6_PREF_...

#endif


#if CONFIG_EXAMPLE_CONNECT_IPV6
extern const char *ipv6_addr_types_to_str[6];
#endif

void wifi_start(void);
void wifi_stop(void);
esp_err_t wifi_sta_do_connect(wifi_config_t wifi_config, bool wait);
esp_err_t wifi_sta_do_disconnect(void);
bool is_our_netif(const char *prefix, esp_netif_t *netif);
void print_all_netif_ips(const char *prefix);
void wifi_shutdown(void);
esp_err_t wifi_connect(void);
void ethernet_shutdown(void);
esp_err_t ethernet_connect(void);


#ifdef __cplusplus
}
#endif

#endif /* WIFI_CONNECT_H_ */
