/*
 * updateSpiffsTask.cpp
 *
 *  Created on: May 24, 2023
 *      Author: dig
 */
#include <string.h>
#include "errno.h"
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "updateTask.h"
#include "updateSpiffsTask.h"
#include "wifiConnect.h"
#include "settings.h"
#include "httpsReadFile.h"

static const char *TAG = "updateSPIFFSTask";

void updateSpiffsTask(void *pvParameter) {
	esp_err_t err;
	size_t binary_file_length = 0;
	char updateURL[96];
	httpsMssg_t mssg;
	bool rdy = false;
	bool started = false;
	int block = 0;
	int data_read;
	httpsRegParams_t httpsRegParams;
	uint8_t ota_write_data[BUFFSIZE + 1];

	ESP_LOGI(TAG, "Starting updateSpiffsTask");
	updateStatus = UPDATE_BUSY;

	const esp_partition_t *spiffsPartition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
	ESP_LOGI(TAG, "SPIFFS partition type %d subtype %d (offset 0x%08"PRIx32")", spiffsPartition->type, spiffsPartition->subtype, spiffsPartition->address);

	err = ESP_OK;

	httpsRegParams.httpsServer = wifiSettings.upgradeServer;
	strcpy(updateURL, wifiSettings.upgradeURL);
	strcat(updateURL, "//");
	strcat(updateURL, CONFIG_SPIFFS_UPGRADE_FILENAME);

	httpsRegParams.httpsURL = updateURL;

	httpsRegParams.destbuffer = ota_write_data;
	httpsRegParams.maxChars = sizeof(ota_write_data) ;

	xTaskCreate(&httpsGetRequestTask, "httpsGetRequestTask", 8192, (void*) &httpsRegParams, 5, NULL);
	vTaskDelay(10);
	/*deal with all receive packet*/

	while (!rdy && (err == ESP_OK)) {
		xQueueSend(httpsReqRdyMssgBox, &mssg, 0);
		if (xQueueReceive(httpsReqMssgBox, (void*) &mssg, ( UPDATETIMEOUT / portTICK_PERIOD_MS))) {
			data_read = mssg.len;
			block++;
			putchar('.');
		//	ESP_LOGI(TAG, "Reading block %d bytes %d ", block ,data_read);
			if (data_read < 0) {
				ESP_LOGE(TAG, "Error reading");
				err = !ESP_OK;
			}
			if (data_read == 0) {
				rdy = true;
			} else {
				if (!started) {
					started = true;
					err = esp_partition_erase_range(spiffsPartition, 0, spiffsPartition->size);
					if (err != ESP_OK) {
						ESP_LOGE(TAG, "spiffs partition erase failed: (%s)", esp_err_to_name(err));
						err = !ESP_OK;
					}
					else
						ESP_LOGI(TAG, "spiffs partition erased");
				}
				err = esp_partition_write(spiffsPartition, binary_file_length, (const void*) ota_write_data, data_read);
				if (err != ESP_OK) {
					ESP_LOGE(TAG, "Error ota write (%s)", esp_err_to_name(err));
				}
				binary_file_length += data_read;
			}
		}
	}

	if (!rdy) {
		do {
			xQueueSend(httpsReqRdyMssgBox, &mssg, 0);
			xQueueReceive(httpsReqMssgBox, (void*) &mssg, (1000 / portTICK_PERIOD_MS)); // wait for httpsGetRequestTask to end
		}while (mssg.len > 0);
	}
	else
		ESP_LOGI(TAG, "Ready written %d bytes %d blocks", binary_file_length, block);

	if ( err == ESP_OK)
		updateStatus = UPDATE_RDY;
	else
		updateStatus = UPDATE_ERROR;

	vTaskDelete (NULL);

}

