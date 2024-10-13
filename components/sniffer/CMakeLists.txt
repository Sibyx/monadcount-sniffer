idf_component_register(
        SRCS "sniffer.c" "csi_sniffer.c" "l2_sniffer.c"
        INCLUDE_DIRS "include"
        REQUIRES bt shared nvs_flash esp_timer sdmmc fatfs
)
