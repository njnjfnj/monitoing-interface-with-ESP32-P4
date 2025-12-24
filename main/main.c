#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_touch_gt911.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lvgl_port.h"
#include "./ui.h"

// --- КОНФІГУРАЦІЯ ЕКРАНУ (720x720 Waveshare 4 inch) ---
#define LCD_H_RES              720
#define LCD_V_RES              720
#define LCD_MIPI_DSI_LANE_NUM  2
#define LCD_MIPI_DSI_LANE_RATE 1000 
#define TOUCH_I2C_SDA          6
#define TOUCH_I2C_SCL          7
#define TOUCH_RST              -1
#define TOUCH_INT              8

// --- КОНФІГУРАЦІЯ ТАЧУ (I2C) ---
#define TOUCH_I2C_SDA          6
#define TOUCH_I2C_SCL          7
#define TOUCH_RST              -1   // Часто керується через розширювач портів або RC-ланцюг
#define TOUCH_INT              8    // Пін переривання

static const char *TAG = "WEATHER_P4";

#define WIFI_SSID      "Ilyna_2.4g"
#define WIFI_PASS      "17856Lena"

#define WEATHER_API_KEY "850bcfe83ccc1f9199bd6784359c0881"
#define UMAN_LAT       "48.45"
#define UMAN_LON       "30.13"

#define WEATHER_URL "https://api.openweathermap.org/data/2.5/weather?lat=" UMAN_LAT "&lon=" UMAN_LON "&appid=" WEATHER_API_KEY "&units=metric"

static EventGroupHandle_t s_wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

#define HTTP_RESPONSE_BUFFER_SIZE 2048

char *response_buffer = NULL;
int response_len = 0;

typedef enum {
    REQ_TYPE_WEATHER,  // Вулична погода (OpenWeather)
    REQ_TYPE_INDOOR,   // Дані з кімнатних датчиків
    REQ_TYPE_OUTDOOR      // Точний час тощо
} request_type_t;

// --- Обробник подій HTTP клієнта ---
esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    request_type_t req_type = (request_type_t)evt->user_data;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                if (response_buffer == NULL) {
                    response_buffer = (char *)malloc(HTTP_RESPONSE_BUFFER_SIZE);
                    response_len = 0;
                }
                if (response_buffer && response_len + evt->data_len < HTTP_RESPONSE_BUFFER_SIZE) {
                    memcpy(response_buffer + response_len, evt->data, evt->data_len);
                    response_len += evt->data_len;
                }
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            if (response_buffer != NULL) {
                response_buffer[response_len] = 0; // Завершуємо рядок
                
                switch (req_type) {
                    case REQ_TYPE_WEATHER:
                        ESP_LOGI(TAG, "Обробка даних ПОГОДИ...");
                        // process_weather_json(response_buffer); <--- Ваша функція парсингу
                        break;
                    
                    case REQ_TYPE_INDOOR:
                        ESP_LOGI(TAG, "Обробка даних КІМНАТ...");
                        // process_indoor_json(response_buffer);
                        break;
                        
                    default:
                        ESP_LOGW(TAG, "Невідомий тип запиту");
                        break;
                }

                free(response_buffer);
                response_buffer = NULL;
                response_len = 0;
            }
            break;
            
        case HTTP_EVENT_DISCONNECTED:
             if (response_buffer != NULL) {
                free(response_buffer);
                response_buffer = NULL;
                response_len = 0;
            }
            break;
        default: break;
    }
    return ESP_OK;
}

// --- Основна задача погоди (Task) ---
void weather_task(void *pvParameters) {
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    
    while (1) {
        ESP_LOGI(TAG, "Відправка запиту до OpenWeatherMap...");
        
        // Налаштування HTTP клієнта
        esp_http_client_config_t config = {
            .url = WEATHER_URL,
            .event_handler = _http_event_handler,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .method = HTTP_METHOD_GET,
            .timeout_ms = 10000,
            .user_data = (void*)REQ_TYPE_WEATHER
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_err_t err = esp_http_client_perform(client); // Виконання запиту

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Успіх! Статус код: %d, Довжина відповіді: %lld",
                    esp_http_client_get_status_code(client),
                    esp_http_client_get_content_length(client));
        } else {
            ESP_LOGE(TAG, "Помилка HTTP запиту: %s", esp_err_to_name(err));
        }
        
        // Очищення ресурсів клієнта
        esp_http_client_cleanup(client);

        ESP_LOGI(TAG, "Чекаємо 1 годину до наступного оновлення...");
        vTaskDelay(pdMS_TO_TICKS(3600 * 1000));
    }
}

// --- Обробник системних подій Wi-Fi ---
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi відключено або не вдалося підключитися. Повторна спроба...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "IP отримано: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

lv_disp_t *lvgl_disp = NULL;
lv_indev_t *lvgl_touch_indev = NULL;
esp_lcd_touch_handle_t tp_handle = NULL;

void init_lcd_and_touch(void) {
    ESP_LOGI(TAG, "Ініціалізація MIPI DSI шини...");

    // 1. Створення шини DSI
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = LCD_MIPI_DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = LCD_MIPI_DSI_LANE_RATE,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

    // 2. Налаштування панелі (ВИПРАВЛЕНО: прибрано зайві структури SPI)
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 60,
        .video_timing = {
            .h_size = LCD_H_RES,
            .v_size = LCD_V_RES,
            .hsync_back_porch = 50, // Перевірте ці значення у прикладі Waveshare!
            .hsync_pulse_width = 20,
            .hsync_front_porch = 50,
            .vsync_back_porch = 20,
            .vsync_pulse_width = 10,
            .vsync_front_porch = 20,
        },
    };

    esp_lcd_panel_handle_t panel_handle = NULL;
    
    // ВИПРАВЛЕНО: Функція приймає лише 3 аргументи
    ESP_ERROR_CHECK(esp_lcd_new_panel_dpi(mipi_dsi_bus, &dpi_config, &panel_handle));
    
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // 3. Ініціалізація LVGL
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);

    // Додавання дисплея
    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel_handle,
        .buffer_size = LCD_H_RES * 50, // Зменшено буфер для стабільності
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        // .mipi_dsi = true  <-- ВИДАЛЕНО: Цей параметр не існує в стандартному lvgl_port, 
        // компонент сам зрозуміє тип панелі через panel_handle
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
        }
    };
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);

    // --- 4. Тач скрін (GT911) ---
    ESP_LOGI(TAG, "Ініціалізація Touch Screen...");
    
    i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = -1,
        .scl_io_num = TOUCH_I2C_SCL,
        .sda_io_num = TOUCH_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &bus_handle));

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus_handle, &tp_io_config, &tp_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = TOUCH_RST,
        .int_gpio_num = TOUCH_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    
    esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp_handle);

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp,
        .handle = tp_handle,
    };
    lvgl_touch_indev = lvgl_port_add_touch(&touch_cfg);
    
    ESP_LOGI(TAG, "UI ініціалізовано успішно!");
}

void app_main(void) {
    init_lcd_and_touch();

    // блокуємо доступ перед малюванням
    lvgl_port_lock(0);
    ui_init();
    lvgl_port_unlock();


    // Ініціалізація NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Ініціалізація мережевого інтерфейсу
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Ініціалізація драйвера Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Реєстрація обробників подій (Wi-Fi та IP)
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    s_wifi_event_group = xEventGroupCreate();

    // Конфігурація Wi-Fi (SSID та пароль)
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi ініціалізацію завершено. Запускаємо задачу погоди...");
    
    // Запускаємо задачу у FreeRTOS. Stack size = 10240 байт
    xTaskCreate(&weather_task, "weather_task", 10240, NULL, 5, NULL);
}