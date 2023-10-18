# UART to WiFi application


## Clone sources 

```
$ cd ~/src
$ git clone https://github.com/espressif/ESP8266_RTOS_SDK.git
$ git clone https://github.com/hanzyd/wifi_uart.git
```
## Setup build environment 

```
$ export IDF_PATH=~/src/ESP8266_RTOS_SDK
$ export IDF_PYTHON_ENV_PATH=/path/to/python3

$ . $IDF_PATH/install.sh
$ . $IDF_PATH/export.sh
```

## Configure the project

```
make  menuconfig
```

* Set serial port under Serial Flasher Options.

* Set WiFi SSID and WiFi Password and Maximum retry under Example Configuration Options.

## Build and Flash

Build the project and flash it to the board, then run monitor tool to view serial output:

```
$ make all flash monitor
```

(To exit the serial monitor, type ``Ctrl-]``.)

See the Getting Started Guide for full steps to configure and use ESP-IDF to build projects.

## What this application do

When started for the first time it will try to connect to AP specified in configuration
If no AP is found it will start AP on its own with SSID "UART mac:address:of:the:device".
Password will be the same as the SSID name. Once connected to the AP you can confiture
new AP and password to be used on the reboot.

Find IP address of the device

```
$ arp espressif
...

wifi_uart_ip="ip.address"

Device will host HTTP server with the following endpoints.

$ curl -X GET ${wifi_uart_ip}/info
Reset: external pin, Active: app1.bin, Name: wifi_uart, Version: 625ef2c

$ curl -X POST -d "This is an echo test message" ${wifi_uart_ip}/echo -o loopback.txt

$ cat loopback.txt
This is an echo test message

$ curl -X POST -d "SUSE Labs" ${wifi_uart_ip}/ssid

$ curl -X POST -d "Welcome" ${wifi_uart_ip}/password

$ curl -X POST -d "1" ${wifi_uart_ip}/reset

$ curl -X POST -d "@app2.bin" ${wifi_uart_ip}/upgrade

$ socat -,echo=0,raw,escape=0x0f TCP4:${wifi_uart_ip}:8888,keepalive,keepidle=10,keepintvl=10,keepcnt=2
```
