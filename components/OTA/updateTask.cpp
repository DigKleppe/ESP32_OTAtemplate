/*
 * getNewFirmWareVersionTask.cpp
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
#include "httpsRequest.h"
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

void getNewVersionTask(void *pvParameter) {
	esp_err_t err;
	char updateURL[96];

	httpsMssg_t mssg;
	bool rdy = false;
	httpsRegParams_t httpsRegParams;
	versionInfoParam_t *versionInfoParam = (versionInfoParam_t*) pvParameter;
	getNewVersionTaskFinished = false;

	httpsRegParams.httpsServer = wifiSettings.upgradeServer;
	strcpy(updateURL, wifiSettings.upgradeURL);
	strcat(updateURL, "//");
	strcat(updateURL, versionInfoParam->infoFileName);


	httpsRegParams.httpsURL = updateURL;
	httpsRegParams.destbuffer = (uint8_t*) versionInfoParam->dest;
	httpsRegParams.maxChars = MAX_STORAGEVERSIONSIZE - 1;

	int data_read;

	ESP_LOGI(TAG, "Starting getNewVersionTask");

	xTaskCreate(&httpsGetRequestTask, "httpsGetRequestTask", 2 * 8192, (void*) &httpsRegParams, 5, NULL); // todo stack minimize
	vTaskDelay(100 / portTICK_PERIOD_MS); // wait for messagebox to create and connection to make
	xQueueSend(httpsReqRdyMssgBox, &mssg, 0);
	if (xQueueReceive(httpsReqMssgBox, (void*) &mssg, UPDATETIMEOUT)) {
		if (mssg.len <= 0) {
			ESP_LOGE(TAG, "error reading info file: %s", BINARY_INFO_FILENAME);
			httpsRegParams.destbuffer[0] = -1;
		} else {
			if (mssg.len < MAX_STORAGEVERSIONSIZE) {
				httpsRegParams.destbuffer[mssg.len] = 0;
				ESP_LOGI(TAG, "New version: %s", httpsRegParams.destbuffer);
			} else {
				ESP_LOGE(TAG, "read version too long: %s", BINARY_INFO_FILENAME);
				httpsRegParams.destbuffer[0] = -1;
			}
		}
	}
	else {
		ESP_LOGE(TAG, "Timeout reading new version info: %s", BINARY_INFO_FILENAME);
	}

	while (mssg.len > 0) { // wait for httpsGetRequestTask to finish
		xQueueSend(httpsReqRdyMssgBox, &mssg, 0);
		xQueueReceive(httpsReqMssgBox, (void*) &mssg, UPDATETIMEOUT); // wait for httpsGetRequestTask to end
	};

	getNewVersionTaskFinished = true;
	(void) vTaskDelete(NULL);
}

esp_err_t getNewVersion(char *infoFileName, char *newVersion) {

	getNewVersionTaskFinished = false;

	versionInfoParam_t versionInfoParam;
	versionInfoParam.infoFileName = infoFileName;
	versionInfoParam.dest = newVersion;

	xTaskCreate(&getNewVersionTask, "getNewVersionTask", 8192, (void*) &versionInfoParam, 5, NULL);

	while (!getNewVersionTaskFinished)
		vTaskDelay(100 / portTICK_PERIOD_MS);
	if ((int8_t) newVersion[0] != -1)
		return ESP_OK;
	else
		return ESP_FAIL;
}

void updateTask(void *pvParameter) {
	bool doUpdate;
	char newVersion[MAX_STORAGEVERSIONSIZE];
	TaskHandle_t updateFWTaskh;
	TaskHandle_t updateSPIFFSTaskh;

	// if ((strcmp(wifiSettings.upgradeFileName, CONFIG_FIRMWARE_UPGRADE_FILENAME) != 0) || (strcmp(wifiSettings.upgradeURL, CONFIG_DEFAULT_FIRMWARE_UPGRADE_URL) != 0)) {
	// 	strcpy(wifiSettings.upgradeFileName, CONFIG_FIRMWARE_UPGRADE_FILENAME);
	// 	strcpy(wifiSettings.upgradeURL, CONFIG_DEFAULT_FIRMWARE_UPGRADE_URL);
	// 	saveSettings();
	// todo
	// }


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
	//	vTaskDelay(CONFIG_CHECK_FIRMWARWE_UPDATE_INTERVAL / portTICK_PERIOD_MS);
		vTaskDelay(10000 / portTICK_PERIOD_MS);
	}
}


