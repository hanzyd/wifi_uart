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
