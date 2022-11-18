
#include <string.h>

#include <esp_err.h>
#include <nvs_flash.h>

#define MY_NAMESPACE "wuapp"

bool nvm_read_key(const char *key, uint8_t val[], size_t *len)
{
	nvs_handle_t nvs;
	esp_err_t sta;

	memset(val, 0, *len);
	sta = nvs_open(MY_NAMESPACE, NVS_READONLY, &nvs);
	if (sta != ESP_OK)
		return false;

	sta = nvs_get_blob(nvs, key, val, len);

	nvs_close(nvs);

	return sta == ESP_OK;
}

bool nvm_write_key(const char *key, uint8_t val[], size_t len)
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
