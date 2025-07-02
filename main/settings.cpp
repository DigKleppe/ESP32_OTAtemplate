/*
 * settings.c
 *
 *  Created on: Nov 30, 2017
 *      Author: dig
 */
#include "esp_log.h"
#include "settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "wifiConnect.h"
#include <string.h>
#include <cerrno>

#define STORAGE_NAMESPACE "storage"

static const char *TAG = "Settings";

extern settingsDescr_t settingsDescr[];
extern int myRssi;
bool settingsChanged;

char checkstr[MAX_STRLEN+1];

const userSettings_t userSettingsDefaults = {
	CONFIG_MDNS_HOSTNAME ,
	"0.0",
	USERSETTINGS_CHECKSTR
};

userSettings_t userSettings;


extern "C" {

esp_err_t saveSettings(void) {
	nvs_handle_t my_handle;
	esp_err_t err = 0;

	err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
	} else {
		err = nvs_set_blob(my_handle, "WifiSettings",(const void *) &wifiSettings, sizeof(wifiSettings_t));
		err |= nvs_set_blob(my_handle, "userSettings",(const void *) &userSettings, sizeof(userSettings_t));

		switch (err) {
		case ESP_OK:
			ESP_LOGI(TAG, "settings written");
			break;
		default:
			ESP_LOGE(TAG, "Error (%s) writing!", esp_err_to_name(err));
		}
		err = nvs_commit(my_handle);

		switch (err) {
		case ESP_OK:
			ESP_LOGI(TAG, "Committed");
			break;
		default:
			ESP_LOGE(TAG, "Error (%s) commit", esp_err_to_name(err));
		}
		nvs_close(my_handle);
	}
	return err;
}

esp_err_t loadSettings() {
	nvs_handle_t my_handle;
	esp_err_t err = 0;
	bool doSave = false;

	err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &my_handle);
	size_t len;
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
	} else {
		ESP_LOGI(TAG, "reading SSID and password");
		len =  sizeof(wifiSettings_t);
		err = nvs_get_blob(my_handle, "WifiSettings", (void *) &wifiSettings, &len);
		len = sizeof(userSettings_t);
		err |= nvs_get_blob(my_handle, "userSettings",(void *) &userSettings, &len);
		switch (err) {
		case ESP_OK:
			ESP_LOGI(TAG, "OTABootSSID: %s", wifiSettings.SSID);
			break;
		case ESP_ERR_NVS_NOT_FOUND:
			ESP_LOGE(TAG, "The value is not initialized yet!");
			break;
		default:
			ESP_LOGE(TAG, "Error (%s) reading!", esp_err_to_name(err));
		}
		nvs_close(my_handle);
	}
// can be removed
	if (strcmp(wifiSettings.upgradeFileName, CONFIG_FIRMWARE_UPGRADE_FILENAME) != 0) {
		strcpy(wifiSettings.upgradeFileName, CONFIG_FIRMWARE_UPGRADE_FILENAME);
		doSave = true;  // set filename for OTA via factory firmware
	}

	if(strncmp(userSettings.checkstr,USERSETTINGS_CHECKSTR, strlen (USERSETTINGS_CHECKSTR) )	!= 0)
	{
		userSettings = userSettingsDefaults;
		wifiSettings = wifiSettingsDefaults;
		ESP_LOGE(TAG, "default usersettings loaded");
		doSave = true;  // set filename for OTA via factory firmware
	}

	if ( doSave)
		return saveSettings();
	else {
		ESP_LOGI(TAG, "usersettings loaded");
	}
	return err;

}
}

