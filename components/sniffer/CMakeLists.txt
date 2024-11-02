idf_component_register(
        SRCS "sniffer.c" "csi_sniffer.c" "l2_sniffer.c" "sdcard_writer.c"
        INCLUDE_DIRS "include"
        REQUIRES shared nvs_flash esp_timer fatfs esp_wifi
)
