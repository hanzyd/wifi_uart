/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <driver/uart.h>
#include <esp_wifi.h>
#include <esp_netif.h>

#include <nvs_flash.h>

#include <lwip/sockets.h>

#include "wifi.h"

#define UART_BUF_SIZE 1024
#define SRV_PORT 8888

static QueueHandle_t uart_queue;
static char rx_buff[UART_BUF_SIZE];
static uint8_t tx_buff[UART_BUF_SIZE];


static void close_sock(int *sock)
{
	shutdown(*sock, SHUT_RDWR);
	close(*sock);
	*sock = -1;
}

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
					if (!ok)
						close_sock(client);
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

void httpd_register_for_events(void);
bool wifi_start_sta_and_connect(void);
void wifi_start_ap(void);

static void bridge_task(void *pvParameters)
{
	int client = -1;
	int srv_sock;

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	esp_wifi_init(&cfg);

	httpd_register_for_events();

	bool ok = wifi_start_sta_and_connect();
	if (!ok)
		wifi_start_ap();

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
