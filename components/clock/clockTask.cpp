/*
 * Clock.cpp
 *
 *  Created on: Apr 2, 2022
 *      Author: dig
 */

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "lwip/ip_addr.h"
#include "esp_sntp.h"


volatile bool clockSynced; 
struct tm timeinfo;
static const char *TAG = "Clock";
#define CONFIG_SNTP_TIME_SERVER "pool.ntp.org"


// void time_sync_notification_cb(struct timeval *tv)
// {
//     ESP_LOGI(TAG, "Notification of a time synchronization event");
// }




// static void obtain_time(void)
// {

//     ESP_LOGI(TAG, "Initializing and starting SNTP");
//     esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);

// //    config.sync_cb = time_sync_notification_cb;     // Note: This is only needed if we want

//     esp_netif_sntp_init(&config);


//     // wait for time to be set
//     time_t now = 0;
//     struct tm timeinfo = { 0 };
//     int retry = 0;
//     const int retry_count = 15;
//     while (esp_netif_sntp_sync_wait(2000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && ++retry < retry_count) {
//         ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
//     }
//     time(&now);
//     localtime_r(&now, &timeinfo);

//     ESP_ERROR_CHECK( example_disconnect() );
//     esp_netif_sntp_deinit();
// }

static void initialize_sntp(void)
{
 
	ESP_LOGI(TAG, "Initializing and starting SNTP");
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);

//    config.sync_cb = time_sync_notification_cb;     // Note: This is only needed if we want

    esp_netif_sntp_init(&config);
}

void clockTask(void *pvParameter) {
	int lastsec = -1;
	char strftime_buf[64];
	bool once = false;

    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3",1);
    tzset();

    initialize_sntp();

    time_t now = 0;
    int retry = 0;
    const int retry_count = 20;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET ) {//  && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... );// (%d/%d)", retry, retry_count);
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
	clockSynced = true;
    do {
    	 time(&now);
    	 localtime_r(&now, &timeinfo);
    	 if (lastsec != timeinfo.tm_sec ) {
    		 lastsec = timeinfo.tm_sec;
    		 if( !once) {
    			 strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    			 ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);
    			 once = true;
    		 }
//    		 sprintf(strftime_buf,"%2d:%02d:%02d" , timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    	 }
    	 vTaskDelay(200/portTICK_PERIOD_MS );
    } while (1);
}



