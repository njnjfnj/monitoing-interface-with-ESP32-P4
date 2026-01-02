#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_memory_utils.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"
#include "ui.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_http_client.h"  
#include "esp_crt_bundle.h"   

static const char *TAG = "WEATHER_P4";

#define WIFI_SSID      "111"
#define WIFI_PASS      "password"

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

void process_openweatherapi_json(const char *json_string) {
    cJSON *root = cJSON_Parse(json_string);
    if (root == NULL) {
        ESP_LOGE(TAG, "Помилка парсингу JSON");
        return;
    }

    cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "main");
    cJSON *temp = cJSON_GetObjectItemCaseSensitive(data, "temp");
    if (cJSON_IsNumber(temp)) {
        ui_update_api_weather("Uman", temp->valuedouble);
    }

    cJSON_Delete(root);
}

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
                        ESP_LOGI(TAG, "Обробка даних API...");
                        process_openweatherapi_json(response_buffer);
                        break;
                    
                    case REQ_TYPE_INDOOR:
                        ESP_LOGI(TAG, "Обробка даних КІМНАТ...");
                        // process_indoor_json(response_buffer);
                        break;
                    case REQ_TYPE_OUTDOOR:
                        ESP_LOGI(TAG, "Обробка даних МЕТЕОСТАНЦІЇ...");
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
void weather_api_task(void *pvParameters) {
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

void app_main(void)
{
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .sw_rotate = false, // must be set to true for software rotation
        }
    };
    lv_display_t *disp = bsp_display_start_with_config(&cfg);

    // if (disp != NULL)
    // {
    //     bsp_display_rotate(disp, LV_DISPLAY_ROTATION_90); // 90、180、270
    // }

    bsp_display_backlight_on();

    bsp_display_lock(0);

    // lv_demo_music();
    // lv_demo_benchmark();
    ui_init();

    bsp_display_unlock();

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
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

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
    xTaskCreate(&weather_api_task, "weather_task", 10240, NULL, 5, NULL);
}
