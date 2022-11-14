/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "freertos/FreeRTOS.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_system.h"
#include <string.h>

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"

#define UART_BUF_SIZE 1024
#define SRV_PORT 8888

static QueueHandle_t uart_queue;

static char rx_buff[128];
static uint8_t tx_buff[128];

static int init_wifi_server(int backlog)
{
	struct sockaddr_in srv_addr;
	int srv_sock;

	srv_addr.sin_family = AF_INET;
	// Grabs connected ip address which is set by the wifi_init_sta
	srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	srv_addr.sin_port = htons(SRV_PORT);

	// initialize tcp socket AF_INET refers to IPV4 addresses and
	// SOCK_STREAM saids initiate socket as tcp socket.
	srv_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (srv_sock < 0)
		return -1;

	// int opt = 1;
	// setsockopt(srv_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));
	// setsockopt(srv_sock, SOL_SOCKET, TCP_NODELAY, &opt, sizeof(int));

	int err = bind(srv_sock, (struct sockaddr*)&srv_addr, sizeof(srv_addr));
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

	sock = accept(srv_sock, (struct sockaddr*)&addr, &len);
	if (sock < 0)
		return sock;

	// fcntl(sock, F_SETFL, O_NONBLOCK);

	int opt = 1;
	setsockopt(sock, SOL_SOCKET, TCP_NODELAY, &opt, sizeof(int));

	return sock;
}

static void recv_wifi_write_uart_task(void* arg)
{
	int* client = (int*)arg;
	ssize_t len;

	while (true) {

		if (*client < 0)
			continue;

		len = recv(*client, rx_buff, sizeof(rx_buff), 0);
		if (len <= 0) {
			close(*client);
			*client = -1;
			continue;
		}

		uart_write_bytes(UART_NUM_0, rx_buff, len);
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

			if (len < 0 || retry > 5) {
				close(client);
				return false;
			}
		}
	}

	return true;
}

static void read_uart_send_wifi_task(void* arg)
{
	int client = -1;
	uart_event_t event;

	for (;;) {

		// Waiting for UART event.
		if (xQueueReceive(uart_queue, (void*)&event, portMAX_DELAY)) {

			switch (event.type) {
				// Event of UART receiving data
				// We'd better handler data event fast, there would be much more
				// data events than other types of events. If we take too much time
				// on data event, the queue might be full.
			case UART_DATA:
				if (client < 0) {
					uart_flush_input(UART_NUM_0);				
				} else {
					bool ok = read_uart_send_wifi(client, event.size);
					if (!ok) 
						client = -1;
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

				// Event of UART frame error
			case UART_EVENT_MAX:
				client = (int)event.size;
				break;

				// Others
			default:
				break;
			}
		}
	}

	vTaskDelete(NULL);
}

void bridge_task(void* pvParameters)
{
	int client = -1;
	int srv_sock;

	srv_sock = init_wifi_server(2); // Initial server configuration.
	if (srv_sock < 0)
		return;

	init_uart();

	xTaskCreate(read_uart_send_wifi_task, "u2w", 512, NULL, 2, NULL);
	xTaskCreate(recv_wifi_write_uart_task, "w2u", 512, NULL, 2, NULL);

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
