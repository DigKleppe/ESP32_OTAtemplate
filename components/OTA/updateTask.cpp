/*
 * updateTask.cpp
 *
 *  Created on: Jan 8, 2024
 *      Author: dig
 */

#include <string.h>
#include <inttypes.h>
#include "errno.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

#include "wifiConnect.h"
#include "settings.h"
#include "httpsReadFile.h"
#include "updateTask.h"
#include "updateFirmWareTask.h"
#include "updateSpiffsTask.h"

static const char *TAG = "updateTask";

volatile updateStatus_t updateStatus;
volatile bool getNewVersionTaskFinished;

typedef struct {
	char *infoFileName;
	char *dest;
} versionInfoParam_t;


esp_err_t getNewVersion(char *infoFileName, char *newVersion) {
	char url[96];
	int len;

	strcpy(url, wifiSettings.upgradeURL);
	strcat(url, "/");
	strcat(url, infoFileName);

	len = httpsReadFile( url, newVersion, MAX_STORAGEVERSIONSIZE-1);
	newVersion[len] = 0;
	
	if (len >0)
		return ESP_OK;
	else
		return ESP_FAIL;
}

void updateTask(void *pvParameter) {
	bool doUpdate;
	char newVersion[MAX_STORAGEVERSIONSIZE];
	TaskHandle_t updateFWTaskh;
	TaskHandle_t updateSPIFFSTaskh;

	if ((strcmp(wifiSettings.upgradeFileName, CONFIG_FIRMWARE_UPGRADE_FILENAME) != 0) || (strcmp(wifiSettings.upgradeURL, CONFIG_DEFAULT_FIRMWARE_UPGRADE_URL) != 0)) {
		strcpy(wifiSettings.upgradeFileName, CONFIG_FIRMWARE_UPGRADE_FILENAME);
		strcpy(wifiSettings.upgradeURL, CONFIG_DEFAULT_FIRMWARE_UPGRADE_URL);
		saveSettings();
	}

	const esp_partition_t *update_partition = NULL;
	const esp_partition_t *configured = esp_ota_get_boot_partition();
	const esp_partition_t *running = esp_ota_get_running_partition();

	if (configured != running) {
		ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08"PRIx32", but running from offset 0x%08"PRIx32, configured->address, running->address);
		ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
	}
	ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08"PRIx32")", running->type, running->subtype, running->address);

	while (1) {
		doUpdate = false;
		getNewVersion(BINARY_INFO_FILENAME, newVersion);
		if (newVersion[0] != 0) {
			if (strcmp(newVersion, wifiSettings.firmwareVersion) != 0) {
				ESP_LOGI(TAG, "New firmware version available: %s", newVersion);
				doUpdate = true;
			} else
				ESP_LOGI(TAG, "Firmware up to date: %s", newVersion);
		} else
			ESP_LOGI(TAG, "Reading New firmware info failed");

		if (doUpdate) {
			ESP_LOGI(TAG, "Updating firmware to version: %s", newVersion);
			xTaskCreate(&updateFirmwareTask, "updateFirmwareTask", 2 * 8192, NULL, 5, &updateFWTaskh);
			vTaskDelay(100 / portTICK_PERIOD_MS);
			while (updateStatus == UPDATE_BUSY)
				vTaskDelay(100 / portTICK_PERIOD_MS);

			if (updateStatus == UPDATE_RDY) {
				strcpy(wifiSettings.firmwareVersion, newVersion);
				saveSettings();
				ESP_LOGI(TAG, "Update successfull, restarting system!");
				vTaskDelay(100 / portTICK_PERIOD_MS);
				esp_restart();
			} else {
				ESP_LOGI(TAG, "Update firmware failed!");
				vTaskDelay (10000/portTICK_PERIOD_MS);
			}
		}

// ********************* SPIFFS update ***************************

		doUpdate = false;
		getNewVersion(SPIFFS_INFO_FILENAME, newVersion);
		if (newVersion[0] != 0) {
			if (strcmp(newVersion, wifiSettings.SPIFFSversion) != 0) {
				ESP_LOGI(TAG, "New SPIFFS version available: %s", newVersion);
				doUpdate = true;
			} else
				ESP_LOGI(TAG, "SPIFFS up to date: %s", newVersion);
		} else
			ESP_LOGI(TAG, "Reading New SPIFFS info failed");

		if (doUpdate) {
			ESP_LOGI(TAG, "Updating SPIFFS to version: %s", newVersion);
			xTaskCreate(&updateSpiffsTask, "updateSpiffsTask", 2 * 8192, NULL, 5, &updateSPIFFSTaskh);
			vTaskDelay(100 / portTICK_PERIOD_MS);

			while (updateStatus == UPDATE_BUSY) // wait for task to finish
				vTaskDelay(100 / portTICK_PERIOD_MS);

			if (updateStatus == UPDATE_RDY) {
				ESP_LOGI(TAG, "SPIFFS flashed OK");
				strcpy(wifiSettings.SPIFFSversion, newVersion);
				saveSettings();
			} else
				ESP_LOGI(TAG, "Update SPIFFS failed!");
		}
		vTaskDelay(CONFIG_CHECK_FIRMWARWE_UPDATE_INTERVAL * 60 * 60 * 1000 / portTICK_PERIOD_MS);
	//	vTaskDelay(10000 / portTICK_PERIOD_MS);
	}
}


