#!/usr/bin/env sh

esptool.py --chip esp8266 --port /dev/tty.usbserial-A50285BI --baud 115200 erase_flash

esptool.py --chip esp8266 --port /dev/tty.usbserial-A50285BI --baud 115200 \
    --before default_reset --after hard_reset \
    write_flash -z --flash_mode dio --flash_freq 40m --flash_size 2MB \
    0xd000  ./build/ota_data_initial.bin \
    0x0     ./build/bootloader/bootloader.bin \
    0x10000 ./build/wifi_uart.bin \
    0x8000  ./build/partitions_two_ota.bin


