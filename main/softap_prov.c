/*  WiFi softAP Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <string.h>

#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"


static void wifi_ap_events_handler(void *arg, esp_event_base_t event_base,
							   int32_t event_id, void *event_data)
{
	switch (event_id) {
		case WIFI_EVENT_AP_STACONNECTED:
		break;
		case WIFI_EVENT_AP_STADISCONNECTED:
		break;
	}
}

void wifi_init_ap(void)
{
	wifi_config_t config;
	uint8_t mac[6];
	char ssid[32];
	int len;

	esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
							   &wifi_ap_events_handler, NULL);

    esp_efuse_mac_get_default(mac);

	len = snprintf(ssid, sizeof(ssid), "UART %02x%02x%02x", mac[2], mac[1], mac[0]);

	memcpy(config.ap.ssid, ssid, sizeof(config.ap.ssid));
	memcpy(config.ap.password, ssid, MIN(sizeof(config.ap.password), sizeof(ssid)));
	config.ap.ssid_len = len;
	config.ap.max_connection = 3;
	config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

	esp_wifi_set_mode(WIFI_MODE_AP);
	esp_wifi_set_config(ESP_IF_WIFI_AP, &config);
	esp_wifi_start();
}
