idf_component_register(
        SRCS "management.c"
        INCLUDE_DIRS "include"
        REQUIRES shared lwip esp_wifi wpa_supplicant sdmmc fatfs
)
