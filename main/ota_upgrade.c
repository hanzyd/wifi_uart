#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"


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
