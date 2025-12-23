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

static const char *TAG = "WEATHER_UMAN";

// --- НАЛАШТУВАННЯ ---
#define WIFI_SSID      "Ilyna_2.4g"
#define WIFI_PASS      "17856Lena"
#define WEATHER_API_KEY "850bcfe83ccc1f9199bd6784359c0881"
#define UMAN_LAT       "48.45"
#define UMAN_LON       "30.13"

// Формуємо URL запиту (metric = градуси Цельсія)
#define WEATHER_URL "https://api.openweathermap.org/data/2.5/weather?lat=" UMAN_LAT "&lon=" UMAN_LON "&appid=" WEATHER_API_KEY "&units=metric"

static EventGroupHandle_t s_wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

// --- Обробник подій HTTP ---
// Збирає дані по шматочках в один буфер
esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    static char *output_buffer;
    static int output_len;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            // Якщо це перша порція даних, виділяємо пам'ять
            if (output_len == 0 && evt->data_len > 0) {
                output_buffer = (char*)malloc(2048); // Виділяємо 2КБ із запасом
            }
            // Додаємо нові дані до буфера
            if (output_buffer && output_len + evt->data_len < 2048) {
                memcpy(output_buffer + output_len, evt->data, evt->data_len);
                output_len += evt->data_len;
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            if (output_buffer) {
                output_buffer[output_len] = 0; // Завершуємо рядок нульовим символом
                printf("\n\n%s\n\n", output_buffer);
                free(output_buffer); // Обов'язково звільняємо пам'ять!
                output_buffer = NULL;
                output_len = 0;
            }
            break;
        case HTTP_EVENT_DISCONNECTED:
            if (output_buffer) {
                free(output_buffer);
                output_buffer = NULL;
                output_len = 0;
            }
            break;
        default: break;
    }
    return ESP_OK;
}

// Задача, яка виконується в окремому потоці
void weather_task(void *pvParameters) {
    // Чекаємо підключення до Wi-Fi
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    
    while (1) {
        ESP_LOGI(TAG, "Виконується запит погоди...");
        
        esp_http_client_config_t config = {
            .url = WEATHER_URL,
            .event_handler = _http_event_handler,
            .crt_bundle_attach = esp_crt_bundle_attach, // Підключення сертифікатів для HTTPS
            .method = HTTP_METHOD_GET,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_err_t err = esp_http_client_perform(client);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Помилка HTTP з'єднання: %s", esp_err_to_name(err));
        }
        esp_http_client_cleanup(client);

        ESP_LOGI(TAG, "Наступне оновлення через 1 годину...");
        vTaskDelay(pdMS_TO_TICKS(3600 * 1000)); // Затримка 3600 секунд
    }
}

// Обробник подій Wi-Fi
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Спроба перепідключення...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Підключено! IP отримано.");
    }
}

void app_main(void) {
    // Ініціалізація NVS та мережевого стеку
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    // Реєстрація обробників подій
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    s_wifi_event_group = xEventGroupCreate();

    // Налаштування Wi-Fi
    wifi_config_t wifi_config = {
        .sta = { .ssid = WIFI_SSID, .password = WIFI_PASS },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    // Запуск задачі для погоди
    xTaskCreate(&weather_task, "weather_task", 8192, NULL, 5, NULL);
}