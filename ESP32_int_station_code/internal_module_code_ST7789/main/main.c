#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "nvs_flash.h"

// Zawsze najpierw LVGL, potem port
#include "lvgl.h"
#include "esp_lvgl_port.h"

// Twoje moduły
#include "gpio_init.h"
#include "spi_init.h"
#include "uart_init.h"
#include "nrf.h"
#include "wifi_project.h"
#include "sntp.h"
#include "lcd.h"
#include "Config.h"

static const char *TAG = "MAIN";

// Obiekty systemowe
spi_device_handle_t nrf_handle = NULL;
SemaphoreHandle_t xSpiMutex = NULL;
QueueHandle_t xSensorQueue = NULL;

// Globalne wskaźniki na obiekty LVGL (aby móc je aktualizować z innych tasków)
lv_obj_t * lbl_title;
lv_obj_t * lbl_temp;
lv_obj_t * lbl_hum;
lv_obj_t * lbl_press;
lv_obj_t * lbl_time;
lv_obj_t * lbl_date;

// --- INICJALIZACJA PODŚWIETLENIA (PWM) ---
void backlight_pwm_init(void) {
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_8_BIT, 
        .freq_hz          = 5000,             
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = BL_PIN,
        // POPRAWKA 1: Zmniejszono jasność startową do 100, żeby uniknąć restartów od spadku napięcia
        .duty           = 255,  
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

void create_status_bar(lv_obj_t * scr) {
    lvgl_port_lock(-1);
    
    // 1. Główny niewidzialny pasek na samej górze
    lv_obj_t * status_bar = lv_obj_create(scr);
    lv_obj_set_size(status_bar, 320, 50); 
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(status_bar, 0, 0);
    lv_obj_set_style_border_opa(status_bar, 0, 0);
    lv_obj_set_style_pad_all(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    //2. Tytuł
    lbl_title = lv_label_create(status_bar);
    lv_label_set_text(lbl_title, "STACJA POGODOWA");
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, -60, 10);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_16, 0);

    // 3. Pigułka z układem pionowym (Flex Column)
    lv_obj_t * clock_cont = lv_obj_create(status_bar);
    lv_obj_set_size(clock_cont, 110, 40); // Trochę wyższa pigułka
    lv_obj_align(clock_cont, LV_ALIGN_TOP_RIGHT, -5, 5);
    lv_obj_set_style_radius(clock_cont, 10, 0);
    lv_obj_set_style_bg_opa(clock_cont, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(clock_cont, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_pad_all(clock_cont, 2, 0); // Mały odstęp od krawędzi pigułki
    
    // Ustawienie układu pionowego
    lv_obj_set_flex_flow(clock_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(clock_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(clock_cont, LV_OBJ_FLAG_SCROLLABLE);

    // 4. Etykieta DATY (na górze)
    lbl_date = lv_label_create(clock_cont);
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_12, 0); // Mniejsza czcionka dla daty
    lv_label_set_text(lbl_date, "01.01.2026");

    // 5. Etykieta CZASU (pod spodem)
    lbl_time = lv_label_create(clock_cont);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_16, 0); // Większa czcionka dla czasu
    lv_label_set_text(lbl_time, "00:00:00");

    lv_obj_move_foreground(status_bar);
    lvgl_port_unlock();
}

// --- TWORZENIE GŁÓWNEGO INTERFEJSU LVGL ---
void create_weather_ui(void) {
    // POPRAWKA 2: Zmieniono z 0 na -1 (nieskończone czekanie na odblokowanie rysowania)
    lvgl_port_lock(-1); 

    lv_obj_t * scr = lv_scr_act();

    // Etykiety na dane (wyrównane do lewej)
    lbl_temp = lv_label_create(scr);
    lv_label_set_text(lbl_temp, "Temperatura: --.-- °C");
    lv_obj_align(lbl_temp, LV_ALIGN_TOP_LEFT, 20, 50);

    lbl_hum = lv_label_create(scr);
    lv_label_set_text(lbl_hum, "Wilgotnosc: --.-- %");
    lv_obj_align(lbl_hum, LV_ALIGN_TOP_LEFT, 20, 90);

    lbl_press = lv_label_create(scr);
    lv_label_set_text(lbl_press, "Cisnienie: ---- hPa");
    lv_obj_align(lbl_press, LV_ALIGN_TOP_LEFT, 20, 130);

    /* //Pasek postępu dla wilgotności
    lv_obj_t * bar_hum = lv_bar_create(scr);
    lv_obj_set_size(bar_hum, 150, 20);
    lv_bar_set_value(bar_hum, 65, LV_ANIM_ON); // 65% wilgotności
    lv_obj_align(bar_hum, LV_ALIGN_TOP_RIGHT, -20, 50);
    */
    create_status_bar(scr);

    lvgl_port_unlock();
}

// --- TASKI ODBIERAJĄCE DANE ---
void nrf_receiver_task(void *pvParameters) {
    SensorData localData = {0};
    while(1) {
        if (xSpiMutex && xSemaphoreTake(xSpiMutex, pdMS_TO_TICKS(100))) {
            if (nrf_receive_data(nrf_handle, &localData)) {
                xQueueOverwrite(xSensorQueue, &localData);
            }
            xSemaphoreGive(xSpiMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void collect_time_task(void *pvParameters) {
    if (wifi_connect_station(SSID, PASSWORD)) { 
        sntp_init_module(); 
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); 
        tzset();
        while(1) { 
            wait_for_time_sync(); 
            vTaskDelay(pdMS_TO_TICKS(3600 * 1000)); // Czekaj godzinę
        }
    } else { 
        vTaskDelete(NULL); 
    }
}


// --- TASK AKTUALIZUJĄCY EKRAN (GUI) ---
void update_ui_task(void *pvParameters) {
    SensorData receivedData = {0};
    char buf[64];
    time_t now; 
    struct tm timeinfo;

    while(1) {
        lvgl_port_lock(-1); // POPRAWKA 2

        // Aktualizacja Danych z NRF
        if (xQueueReceive(xSensorQueue, &receivedData, 0) == pdTRUE) {
            sprintf(buf, "Temperatura: %.2f °C", receivedData.temp_hundredths / 100.0f);
            lv_label_set_text(lbl_temp, buf); 
            
            sprintf(buf, "Wilgotnosc: %.2f %%", receivedData.hum_x1024 / 1024.0f);
            lv_label_set_text(lbl_hum, buf);
            
            sprintf(buf, "Cisnienie: %.0f hPa", receivedData.pressure_pa / 100.0f);
            lv_label_set_text(lbl_press, buf);
        }

        // Aktualizacja Czasu
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year > (2020 - 1900)) {
            // Sam czas do prawego rogu
            sprintf(buf, "%02d.%02d.%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
            lv_label_set_text(lbl_date, buf);
            sprintf(buf, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            lv_label_set_text(lbl_time, buf);
        }

        lvgl_port_unlock();
        vTaskDelay(pdMS_TO_TICKS(500)); // Odświeżaj 2 razy na sekundę
    }
}

// --- TASK TRYBU NOCNEGO ---
void night_mode_task(void *pvParameters) {
    bool last_is_night = false;
    bool first_run = true;

    while(1) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);

        if (timeinfo.tm_year > (2020 - 1900)) { // Sprawdź, czy mamy prawdziwy czas
            
            // Tryb nocny: od 22:00 do 7:59
            bool is_night = (timeinfo.tm_hour >= 22 || timeinfo.tm_hour < 8);
            bool lv_dark_mode = !is_night; // Możesz tu dodać dodatkowe warunki, np. na podstawie czujnika światła

            if (is_night != last_is_night || first_run) {
                last_is_night = is_night;
                first_run = false;

                ESP_LOGI(TAG, "Przelaczanie na tryb: %s", is_night ? "NOCNY" : "DZIENNY");

                // 1. Zmiana motywu LVGL
                lvgl_port_lock(-1); // POPRAWKA 2
                lv_disp_t * disp = lv_disp_get_default();
                lv_theme_t * th = lv_theme_default_init(disp,
                    lv_palette_main(LV_PALETTE_BLUE), // Kolor akcentów 1
                    lv_palette_main(LV_PALETTE_RED),  // Kolor akcentów 2
                    lv_dark_mode,                         // TRUE = tryb ciemny
                    LV_FONT_DEFAULT);
                lv_disp_set_theme(disp, th);
                lvgl_port_unlock();

                // 2. Ściemnianie/Rozjaśnianie podświetlenia
                uint32_t duty = is_night ? 20 : 255; 
                ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
                ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000)); // Sprawdzaj czas co 5 sekund
    }
}

// --- MAIN ---
void app_main(void) {
    ESP_LOGI(TAG, "Start stacji pogodowej z LVGL...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 1. Inicjalizacja sprzętu
    xSpiMutex = xSemaphoreCreateMutex();
    xSensorQueue = xQueueCreate(1, sizeof(SensorData));
    
    gpio_init();
    spi_init();
    nrf_init(&nrf_handle);
    backlight_pwm_init();
    lcd_init();

    // 2. Rysowanie początkowego interfejsu
    create_weather_ui();
    
    // 3. Startowanie zadań FreeRTOS
    xTaskCreate(nrf_receiver_task, "NRF_TASK", 4096, NULL, 4, NULL);
    xTaskCreate(collect_time_task, "TIME_TASK", 8192, NULL, 4, NULL); 
    xTaskCreate(update_ui_task, "GUI_UPDATE_TASK", 4096, NULL, 5, NULL);
    xTaskCreate(night_mode_task, "NIGHT_MODE_TASK", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "System dziala!");
    
    // POPRAWKA 3: Zatrzymujemy główny task w nieskończonej pętli
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}