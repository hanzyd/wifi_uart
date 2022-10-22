/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "driver/uart.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"

#include "nvs_flash.h"

#include "lwip/sockets.h"


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

#define UART_BUF_SIZE 1024
#define SRV_PORT 8888

static int g_retry_num = 0;
/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t g_wifi_events;
static QueueHandle_t uart_queue;
static char rx_buff[128];
static uint8_t tx_buff[128];

static int init_wifi_server(int backlog)
{
	struct sockaddr_in srv_addr;
	int srv_sock;

	srv_addr.sin_family = AF_INET;
	srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	srv_addr.sin_port = htons(SRV_PORT);

	srv_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (srv_sock < 0)
		return -1;

	// int opt = 1;
	// setsockopt(srv_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));
	// setsockopt(srv_sock, SOL_SOCKET, TCP_NODELAY, &opt, sizeof(int));

	int err = bind(srv_sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
	if (err != 0) {
		close(srv_sock);
		return -1;
	}

	err = listen(srv_sock, backlog);
	if (err != 0) {
		close(srv_sock);
		return -1;
	}

	// fcntl(srv_sock, F_SETFL, O_NONBLOCK);

	return srv_sock;
}

static int wait_for_wifi_client(int srv_sock)
{
	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);
	int sock;

	sock = accept(srv_sock, (struct sockaddr *)&addr, &len);
	if (sock < 0)
		return sock;

	// fcntl(sock, F_SETFL, O_NONBLOCK);

	int opt = 1;
	setsockopt(sock, SOL_SOCKET, TCP_NODELAY, &opt, sizeof(int));

	return sock;
}

static void recv_wifi_write_uart_task(void *arg)
{
	int *client = (int *)arg;
	ssize_t len;

	/* Block for 100ms. */
	const TickType_t xDelay = 100 / portTICK_PERIOD_MS;

	while (true) {

		if (*client < 0) {
			vTaskDelay(xDelay);
			continue;
		}

		len = recv(*client, rx_buff, sizeof(rx_buff), 0);
		if (len > 0) {
			uart_write_bytes(UART_NUM_0, rx_buff, len);
		} else {
			close(*client);
			*client = -1;
		}
	}

	vTaskDelete(NULL);
}

static void init_uart(void)
{
	uart_config_t uart_config = {
		// writing down the conditions of the uart.
		.baud_rate = 115200,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE
	};

	// We won't use a buffer for sending data.
	// starting UART with a rx and tx buffer of 2048
	// with no queue and no interrupt function.
	uart_driver_install(UART_NUM_0, UART_BUF_SIZE * 2, 0, 2, &uart_queue, 0);
	uart_param_config(UART_NUM_0, &uart_config);
}

static bool read_uart_send_wifi(int client, size_t length)
{
	ssize_t len, offs;
	int retry;

	while (length) {

		len = length < UART_BUF_SIZE ? length : UART_BUF_SIZE;
		len = uart_read_bytes(UART_NUM_0, tx_buff, len, 20 / portTICK_RATE_MS);
		if (len < 0)
			return true;

		length -= len;

		retry = 0;
		for (offs = 0; offs < len; offs += len) {

			len = send(client, &tx_buff[offs], len - offs, 0);
			if (len == 0)
				retry++;

			if (len < 0 || retry > 5)
				return false;
		}
	}

	return true;
}

static void read_uart_send_wifi_task(void *arg)
{
	int *client = (int *)arg;
	uart_event_t event;

	for (;;) {

		// Waiting for UART event.
		if (xQueueReceive(uart_queue, (void *)&event, portMAX_DELAY)) {

			switch (event.type) {
				// Event of UART receiving data
				// We'd better handler data event fast, there would be much more
				// data events than other types of events. If we take too much
				// time on data event, the queue might be full.
			case UART_DATA:
				if (*client < 0) {
					uart_flush_input(UART_NUM_0);				
				} else {
					bool ok = read_uart_send_wifi(*client, event.size);
					if (!ok) {
						close(*client);
						*client = -1;
					}
				}
				break;

				// Event of HW FIFO overflow detected
			case UART_FIFO_OVF:
				// If fifo overflow happened, you should consider adding flow
				// control for your application. The ISR has already reset the
				// rx FIFO, As an example, we directly flush the rx buffer here
				// in order to read more data.
				uart_flush_input(UART_NUM_0);
				xQueueReset(uart_queue);
				break;

				// Event of UART ring buffer full
			case UART_BUFFER_FULL:
				// If buffer full happened, you should consider increasing your
				// buffer size As an example, we directly flush the rx buffer
				// here in order to read more data.
				uart_flush_input(UART_NUM_0);
				xQueueReset(uart_queue);
				break;

			case UART_PARITY_ERR:
				break;

				// Event of UART frame error
			case UART_FRAME_ERR:
				break;

				// Others
			default:
				break;
			}
		}
	}

	vTaskDelete(NULL);
}


static void wifi_event_handler(void *arg, esp_event_base_t event_base,
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
	}
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
							 int32_t event_id, void *event_data)
{
	switch (event_id) {
	case IP_EVENT_STA_GOT_IP:
		g_retry_num = 0;
		xEventGroupSetBits(g_wifi_events, WIFI_CONNECTED_BIT);
		break;
	}
}

static bool init_wifi_station_and_connect(void)
{
	g_wifi_events = xEventGroupCreate();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	esp_wifi_init(&cfg);

	esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
							   &wifi_event_handler, NULL);
	esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler,
							   NULL);

	wifi_config_t wifi_config = {
		.sta = {.ssid = EXAMPLE_ESP_WIFI_SSID,
				.password = EXAMPLE_ESP_WIFI_PASS},
	};

	/* Setting a password implies station will connect to all security modes
	 * including WEP/WPA. However these modes are deprecated and not advisable
	 * to be used. Incase your Access point doesn't support WPA2, these mode can
	 * be enabled by commenting below line */

	if (strlen((char *)wifi_config.sta.password)) {
		wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	}

	esp_wifi_set_storage(WIFI_STORAGE_RAM);
	esp_wifi_set_mode(WIFI_MODE_STA);
	esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
	esp_wifi_start();

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT)
	 * or connection failed for the maximum number of re-tries (WIFI_FAIL_BIT).
	 * The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(g_wifi_events,
										   WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
										   pdFALSE, pdFALSE, portMAX_DELAY);

	if (bits & WIFI_CONNECTED_BIT)
		return true;

	return false;
}

static void bridge_task(void *pvParameters)
{
	int client = -1;
	int srv_sock;

	bool ok = init_wifi_station_and_connect();
	if (!ok)
		vTaskDelete(NULL);

	srv_sock = init_wifi_server(2); // Initial server configuration.
	if (srv_sock < 0)
		vTaskDelete(NULL);

	init_uart();

	xTaskCreate(read_uart_send_wifi_task, "u2w", 1024, &client, 2, NULL);
	xTaskCreate(recv_wifi_write_uart_task, "w2u", 1024, &client, 2, NULL);

	for (;;) {
		int new_client;

		new_client = wait_for_wifi_client(srv_sock);
		if (new_client < 0)
			continue;

		if (client >= 0) {
			close(new_client);
			continue;
		}

		client = new_client;
	}
}

void app_main()
{
	nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();

	xTaskCreate(bridge_task, "bridge_task", 1024 * 2, NULL, 2, NULL);
}
