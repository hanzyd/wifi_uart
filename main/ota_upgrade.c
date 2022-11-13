// Copyright 2017-2018 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "sdkconfig.h"
#include <esp_ota_ops.h>

#include <sys/param.h>

#include <esp_http_server.h>

#define OTA_BUF_SIZE CONFIG_OTA_BUF_SIZE

static void reset_task(void *pvParameters)
{
	vTaskDelay(1000 / portTICK_PERIOD_MS);

	esp_restart();

	while (1) {
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}

void star_reset_procedure(void)
{
	xTaskCreate(reset_task, "reset_task", 512, NULL, 1, NULL);
}

static char up_buf[512];

/* An HTTP POST handler */
esp_err_t upgrade_endpoint(httpd_req_t *req)
{
	esp_ota_handle_t ota_handle;
	const esp_partition_t *partition = NULL;
	int ret, remaining = req->content_len;
	const char *resp_str;
	esp_err_t err;

	partition = esp_ota_get_next_update_partition(NULL);
	if (partition == NULL) {
		resp_str = "Can't find partition\n";
		httpd_resp_send_chunk(req, resp_str, strlen(resp_str));
		return ESP_FAIL;
	}

	err = esp_ota_begin(partition, remaining, &ota_handle);
	if (err != ESP_OK) {
		resp_str = "Start OTA failed\n";
		httpd_resp_send_chunk(req, resp_str, strlen(resp_str));
		return ESP_FAIL;
	}

	resp_str = "Collecting OTA parts ";
	httpd_resp_send_chunk(req, resp_str, strlen(resp_str));

	while (remaining > 0) {

		/* Read the data for the request */
		ret = httpd_req_recv(req, up_buf, MIN(remaining, sizeof(up_buf)));
		if (ret <= 0) {
			if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
				/* Retry receiving if timeout occurred */
				continue;
			}
			resp_str = "Receive error\n";
			httpd_resp_send_chunk(req, resp_str, strlen(resp_str));
			return ESP_FAIL;
		}

		err = esp_ota_write(ota_handle, (const void *)up_buf, ret);
		if (err != ESP_OK) {
			resp_str = "Write OTA error\n";
			httpd_resp_send_chunk(req, resp_str, strlen(resp_str));
			return ESP_FAIL;
		} 

		remaining -= ret;
		if (remaining)
			resp_str = ".";
		else
			resp_str = " complete\n";

		httpd_resp_send_chunk(req, resp_str, strlen(resp_str));
	}

	err = esp_ota_end(ota_handle);
	if (err != ESP_OK) {
		resp_str = "Finish OTA failed\n";
		httpd_resp_send_chunk(req, resp_str, strlen(resp_str));
		return ESP_FAIL;
	}

	err = esp_ota_set_boot_partition(partition);
	if (err == ESP_OK) {
		resp_str = "Upgrade complete, rebooting ...\n";
		star_reset_procedure();
	} else {
		resp_str = "Can't set boot partition\n";
	}

	httpd_resp_send_chunk(req, resp_str, strlen(resp_str));

	// End response
	httpd_resp_send_chunk(req, NULL, 0);
	return err;
}
