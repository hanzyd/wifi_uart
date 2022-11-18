/* Simple HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_system.h"

#include <esp_http_server.h>
#include <esp_ota_ops.h>

#include "nvm.h"

static esp_err_t echo_endpoint(httpd_req_t *req)
{
	char buf[100];
	int ret, remaining = req->content_len;

	while (remaining > 0) {
		/* Read the data for the request */
		if ((ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <=
			0) {
			if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
				/* Retry receiving if timeout occurred */
				continue;
			}
			return ESP_FAIL;
		}

		/* Send back the same data */
		httpd_resp_send_chunk(req, buf, ret);
		remaining -= ret;
	}

	// End response
	httpd_resp_send_chunk(req, NULL, 0);
	return ESP_OK;
}

static httpd_uri_t echo = {
	.uri = "/echo",
	.method = HTTP_POST,
	.handler = echo_endpoint,
	.user_ctx = NULL
};


static esp_err_t ssid_endpoint(httpd_req_t *req)
{
	int ret, len = req->content_len;
	char buf[MAX_SSID_LEN];
	bool ok;

	/* Read the SSID from the request */
	ret = httpd_req_recv(req, buf, MIN(len, sizeof(buf)));
	if (ret <= 0) {
		if (ret == HTTPD_SOCK_ERR_TIMEOUT)
			httpd_resp_send_408(req);
		return ESP_FAIL;
	}

	ok = nvm_write_key("ssid", (uint8_t *)buf, ret);
	if (ok) {
		/* Send back the same data */
		httpd_resp_send_chunk(req, buf, ret);
	} else {
		const char *str = "Can't save SSID\n"; 
		httpd_resp_send_chunk(req, str, strlen(str));
	}

	// End response
	httpd_resp_send_chunk(req, NULL, 0);
	return ESP_OK;
}

static httpd_uri_t ssid = {
	.uri = "/ssid",
	.method = HTTP_POST,
	.handler = ssid_endpoint,
	.user_ctx = NULL
};

static esp_err_t password_endpoint(httpd_req_t *req)
{
	int ret, len = req->content_len;
	char buf[MAX_PASSPHRASE_LEN];
	bool ok;

	/* Read the SSID from the request */
	ret = httpd_req_recv(req, buf, MIN(len, sizeof(buf)));
	if (ret <= 0) {
		if (ret == HTTPD_SOCK_ERR_TIMEOUT)
			httpd_resp_send_408(req);
		return ESP_FAIL;
	}

	ok = nvm_write_key("password", (uint8_t *)buf, ret);
	if (ok) {
		/* Send back the same data */
		httpd_resp_send_chunk(req, buf, ret);
	} else {
		const char *str = "Can't save password\n"; 
		httpd_resp_send_chunk(req, str, strlen(str));
	}

	// End response
	httpd_resp_send_chunk(req, NULL, 0);
	return ESP_OK;
}

static httpd_uri_t password = {
	.uri = "/password",
	.method = HTTP_POST,
	.handler = password_endpoint,
	.user_ctx = NULL
};

void star_reset_procedure(void);

static esp_err_t reset_endpoint(httpd_req_t *req)
{
	char buf;
	int ret;

	if ((ret = httpd_req_recv(req, &buf, 1)) <= 0) {
		if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
			httpd_resp_send_408(req);
		}
		return ESP_FAIL;
	}

	/* Respond with empty body */
	httpd_resp_send(req, NULL, 0);

	star_reset_procedure();
	return ESP_OK;
}

static httpd_uri_t reset = {
	.uri = "/reset",
	.method = HTTP_POST,
	.handler = reset_endpoint,
	.user_ctx = NULL
};

esp_err_t upgrade_endpoint(httpd_req_t *req);

static httpd_uri_t upgrade = {
	.uri = "/upgrade",
	.method = HTTP_POST,
	.handler = upgrade_endpoint,
	.user_ctx = NULL
};

static const char *reset_codes[] = {
    "unknown",
    "power-on",
    "external pin",
    "esp_restart",
    "exception/panic",
    "interrupt watchdog",
    "task watchdog",
    "other watchdogs",
    "exiting deep sleep mode",
    "brownout reset",
    "reset over SDIO",
    "fast reboot"
};

esp_err_t info_endpoint(httpd_req_t *req)
{
	const esp_partition_t* part;
	char resp_str[128];
	const char *why, *label, *app;
	const esp_app_desc_t *desc;

	part = esp_ota_get_running_partition();
	if (part)
		label = part->label;
	else
		label = "unknown";

	esp_reset_reason_t rst = esp_reset_reason();

	if (rst >= sizeof(reset_codes)/sizeof(reset_codes[0]))
		why = "unknown";
	else
		why = reset_codes[rst];

	if (strcmp(label, "ota_0") == 0)
		app = "app1.bin";
	else if (strcmp(label, "ota_1") == 0)
		app = "app2.bin";
	else
		app = "unknown";

	desc = esp_ota_get_app_description();

	snprintf(resp_str, sizeof(resp_str),
			 "Reset: %s, Active: %s, Name: %s, Version: %s\n", why, app,
			 desc->project_name, desc->version);
	httpd_resp_send(req, resp_str, strlen(resp_str));
	return ESP_OK;
}

static httpd_uri_t info = {
	.uri = "/info",
    .method = HTTP_GET,
	.handler = info_endpoint,
	.user_ctx = NULL
};

static httpd_handle_t start_webserver(void)
{
	httpd_handle_t server = NULL;
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();

	// Start the httpd server
	if (httpd_start(&server, &config) == ESP_OK) {
		// Set URI handlers
		httpd_register_uri_handler(server, &echo);
		httpd_register_uri_handler(server, &upgrade);
		httpd_register_uri_handler(server, &reset);
		httpd_register_uri_handler(server, &info);
		httpd_register_uri_handler(server, &ssid);
		httpd_register_uri_handler(server, &password);
		return server;
	}

	return NULL;
}

static void stop_webserver(httpd_handle_t server)
{
	// Stop the httpd server
	httpd_stop(server);
}

static httpd_handle_t g_server = NULL;

static void disconnect_handler(void *arg, esp_event_base_t event_base,
							   int32_t event_id, void *event_data)
{
	httpd_handle_t *server = (httpd_handle_t *)arg;

	switch (event_id) {
	case WIFI_EVENT_AP_STOP:
	case WIFI_EVENT_STA_DISCONNECTED:
		if (*server) {
			stop_webserver(*server);
			*server = NULL;
		}
		break;
	case WIFI_EVENT_AP_START:
		if (*server == NULL)
			*server = start_webserver();
		break;
	}
}

static void connect_handler(void *arg, esp_event_base_t event_base,
							int32_t event_id, void *event_data)
{
	httpd_handle_t *server = (httpd_handle_t *)arg;
	
	if (*server == NULL)
		*server = start_webserver();
}

void httpd_register_for_events(void)
{
	esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, connect_handler,
							   &g_server);
	esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
							   disconnect_handler, &g_server);
}
