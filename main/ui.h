/*
 * @file ui.h
 * Header file for the UI logic
 */

#ifndef UI_H
#define UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

// Ініціалізація всього інтерфейсу
void ui_init(void);

// --- Функції для оновлення даних (API бекенду) ---

/*
 * Оновлення вуличної метеостанції
 * @param temp Температура (напр. 24.5)
 * @param humidity Вологість (напр. 60)
 * @param pressure Тиск (напр. 1013)
 */
void ui_update_outdoor_data(float temp, int humidity, int pressure);

/*
 * Оновлення погоди з сайту
 * @param city Назва міста
 * @param temp Температура
 * @param desc Опис (напр. "Хмарно")
 */
void ui_update_api_weather(const char* city, float temp);

/*
 * Оновлення даних кімнат (викликай для кожної кімнати окремо)
 * @param room_index Індекс кімнати (0, 1 або 2)
 * @param co2 Рівень CO2
 * @param temp Температура в кімнаті
 */
void ui_update_room_data(int room_index, int co2, float temp);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_H*/
