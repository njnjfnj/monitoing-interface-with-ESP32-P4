/**
* @file ui.c
*/

#include "./ui.h"
#include "esp_lvgl_port.h"

// --- Змінні для зберігання посилань на об'єкти (щоб ми могли їх змінювати) ---
static lv_obj_t*lbl_outdoor_temp;
static lv_obj_t*lbl_outdoor_hum;
static lv_obj_t*lbl_outdoor_press;

static lv_obj_t*lbl_api_city;
static lv_obj_t*lbl_api_temp;
static lv_obj_t*lbl_api_desc;

// Масиви для 3-х кімнат
static lv_obj_t*lbl_rooms_co2[3];
static lv_obj_t*lbl_rooms_temp[3];

// --- Допоміжні функції стилізації ---

static void style_screen_part(lv_obj_t* parent) {
    // Центруємо контент та додаємо відступи, щоб на круглому екрані не обрізало кути
    lv_obj_set_size(parent, lv_pct(100), lv_pct(100));
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 40, 0); // Відступи від країв кола
    lv_obj_set_style_bg_opa(parent, LV_OPA_TRANSP, 0); // Прозорий фон для плитки
}

static lv_obj_t* create_header_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl, lv_palette_main(LV_PALETTE_YELLOW), 0);
    return lbl;
}

static lv_obj_t* create_value_label(lv_obj_t* parent, const char* default_text, int font_size_mode) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, default_text);
    // Вибір шрифту
    if(font_size_mode == 1) lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0); // Великий
    else lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0); // Середній

    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    return lbl;
}

// --- Створення сторінок ---

// 1. Вулична станція
static void build_outdoor_page(lv_obj_t* page) {
    style_screen_part(page);

    create_header_label(page, "OUTDOOR STATION");

    lbl_outdoor_temp = create_value_label(page, "--.- °C", 1);

    lv_obj_t* cont_details = lv_obj_create(page);
    lv_obj_set_size(cont_details, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(cont_details, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont_details, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_details, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cont_details, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont_details, 0, 0);

    lbl_outdoor_hum = create_value_label(cont_details, "Hum: --%", 0);
    lv_obj_set_style_pad_left(lbl_outdoor_hum, 10, 0);

    lbl_outdoor_press = create_value_label(page, "Press: ---- hPa", 0);
}

// 2. Погода з API
static void build_api_page(lv_obj_t* page) {
    style_screen_part(page);

    create_header_label(page, "WEATHER API");

    lbl_api_city = create_value_label(page, "Cherkassy", 0);
    lv_obj_set_style_text_color(lbl_api_city, lv_palette_main(LV_PALETTE_BLUE), 0);

    lbl_api_temp = create_value_label(page, "-- °C", 1);
    lbl_api_desc = create_value_label(page, "Loading...", 0);
}

// 3. Кімнати (Моніторинг якості повітря)
static void build_indoor_page(lv_obj_t* page) {
    style_screen_part(page);
    create_header_label(page, "INDOOR AIR QUALITY");

    // Створимо список для 3х кімнат
    for(int i=0; i<3; i++) {
        lv_obj_t* cont = lv_obj_create(page);
        lv_obj_set_size(cont, lv_pct(45), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(cont, lv_palette_darken(LV_PALETTE_GREY, 3), 0);
        lv_obj_set_style_border_width(cont, 3, 0);
        lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(cont, 15, 0);

        // Назва кімнати
        char room_name[10];
        lv_obj_t* l_name = lv_label_create(cont);
        lv_label_set_text(l_name, room_name);
        lv_obj_set_style_text_color(l_name, lv_palette_main(LV_PALETTE_ORANGE), 0);

        // Показники
        lbl_rooms_co2[i] = lv_label_create(cont);
        lv_label_set_text(lbl_rooms_co2[i], "CO2: --");
        lv_obj_set_style_text_color(lbl_rooms_co2[i], lv_color_white(), 0);

        lbl_rooms_temp[i] = lv_label_create(cont);
        lv_label_set_text(lbl_rooms_temp[i], "--°");
        lv_obj_set_style_text_color(lbl_rooms_temp[i], lv_color_white(), 0);
    }
}

// --- ГОЛОВНА ФУНКЦІЯ ІНІЦІАЛІЗАЦІЇ ---
void ui_init(void) {
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0); // Темна тема для OLED/LCD

    // Створюємо Tileview (гортання сторінок)
    lv_obj_t* tv = lv_tileview_create(scr);

    lv_obj_set_style_bg_color(tv, lv_color_black(), 0); // Робимо Tileview чорним
    lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);       // Робимо його непрозорим

    // Прибираємо скроллбари, щоб виглядало чисто
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);

    // Створення плиток (Tile)
    lv_obj_t* t1 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT); // Можна свайпати вправо
    lv_obj_t* t2 = lv_tileview_add_tile(tv, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t* t3 = lv_tileview_add_tile(tv, 2, 0, LV_DIR_LEFT);

    // Наповнення контентом
    build_outdoor_page(t1);
    build_api_page(t2);
    build_indoor_page(t3);
}



// --- ФУНКЦІЇ ОНОВЛЕННЯ ---

void ui_update_outdoor_data(float temp, int humidity, int pressure) {
    lvgl_port_lock(0);
    if(lbl_outdoor_temp) lv_label_set_text_fmt(lbl_outdoor_temp, "%.1f °C", temp);
    if(lbl_outdoor_hum) lv_label_set_text_fmt(lbl_outdoor_hum, "Hum: %d%%", humidity);
    if(lbl_outdoor_press) lv_label_set_text_fmt(lbl_outdoor_press, "Prs: %d hPa", pressure);
    lvgl_port_unlock();
}

void ui_update_api_weather(const char* city, float temp, const char* desc) {
    lvgl_port_lock(0);
    if(lbl_api_city) lv_label_set_text(lbl_api_city, city);
    if(lbl_api_temp) lv_label_set_text_fmt(lbl_api_temp, "%.1f °C", temp);
    if(lbl_api_desc) lv_label_set_text(lbl_api_desc, desc);
    lvgl_port_unlock();
}

void ui_update_room_data(int room_index, int co2, float temp) {
    lvgl_port_lock(0);
    if(room_index < 0 || room_index >= 3) return;

    if(lbl_rooms_co2[room_index]) {
        lv_label_set_text_fmt(lbl_rooms_co2[room_index], "CO2: %d", co2);
        // Зміна кольору якщо CO2 високий
        if(co2 > 1000) lv_obj_set_style_text_color(lbl_rooms_co2[room_index], lv_palette_main(LV_PALETTE_RED), 0);
        else lv_obj_set_style_text_color(lbl_rooms_co2[room_index], lv_palette_main(LV_PALETTE_GREEN), 0);
    }

    if(lbl_rooms_temp[room_index]) {
        lv_label_set_text_fmt(lbl_rooms_temp[room_index], "%.1f°", temp);
    }
    lvgl_port_unlock();
}
