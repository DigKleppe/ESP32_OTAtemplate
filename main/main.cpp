///* OTA example
//
//   This example code is in the Public Domain (or CC0 licensed, at your option.)
//
//   Unless required by applicable law or agreed to in writing, this
//   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//   CONDITIONS OF ANY KIND, either express or implied.
//*/



#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "wifiConnect.h"
#include "settings.h"
#include "updateTask.h"
#include "clockTask.h"
#include <esp_err.h>

esp_err_t init_spiffs(void);
TaskHandle_t connectTaskh;

#define BLINK_GPIO	GPIO_NUM_4
static const char *TAG = "main";


static void blinkTask(void *pvParameter) {

	gpio_reset_pin(BLINK_GPIO);
	/* Set the GPIO as a push/pull output */
	gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

	while (1) {
		gpio_set_level(BLINK_GPIO, 1);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		gpio_set_level(BLINK_GPIO, 0);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}

extern "C" void app_main(void) {
	esp_err_t err;
	TaskHandle_t updateTaskh;

	ESP_LOGI(TAG, "OTA template started\n\n");

	ESP_ERROR_CHECK(init_spiffs());
	err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ESP_ERROR_CHECK(nvs_flash_init());
	}
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	loadSettings();
	strcpy( wifiSettings.SSID, "Klepnet");
	strcpy( wifiSettings.pwd, "Yellowstone1999");

	strcpy(wifiSettings.upgradeServer, "digkleppe.nl" );
	strcpy(wifiSettings.upgradeURL, "https://digkleppe.nl/firmware/OTA");
	strcpy(wifiSettings.upgradeFileName,"ESP32_OTAtemplate.bin");

	//strcpy(wifiSettings.firmwareVersion,"xxx");

	xTaskCreate(&blinkTask, "blink", 8192, NULL, 5, NULL);

	wifiConnect();

	do {
		vTaskDelay(100);
	} while (connectStatus != IP_RECEIVED);
	xTaskCreate(clockTask, "clock", 4 * 1024, NULL, 0, NULL);

	// do {
	// 	vTaskDelay(100);
	// } while (!clockSynced);

	
	xTaskCreate(&updateTask, "updateTask",2* 8192, NULL, 5, &updateTaskh);

	while (1) {
//		newStorageVersion[0] = 0;
//		spiffsUpdateFinised = true;
//		xTaskCreate(&updateSpiffsTask, "updateSpiffsTask", 8192, (void*) newStorageVersion, 5, NULL);
//		while (!spiffsUpdateFinised)
//			vTaskDelay(1000);
//
//		if (newStorageVersion[0]) {
//			strcpy(userSettings.spiffsVersion, newStorageVersion);
//			saveSettings();
//		}
		vTaskDelay(100000);
	}
}

