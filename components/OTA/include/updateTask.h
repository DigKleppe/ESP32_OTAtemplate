/*
 * updateTask.h
 *
 *  Created on: Jan 8, 2024
 *      Author: dig
 */

#ifndef COMPONENTS_OTA_INCLUDE_UPDATETASK_H_
#define COMPONENTS_OTA_INCLUDE_UPDATETASK_H_

#include "esp_err.h"
#define BINARY_INFO_FILENAME "firmWareVersion.txt"
#define SPIFFS_INFO_FILENAME "storageVersion.txt"

#define UPDATETIMEOUT (24 * 60 * 60) //seconds 

#define BUFFSIZE 		1024 // buffer size for http

esp_err_t getNewVersion (char * infoFileName , char * newVersion);
void updateTask(void *pvParameter);

extern volatile bool getNewVersionTaskFinished;

typedef enum {UPDATE_RDY, UPDATE_BUSY, UPDATE_ERROR } updateStatus_t;
extern volatile updateStatus_t updateStatus;

#endif /* COMPONENTS_OTA_INCLUDE_UPDATETASK_H_ */
