idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES spi_flash freertos esp_http_client esp_wifi esp_event nvs_flash mbedtls
                       INCLUDE_DIRS ".")
