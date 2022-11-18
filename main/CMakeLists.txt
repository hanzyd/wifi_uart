set(srcs
    "bridge.c"
    "http.c"
    "ota.c"
    "wifi.c"
	"nvm.c")

idf_component_register(SRCS "${srcs}")
