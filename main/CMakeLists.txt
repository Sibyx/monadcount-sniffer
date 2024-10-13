idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS ""
        PRIV_REQUIRES sniffer bluetooth spi_flash nvs_flash management
)
