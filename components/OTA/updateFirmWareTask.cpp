/*
 * updateFirmWareTask.cpp
 *
 *  Created on: Jan 22, 2024
 *      Author: dig
 */

#include <inttypes.h>
#include <string.h>

#include "esp_app_format.h"
#include "esp_flash_partitions.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "updateTask.h"

#include "httpsReadFile.h"
#include "settings.h"
#include "wifiConnect.h"

//#define DOWNLOAD_ONLY

static uint8_t ota_write_data[BUFFSIZE];

static const char *TAG = "updateFirmwareTask";

void updateFirmwareTask(void *pvParameter) {
	esp_err_t err = ESP_OK;
	esp_ota_handle_t update_handle = 0;
	char updateURL[96];
	httpsRegParams_t httpsRegParams;
	size_t binary_file_length = 0;
	httpsMssg_t mssg;
	bool rdy = false;
	bool doUpdate = false;
	bool image_header_was_checked = false;
	int data_read;
	int block = 0;

	ESP_LOGI(TAG, "Starting updateFirmwareTask");

	updateStatus = UPDATE_BUSY;

	httpsRegParams.httpsServer = wifiSettings.upgradeServer;
	strcpy(updateURL, wifiSettings.upgradeURL);
	strcat(updateURL, "/");
	strcat(updateURL, wifiSettings.upgradeFileName);
	httpsRegParams.httpsURL = updateURL;
	httpsRegParams.destbuffer = ota_write_data;
	httpsRegParams.maxChars = sizeof(ota_write_data);

	const esp_partition_t *configured = esp_ota_get_boot_partition();
	const esp_partition_t *running = esp_ota_get_running_partition();
	const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
	if (update_partition == NULL) {
		ESP_LOGE(TAG, "update_partition not valid");
		updateStatus = UPDATE_ERROR;
		vTaskDelete( NULL);	
	}
	
	if (configured != running) {
		ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08" PRIx32 ", but running from offset 0x%08" PRIx32, configured->address,
				 running->address);
		ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
	}
	ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08" PRIx32 ")", running->type, running->subtype, running->address);

	ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%" PRIx32, update_partition->subtype, update_partition->address);
	if ( httpsReqRdyMssgBox) {
		xQueueSend(httpsReqRdyMssgBox, &mssg, 0);
	}
	xTaskCreate(&httpsGetRequestTask, "httpsGetRequestTask", 8192, (void *)&httpsRegParams, 5, NULL);
	vTaskDelay(10);

	while (!rdy && (err == ESP_OK)) {
		xQueueSend(httpsReqRdyMssgBox, &mssg, 0);
		if (xQueueReceive(httpsReqMssgBox, (void *)&mssg, (CONFIG_OTA_RECV_TIMEOUT / portTICK_PERIOD_MS))) {
			data_read = mssg.len;
			block++;
			//	ESP_LOGI(TAG, "Reading block %d bytes %d ", block ,data_read);
			putchar('.');
			if (data_read < 0) {
				ESP_LOGE(TAG, "Error: SSL data read error");
				err = !ESP_OK;
			} else {

				if (data_read == 0) {
					ESP_LOGI(TAG, "Ready written %d bytes %d mssgs", binary_file_length, block);
					rdy = true;
				} else {
					if (image_header_was_checked == false) {
						esp_app_desc_t new_app_info;
						if (data_read > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
							memcpy(&new_app_info, &ota_write_data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)],
								   sizeof(esp_app_desc_t));
							ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);
							ESP_LOGI(TAG, "New firmware project: %s", new_app_info.project_name);

							esp_app_desc_t running_app_info;
							if (esp_ota_get_partition_description(update_partition, &running_app_info) == ESP_OK) {
								ESP_LOGI(TAG, "Project name: %s", running_app_info.project_name);
								ESP_LOGI(TAG, "Old internal firmware version: %s",
										 running_app_info.version); // internal firmware version, not used here
							} else
								ESP_LOGI(TAG, "Cannot read info running app");

							if (memcmp(new_app_info.version, running_app_info.version, sizeof(new_app_info.version)) == 0) {
								ESP_LOGW(TAG, "Current running internal version is the same as a new."); // internal firmware version, not used here
							}
							image_header_was_checked = true;
#ifndef DOWNLOAD_ONLY
							err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
							if (err != ESP_OK) {
								ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
								esp_ota_abort(update_handle);
								err = !ESP_OK;
							}
							else
								ESP_LOGI(TAG, "esp_ota_begin succeeded");
#endif								

						} else {
							ESP_LOGE(TAG, "received failed");
#ifndef DOWNLOAD_ONLY
							esp_ota_abort(update_handle);
#endif
							err = !ESP_OK;
						}
						//						ESP_LOGI(TAG, "read :%d %d", data_read, block++);
					} // end if (image_header_was_checked == false)

					if (err == ESP_OK) {
#ifndef DOWNLOAD_ONLY
						err = esp_ota_write(update_handle, (const void *)ota_write_data, data_read);
						if (err != ESP_OK) {
							ESP_LOGE(TAG, "Error ota write (%s)", esp_err_to_name(err));
							esp_ota_abort(update_handle);
						}
#endif
					}
					binary_file_length += data_read;
					//			ESP_LOGD(TAG, "Written image length %d", binary_file_length);
				}
			}
		} else {
			ESP_LOGE(TAG, "read firmware timeout");

#ifndef DOWNLOAD_ONLY
			if (update_handle)
				esp_ota_abort(update_handle);
#endif
			err = !ESP_OK;
		}

	} // end while (! rdy && !err)
	if ( binary_file_length == 0) {
		ESP_LOGE(TAG, "No data received");
		err = !ESP_OK;
	}
	if (err == ESP_OK) {
		ESP_LOGI(TAG, "Total Write binary data length: %d", binary_file_length);
#ifndef DOWNLOAD_ONLY
		err |= esp_ota_end(update_handle);
#endif
		if (err != ESP_OK) {
			if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
				ESP_LOGE(TAG, "Image validation failed, image is corrupted (%s)", esp_err_to_name(err));
			} else {
				ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
			}
		}
		if (err == ESP_OK) {
			ESP_LOGI(TAG, "Image written successful");
#ifndef DOWNLOAD_ONLY
			err = esp_ota_set_boot_partition(update_partition);
#endif
			if (err != ESP_OK) {
				ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
			}
		}
	}

	if (err == ESP_OK)
		updateStatus = UPDATE_RDY;
	else
		updateStatus = UPDATE_ERROR;

	vTaskDelete(NULL);
}
