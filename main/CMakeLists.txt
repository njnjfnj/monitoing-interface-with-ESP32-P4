idf_component_register(
    SRCS "main.c" "ui.c"              
    INCLUDE_DIRS "."                  
    PRIV_REQUIRES 
        spi_flash 
        freertos 
        esp_http_client 
        esp_wifi 
        esp_event 
        nvs_flash 
        mbedtls 
        esp_netif                     
        driver                        
        lvgl
        esp_lcd
        esp_lvgl_port
        esp_lcd_touch_gt911           
)