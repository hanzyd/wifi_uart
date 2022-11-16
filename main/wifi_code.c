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
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"


/* The examples use WiFi configuration that you can set via project
   configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY CONFIG_ESP_MAXIMUM_RETRY

/* The event group allows multiple bits for each event, but we only care about
 * two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

#define MY_NAMESPACE	"wuapp"

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t g_wifi_events = NULL;
static int g_retry_num = 0;

static void wifi_sta_events(void *arg, esp_event_base_t event_base,
							int32_t event_id, void *event_data)
{
	switch (event_id) {
	case WIFI_EVENT_STA_START:
		esp_wifi_connect();
		break;

	case WIFI_EVENT_STA_DISCONNECTED:
		if (g_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
			esp_wifi_connect();
			g_retry_num++;
		} else {
			xEventGroupSetBits(g_wifi_events, WIFI_FAIL_BIT);
		}
		break;
	default:
		break;
	}
}

static void wifi_sta_ip_events(void *arg, esp_event_base_t event_base,
							   int32_t event_id, void *event_data)
{
	switch (event_id) {
	case IP_EVENT_STA_GOT_IP:
		g_retry_num = 0;
		xEventGroupSetBits(g_wifi_events, WIFI_CONNECTED_BIT);
		break;
	default:
		break;
	}
}

bool read_wifi_ap_key(const char *key, uint8_t val[], size_t *len)
{
	nvs_handle_t nvs;
	esp_err_t sta;

	sta = nvs_open(MY_NAMESPACE, NVS_READONLY, &nvs);
	if (sta != ESP_OK) 
		return false;

	sta = nvs_get_blob(nvs, key, val, len);

	nvs_close(nvs);

	return sta == ESP_OK;
}

bool write_wifi_ap_key(const char *key, uint8_t val[], size_t len)
{
	nvs_handle_t nvs;
	esp_err_t sta;

	sta = nvs_open(MY_NAMESPACE, NVS_READWRITE, &nvs);
	if (sta != ESP_OK) 
		return false;

	sta = nvs_set_blob(nvs, key, val, len);

	nvs_close(nvs);

	return sta == ESP_OK;
}


bool start_wifi_sta_and_connect(void)
{
	uint8_t ssid[MAX_SSID_LEN], password[MAX_PASSPHRASE_LEN];
	size_t len;
	bool ok;

	if (!g_wifi_events)
		g_wifi_events = xEventGroupCreate();

	esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_sta_events,
							   NULL);
	esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
							   &wifi_sta_ip_events, NULL);

	wifi_config_t config = {
		.sta = {
			.ssid = EXAMPLE_ESP_WIFI_SSID,
			.password = EXAMPLE_ESP_WIFI_PASS,
			.threshold.authmode = WIFI_AUTH_WPA2_PSK
		},
	};

	esp_wifi_set_storage(WIFI_STORAGE_FLASH);
	esp_wifi_set_mode(WIFI_MODE_STA);

	len = sizeof(ssid);
	ok = read_wifi_ap_key("ssid", ssid, &len);
	if (ok) 
		memcpy(config.sta.ssid, ssid, len);

	/* WiFi stack expect '\0' terminated string */
	len = sizeof(password) - 1;
	ok = read_wifi_ap_key("password", password, &len);
	if (ok) {
		memset(config.sta.password, 0, sizeof(config.sta.password));
		memcpy(config.sta.password, password, len);
	}

	esp_wifi_set_config(ESP_IF_WIFI_STA, &config);
	esp_wifi_start();

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT)
	 * or connection failed for the maximum number of re-tries (WIFI_FAIL_BIT).
	 * The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(g_wifi_events,
										   WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
										   pdFALSE, pdFALSE, portMAX_DELAY);

	if (bits & WIFI_CONNECTED_BIT)
		return true;

	esp_wifi_stop();
	esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_sta_events);
	esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_sta_ip_events);
	xEventGroupClearBits(g_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
	return false;
}

static void wifi_ap_events(void *arg, esp_event_base_t event_base,
						   int32_t event_id, void *event_data)
{
	switch (event_id) {
		case WIFI_EVENT_AP_STACONNECTED:
		break;
		case WIFI_EVENT_AP_STADISCONNECTED:
		break;
	}
}

void start_wifi_ap(void)
{
	wifi_config_t config;
	uint8_t pass[64];
	uint8_t mac[6];
	char ssid[32];
	size_t len;
	bool ok;

	esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
							   &wifi_ap_events, NULL);

    esp_efuse_mac_get_default(mac);

	len = snprintf(ssid, sizeof(ssid), "UART %02x%02x%02x", mac[2], mac[1], mac[0]);

	memcpy(config.ap.ssid, ssid, sizeof(config.ap.ssid));
	config.ap.ssid_len = len;

	len = sizeof(pass);
	ok = read_wifi_ap_key("password", pass, &len);
	if (ok)
		memcpy(config.ap.password, pass, len);
	else
		memcpy(config.ap.password, ssid, MIN(sizeof(config.ap.password), sizeof(ssid)));
	config.ap.max_connection = 3;
	config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

	esp_wifi_set_mode(WIFI_MODE_AP);
	esp_wifi_set_config(ESP_IF_WIFI_AP, &config);
	esp_wifi_start();
}


void stop_wifi_ap(void)
{
	esp_wifi_stop();
	esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_ap_events);
}
