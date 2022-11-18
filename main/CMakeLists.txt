set(srcs
    "uart_bridge.c"
    "http_server.c"
    "ota_upgrade.c"
    "wifi_code.c"
	"nvm.c")

idf_component_register(SRCS "${srcs}")
