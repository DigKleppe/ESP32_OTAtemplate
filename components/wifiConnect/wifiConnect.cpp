/*
handles wifi connect process

*/

#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <string.h>

#include "lwip/err.h"
#include "lwip/ip4_addr.h"
#include "lwip/sys.h"
#include "mdns.h"
#include "settings.h"

#include "esp_smartconfig.h"
#include "wifiConnect.h"
#ifndef CONFIG_FIXED_LAST_IP_DIGIT
#define CONFIG_FIXED_LAST_IP_DIGIT 0 // ip will be xx.xx.xx.pp    xx from DHCP  , <= 0 disables this
#endif

#ifndef CONFIG_WPS_ENABLED
#define CONFIG_WPS_ENABLED 1
#endif

#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <string.h>

#include "lwip/err.h"
#include "lwip/ip4_addr.h"
#include "lwip/sys.h"
#include "mdns.h"
#include "settings.h"

#include "esp_smartconfig.h"
#include "wifiConnect.h"
#ifndef CONFIG_FIXED_LAST_IP_DIGIT
#define CONFIG_FIXED_LAST_IP_DIGIT 99 // ip will be xx.xx.xx.pp    xx from DHCP  , <= 0 disables this
#endif

/*set wps mode via project configuration */
#if CONFIG_EXAMPLE_WPS_TYPE_PBC
#define WPS_MODE WPS_TYPE_PBC
#elif CONFIG_EXAMPLE_WPS_TYPE_PIN
#define WPS_MODE WPS_TYPE_PIN
#else
#define WPS_MODE WPS_TYPE_DISABLE
#endif /*CONFIG_EXAMPLE_WPS_TYPE_PBC*/

#define MAX_RETRY_ATTEMPTS 2

#ifdef CONFIG_WPS_ENABLED
#ifndef PIN2STR
#define PIN2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5], (a)[6], (a)[7]
#define PINSTR "%c%c%c%c%c%c%c%c"
#endif
static esp_wps_config_t wpsConfig = WPS_CONFIG_INIT_DEFAULT(WPS_MODE);
static wifi_config_t wps_ap_creds[MAX_WPS_AP_CRED];
static int s_ap_creds_num = 0;
#endif

static int s_retry_num = 0;
void initialiseMdns(char *hostName);
esp_err_t start_file_server(const char *base_path);

char myIpAddress[16];
bool DHCPoff;
bool IP6off;
bool DNSoff;
bool fileServerOff;
bool fixedLastIPdigit = true;

bool doStop;
esp_netif_t *s_sta_netif = NULL;
volatile connectStatus_t connectStatus;

static void setStaticIp(esp_netif_t *netif);
esp_err_t saveSettings(void);

#define EXAMPLE_ESP_WIFI_SSID "xxx"
#define EXAMPLE_ESP_WIFI_PASS "yyy"

wifiSettings_t wifiSettings;
// wifiSettings_t wifiSettingsDefaults = { CONFIG_EXAMPLE_WIFI_SSID,
// CONFIG_EXAMPLE_WIFI_PASSWORD,ipaddr_addr(DEFAULT_IPADDRESS),ipaddr_addr(DEFAULT_GW),CONFIG_DEFAULT_FIRMWARE_UPGRADE_URL,CONFIG_FIRMWARE_UPGRADE_FILENAME,false
// };
wifiSettings_t wifiSettingsDefaults = {
	CONFIG_EXAMPLE_WIFI_SSID, CONFIG_EXAMPLE_WIFI_PASSWORD, ipaddr_addr(DEFAULT_IPADDRESS), ipaddr_addr(DEFAULT_GW), " ", " ", false};

/* The examples use WiFi configuration that you can set via project configuration menu

 If you'd rather not, just change the below entries to strings with
 the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
 */

#define EXAMPLE_ESP_MAXIMUM_RETRY 2
#define CONFIG_ESP_WPA3_SAE_PWE_BOTH 1
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define CONFIG_ESP_WIFI_PW_ID ""
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#define SMARTCONFIGTIMEOUT CONFIG_SMARTCONFIG_TIMEOUT

#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define CONNECTED_BIT BIT0
static const int ESPTOUCH_DONE_BIT = BIT2;

static const char *TAG = "wifiConnect";

int getRssi(void) {
	wifi_ap_record_t ap_info;
	if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
		return ap_info.rssi;
	} else {
		ESP_LOGE(TAG, "Failed to get AP info");
		return 0;
	}
}

//#include "lwip/init.h"
// #include "lwip/netif.h"
// #include "lwip/dns.h"
// #include "lwip/apps/sntp.h"

// // Configure network interface with static IP
// void configure_network(void) {
//     struct netif *netif = &your_netif_struct; // Your network interface
//     ip4_addr_t ipaddr, netmask, gw, dns1, dns2;
    
//     // Set static IP configuration
//     IP4_ADDR(&ipaddr, 192, 168, 1, 100);    // Your static IP
//     IP4_ADDR(&netmask, 255, 255, 255, 0);   // Your netmask
//     IP4_ADDR(&gw, 192, 168, 1, 1);          // Your gateway
//     IP4_ADDR(&dns1, 8, 8, 8, 8);            // Primary DNS (Google)
//     IP4_ADDR(&dns2, 8, 8, 4, 4);            // Secondary DNS (Google)
    
//     // Apply network configuration
//     netif_set_addr(netif, &ipaddr, &netmask, &gw);
//     netif_set_up(netif);
    
//     // Set DNS servers
//     dns_setserver(0, &dns1);
//     dns_setserver(1, &dns2);
// }


static esp_err_t example_set_dns_server(esp_netif_t *netif, uint32_t addr, esp_netif_dns_type_t type)
{
    if (addr && (addr != IPADDR_NONE)) {
        esp_netif_dns_info_t dns;
        dns.ip.u_addr.ip4.addr = addr;
        dns.ip.type = IPADDR_TYPE_V4;
        ESP_ERROR_CHECK(esp_netif_set_dns_info(netif, type, &dns));
    }
    return ESP_OK;
}

static void setStaticIp(esp_netif_t *netif) {
	if (esp_netif_dhcpc_stop(netif) != ESP_OK) {
		//	ESP_LOGE(TAG, "Failed to stop dhcp client");
	}
	esp_netif_ip_info_t ip;
	memset(&ip, 0, sizeof(esp_netif_ip_info_t));

	if (wifiSettings.ip4Address.addr == 0) {
		ip.ip.addr = ipaddr_addr(DEFAULT_IPADDRESS);
		ip.gw.addr = ipaddr_addr(DEFAULT_GW);
	} else {
		ip.ip = wifiSettings.ip4Address; //  ipaddr_addr(EXAMPLE_STATIC_IP_ADDR);
		ip.gw = wifiSettings.gw;
	}
	ip.netmask.addr = ipaddr_addr(STATIC_NETMASK_ADDR);

	ESP_LOGI(TAG, "Set fixed IPv4 address to: " IPSTR ",", IP2STR(&ip.ip));

	if (esp_netif_set_ip_info(netif, &ip) != ESP_OK) {
		ESP_LOGE(TAG, "Failed to set ip info");
		return;
	}
//	 netif_set_up((netif *) netif);
//     // Set DNS servers

// 	IP4_ADDR(&dns1, 8, 8, 8, 8);            // Primary DNS (Google)
// 	IP4_ADDR(&dns2, 8, 8, 4, 4);            // Secondary DNS (Google)

//     //  dns_setserver(0,(ip_addr_t *) &dns1);
//     //  dns_setserver(1,(ip_addr_t *) &dns2);

// 	 esp_netif_dns_info_t dnsinfo1,  dnsinfo2;

// //	dnsinfo1.ip.u_addr.ip4 =  (esp_ip4_addr_t) dns1.addr;
// 	dnsinfo1.ip.u_addr.ip4 = (esp_ip4_addr_t ) wifiSettings.gw;
// 	dnsinfo2.ip.u_addr.ip4 = (esp_ip4_addr_t ) dns2.addr;

// 	//   ESP_LOGD(TAG, "Success to set static ip: %s, netmask: %s, gw: %s", EXAMPLE_STATIC_IP_ADDR, EXAMPLE_STATIC_NETMASK_ADDR,
// 	//   EXAMPLE_STATIC_GW_ADDR);
// 	if ( esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dnsinfo1) != ESP_OK)
// 		ESP_LOGE(TAG, "Failed to set dns1 info");

// 	if ( esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dnsinfo2) != ESP_OK)
// 		ESP_LOGE(TAG, "Failed to set dns2 info");

// }

//	ESP_ERROR_CHECK(esp_netif_set_dns_info(netif, EXAMPLE_BACKUP_DNS_SERVER, dnsinfo2));
	if (example_set_dns_server(netif, (uint32_t) wifiSettings.gw.addr, ESP_NETIF_DNS_MAIN) != ESP_OK)
		ESP_LOGE(TAG, "Failed to set dns main");
	if (example_set_dns_server(netif, ipaddr_addr("8,8,8,8"), ESP_NETIF_DNS_BACKUP) != ESP_OK)
		ESP_LOGE(TAG, "Failed to set dns backup");

	if (example_set_dns_server(netif, ipaddr_addr("8,8,4,4"), ESP_NETIF_DNS_FALLBACK) != ESP_OK)
		ESP_LOGE(TAG, "Failed to set dns fallback");
 	
		// ESP_ERROR_CHECK(example_set_dns_server(netif, ipaddr_addr(EXAMPLE_MAIN_DNS_SERVER), ESP_NETIF_DNS_MAIN));
 	// ESP_ERROR_CHECK(example_set_dns_server(netif, ipaddr_addr(EXAMPLE_BACKUP_DNS_SERVER), ESP_NETIF_DNS_BACKUP));
}

#ifdef CONFIG_WPS_ENABLED
static bool wpsActive = false;
static TimerHandle_t wpsTimer;
void wpsTimerCallback(TimerHandle_t xTimer) {
	ESP_LOGI(TAG, "WPS Timeout");
	ESP_ERROR_CHECK(esp_wifi_wps_disable());

	connectStatus = CONNECTING;
	s_retry_num = 0;
	esp_wifi_connect();
	xTimerDelete(wpsTimer, 0);
	wpsTimer = NULL;
}
// for timeout, without this timer the timeout is 120s
void startWpsTimer(void) {
	wpsTimer = xTimerCreate("WpsTimer", pdMS_TO_TICKS(1000 * CONFIG_WPS_TIMEOUT), pdFALSE, (void *)0, wpsTimerCallback);
	if (xTimerStart(wpsTimer, 0) != pdPASS) {
		ESP_LOGE(TAG, "Failed to start WPS timer");
	}
}
#endif

#ifdef CONFIG_SMARTCONFIG_ENABLED
static void smartconfigTask(void *parm) {
	EventBits_t uxBits;
	ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
	smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));
	while (1) {
		uxBits =
			xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, (SMARTCONFIGTIMEOUT * 1000) / portTICK_PERIOD_MS);
		if (uxBits & CONNECTED_BIT) {
			ESP_LOGI(TAG, "WiFi Connected to ap");
		}
		if (uxBits & ESPTOUCH_DONE_BIT) {
			ESP_LOGI(TAG, "smartconfig over");
			esp_smartconfig_stop();
			vTaskDelete(NULL);
		}
		if (uxBits == 0) { // timeout
			ESP_LOGI(TAG, "smartconfig timeout");
			esp_smartconfig_stop();
			connectStatus = CONNECTING;
			s_retry_num = 0;
			esp_wifi_connect();
			vTaskDelete(NULL);
		}
	}
}
#endif

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
	static int ap_idx = 1;
	if (doStop)
		return;

	if (event_base == WIFI_EVENT) {
		ESP_LOGI(TAG, "WifiEvent %d", (int)event_id);
		switch (event_id) {
		case WIFI_EVENT_STA_START:
			ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
			connectStatus = CONNECTING;
			esp_wifi_connect();
			break;
		case WIFI_EVENT_STA_DISCONNECTED:
			ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");
			if (s_retry_num < MAX_RETRY_ATTEMPTS) {
				esp_wifi_connect();
				s_retry_num++;
			} else {
#ifdef CONFIG_WPS_ENABLED
				if (connectStatus != WPS_ACTIVE) {
					connectStatus = WPS_ACTIVE;
					ESP_LOGI(TAG, "WPS Active");
					ESP_ERROR_CHECK(esp_wifi_wps_disable());
					ESP_ERROR_CHECK(esp_wifi_wps_enable(&wpsConfig));
					ESP_ERROR_CHECK(esp_wifi_wps_start(0));
					wpsActive = true;
					startWpsTimer();
				} else {
					if (ap_idx < s_ap_creds_num) {
						/* Try the next AP credential if first one fails */
						if (ap_idx < s_ap_creds_num) {
							ESP_LOGI(TAG, "Connecting to SSID: %s, Passphrase: %s", wps_ap_creds[ap_idx].sta.ssid, wps_ap_creds[ap_idx].sta.password);
							ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wps_ap_creds[ap_idx++]));
							esp_wifi_connect();
						}
					} else {
						ESP_LOGI(TAG, "Failed to connect!");
						connectStatus = CONNECTING;
					}
				}
#else
#ifdef CONFIG_SMARTCONFIG_ENABLED
				xTaskCreate(smartconfigTask, "smartconfig_task", 4096, NULL, 3, NULL);
				connectStatus = SMARTCONFIG_ACTIVE;
				ESP_LOGI(TAG, "Starting SmartConfig");
#endif
#endif
				s_retry_num = 0;
			}
			break;
#ifdef CONFIG_WPS_ENABLED

		case WIFI_EVENT_STA_WPS_ER_SUCCESS: {
			ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_SUCCESS");
			{
				wifi_event_sta_wps_er_success_t *evt = (wifi_event_sta_wps_er_success_t *)event_data;
				int i;
				if (wpsTimer != NULL) {
					xTimerDelete(wpsTimer, 0);
					wpsTimer = NULL;
				}
				if (evt) { // never seen this ??

					s_ap_creds_num = evt->ap_cred_cnt;
					for (i = 0; i < s_ap_creds_num; i++) {
						memcpy(wps_ap_creds[i].sta.ssid, evt->ap_cred[i].ssid, sizeof(evt->ap_cred[i].ssid));
						memcpy(wps_ap_creds[i].sta.password, evt->ap_cred[i].passphrase, sizeof(evt->ap_cred[i].passphrase));
					}
					/* If multiple AP credentials are received from WPS, connect with first one */
					ESP_LOGI(TAG, "Connecting to SSID: %s, Passphrase: %s", wps_ap_creds[0].sta.ssid, wps_ap_creds[0].sta.password);
					ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wps_ap_creds[0]));
					connectStatus = CONNECTED;
				}
				/*
				 * If only one AP credential is received from WPS, there will be no event data and
				 * esp_wifi_set_config() is already called by WPS modules for backward compatibility
				 * with legacy apps. So directly attempt connection here.
				 */
				ESP_ERROR_CHECK(esp_wifi_wps_disable());
				esp_wifi_connect();
			}
		} break;
		case WIFI_EVENT_STA_WPS_ER_FAILED:
			ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_FAILED");
			ESP_ERROR_CHECK(esp_wifi_wps_disable());
			connectStatus = CONNECTING;
			if (wpsTimer != NULL) {
				xTimerDelete(wpsTimer, 0);
				wpsTimer = NULL;
			}
			s_retry_num = 0;
			esp_wifi_connect();
			break;
		case WIFI_EVENT_STA_WPS_ER_TIMEOUT:
			ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_TIMEOUT");
			ESP_ERROR_CHECK(esp_wifi_wps_disable());
			if (connectStatus == CONNECTING) {
				s_retry_num = 0;
				esp_wifi_connect();
				if (wpsTimer != NULL) {
					xTimerDelete(wpsTimer, 0);
					wpsTimer = NULL;
				}
			}
			break;
		case WIFI_EVENT_STA_WPS_ER_PIN: {
			ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_PIN");
			/* display the PIN code */
			wifi_event_sta_wps_er_pin_t *event = (wifi_event_sta_wps_er_pin_t *)event_data;
			ESP_LOGI(TAG, "WPS_PIN = " PINSTR, PIN2STR(event->pin_code));
		} break;
#endif
		default:
			break;
		}
		return;
	}
	/*******************************   IP EVENT  ********************************************************* */
	if (event_base == IP_EVENT) {
		ESP_LOGI(TAG, "IP_EVENT %d", (int)event_id);
		switch (event_id) {
		case IP_EVENT_STA_GOT_IP: {
			ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
			sprintf(myIpAddress, IPSTR, IP2STR(&event->ip_info.ip));

#ifdef CONFIG_WPS_ENABLED

			if (wpsTimer != NULL) {
				xTimerDelete(wpsTimer, 0);
				wpsTimer = NULL;
			}
			if (wpsActive) { // else this is set in WPS_EVENT_STA_WPS_ER_SUCCESS
				wifi_config_t config;
				esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &config);
				if (err == ESP_OK) {
					ESP_LOGI(TAG, "WPS: SSID: %s, PW: %s\n", (char *)config.sta.ssid, (char *)config.sta.password);
					memcpy((char *)wifiSettings.SSID, (char *)config.sta.ssid, sizeof(wifiSettings.SSID));
					memcpy((char *)wifiSettings.pwd, (char *)config.sta.password, sizeof(wifiSettings.pwd));
					saveSettings();
				} else {
					printf("Couldn't get config: %d\n", (int)err);
				}
				wpsActive = false;
			}

#endif
			if (CONFIG_FIXED_LAST_IP_DIGIT > 0) { // check if the last digit of IP address = CONFIG_FIXED_LAST_IP_DIGIT
				uint32_t addr = event->ip_info.ip.addr;

				if ((addr & 0xFF000000) == (CONFIG_FIXED_LAST_IP_DIGIT << 24)) { // last ip digit(LSB) is MSB in addr
					xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);		 // ok
					connectStatus = IP_RECEIVED;
				} else {
					wifiSettings.ip4Address = (esp_ip4_addr_t)((addr & 0x00FFFFFF) + (CONFIG_FIXED_LAST_IP_DIGIT << 24));
					sprintf(myIpAddress, IPSTR, IP2STR(&wifiSettings.ip4Address));
					saveSettings();
					ESP_LOGI(TAG, "Set static IP to %s , reconnecting", (myIpAddress));
					setStaticIp(s_sta_netif);
					esp_wifi_disconnect();
					esp_wifi_connect();
					if (!DNSoff)
						initialiseMdns(userSettings.moduleName);
				}
			}

			else {
				xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
				connectStatus = IP_RECEIVED;
				if (!DNSoff)
					initialiseMdns(userSettings.moduleName);
			}
		} break;
		default:
			break;
		}
		return;
	}
#ifndef CONFIG_WPS_ENABLED
#ifdef CONFIG_SMARTCONFIG_ENABLED

	/*******************************   Smartconfig EVENT  ********************************************************* */
	if (event_base == SC_EVENT) {
		ESP_LOGI(TAG, "SC_EVENT %d", (int)event_id);
		switch (event_id) {
		case SC_EVENT_SCAN_DONE:
			ESP_LOGI(TAG, "Scan done");
			break;
		case SC_EVENT_FOUND_CHANNEL:
			ESP_LOGI(TAG, "Found channel");
			break;
		case SC_EVENT_GOT_SSID_PSWD: {
			ESP_LOGI(TAG, "Got SSID and password");
			connectStatus = CONNECTED;
			smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
			wifi_config_t wifi_config;
			uint8_t ssid[33] = {0};
			uint8_t password[65] = {0};
			uint8_t rvd_data[33] = {0};

			bzero(&wifi_config, sizeof(wifi_config_t));
			memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
			memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
			wifi_config.sta.bssid_set = evt->bssid_set;
			if (wifi_config.sta.bssid_set == true) {
				memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
			}

			memcpy(ssid, evt->ssid, sizeof(evt->ssid));
			memcpy(password, evt->password, sizeof(evt->password));
			ESP_LOGI(TAG, "SSID:%s", ssid);
			ESP_LOGI(TAG, "PASSWORD:%s", password);

			memcpy((char *)wifiSettings.SSID, ssid, sizeof(wifiSettings.SSID));
			memcpy((char *)wifiSettings.pwd, password, sizeof(wifiSettings.pwd));
			saveSettings();

			if (evt->type == SC_TYPE_ESPTOUCH_V2) {
				ESP_ERROR_CHECK(esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)));
				ESP_LOGI(TAG, "RVD_DATA:");
				for (int i = 0; i < 33; i++) {
					printf("%02x ", rvd_data[i]);
				}
				printf("\n");
			}
			ESP_ERROR_CHECK(esp_wifi_disconnect());
			ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
			esp_wifi_connect();
		} break;
		case SC_EVENT_SEND_ACK_DONE:
			xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
			break;
		default:
			break;
		} // end switch event_id
	} // end if event_base == SC_EVENT
#endif // CONFIG_SMARTCONFIG_ENABLED
#endif // NOT CONFIG_WPS_ENABLED
	return;
} // end event_handler

void wifi_init_sta(void) {

	connectStatus = CONNECTING;
	s_wifi_event_group = xEventGroupCreate();

	ESP_ERROR_CHECK(esp_netif_init());

	//	ESP_ERROR_CHECK(esp_event_loop_create_default());  in main
	s_sta_netif = esp_netif_create_default_wifi_sta();
	if (DHCPoff)
		setStaticIp((esp_netif_t *)s_sta_netif);

	esp_netif_set_hostname(s_sta_netif, userSettings.moduleName);

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	esp_event_handler_instance_t instance_any_id;
	esp_event_handler_instance_t instance_got_ip;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

	ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

	wifi_config_t wifi_config = {
		.sta =
			{
				.ssid = EXAMPLE_ESP_WIFI_SSID, .password = EXAMPLE_ESP_WIFI_PASS,

				/* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
				 * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
				 * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
				 * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
				 */
				//	.threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
				//	.sae_pwe_h2e = ESP_WIFI_SAE_MODE,
				//	.sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
			},
	};

	wifi_config.sta.threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD;
	wifi_config.sta.sae_pwe_h2e = ESP_WIFI_SAE_MODE;
	strcpy((char *)wifi_config.sta.sae_h2e_identifier, EXAMPLE_H2E_IDENTIFIER);

	strcpy((char *)wifi_config.sta.ssid, wifiSettings.SSID);
	strcpy((char *)wifi_config.sta.password, wifiSettings.pwd);

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_ERROR_CHECK(start_file_server("/spiffs"));

	ESP_LOGI(TAG, "wifi_init_sta finished.");
}

void wifi_stop(void) {
	doStop = true;

#ifdef CONFIG_WPS_ENABLED
	if (wpsTimer != NULL) {
		xTimerDelete(wpsTimer, 0);
		wpsTimer = NULL;
	}
#endif
	esp_err_t err = esp_wifi_stop();
	if (err == ESP_ERR_WIFI_NOT_INIT) {
		return;
	}
	ESP_ERROR_CHECK(err);
	ESP_ERROR_CHECK(esp_wifi_deinit());
	ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(s_sta_netif));
	esp_netif_destroy(s_sta_netif);
	s_sta_netif = NULL;
}

void wifiConnect(void) {
	wifi_init_sta();
	//	g_pCGIs = CGIurls; // for file_server to read CGIurls
}
