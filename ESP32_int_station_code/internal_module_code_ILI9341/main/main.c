#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "nvs.h"

// LVGL
#include "lvgl.h"
#include "esp_lvgl_port.h"

// Moduły sprzętowe i projektowe
#include "gpio_init.h"
#include "spi_init.h"
#include "nrf.h"
#include "wifi_project.h"
#include "sntp.h"
#include "lcd.h"
#include "Config.h"
#include "touch.h"

// Nasz nowy moduł interfejsu graficznego!
#include "ui.h" 

static const char *TAG = "MAIN";

// --- MAKRA IKON POGODOWYCH (Dla fetch_weather_task) ---
#define SYM_W_SUN        "\xEF\x86\x85" 
#define SYM_W_CLOUD      "\xEF\x83\x82" 
#define SYM_W_CLOUD_SUN  "\xEF\x9B\x84" 
#define SYM_W_RAIN       "\xEF\x9D\x80" 
#define SYM_W_SNOW       "\xEF\x8B\x9C" 
#define SYM_W_STORM      "\xEF\x83\xA7" 

// --- ZMIENNE GLOBALNE SYSTEMOWE ---
spi_device_handle_t nrf_handle = NULL;
SemaphoreHandle_t xSpiMutex = NULL;
QueueHandle_t xSensorQueue = NULL;

char current_city[64] = "Warszawa"; 
bool force_weather_update = true;   
uint32_t day_brightness = 100; 
uint32_t night_brightness = 25; 
bool is_wifi_connecting = false;


// ==========================================================
// 1. INICJALIZACJA SPRZĘTU (PWM)
// ==========================================================
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
        .duty           = 255,  // Rozruch bezpieczny
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

// ==========================================================
// 2. FUNKCJE PAMIĘCI NVS (Zapis i odczyt ustawień)
// ==========================================================
void save_brightness_settings(uint32_t day, uint32_t night) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_u32(my_handle, "day_br", day);
        nvs_set_u32(my_handle, "night_br", night);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
}

void load_brightness_settings(void) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        nvs_get_u32(my_handle, "day_br", &day_brightness);
        nvs_get_u32(my_handle, "night_br", &night_brightness);
        nvs_close(my_handle);
    }
}

void save_location_settings(const char * city) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_str(my_handle, "city", city);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Zapisano miasto: %s", city);
    }
}

void load_location_settings(void) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        size_t len = sizeof(current_city);
        nvs_get_str(my_handle, "city", current_city, &len);
        nvs_close(my_handle);
    }
}

void save_wifi_credentials(const char * ssid, const char * pass) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_str(my_handle, "ssid", ssid);
        nvs_set_str(my_handle, "pass", pass);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Zapisano dane WiFi do NVS: %s", ssid);
    }
}

bool load_wifi_credentials(char * ssid, char * pass, size_t max_len) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        size_t len = max_len;
        esp_err_t err_ssid = nvs_get_str(my_handle, "ssid", ssid, &len);
        len = max_len;
        esp_err_t err_pass = nvs_get_str(my_handle, "pass", pass, &len);
        nvs_close(my_handle);
        return (err_ssid == ESP_OK && err_pass == ESP_OK);
    }
    return false;
}

// ==========================================================
// 3. FUNKCJE POMOCNICZE (Logika)
// ==========================================================
uint8_t get_battery_level(uint16_t v) {
    if (v >= 3000) return 100;
    if (v <= 2000) return 0;
    if (v > 2800) return 85 + 15 * (v - 2800) / 200; 
    else if (v > 2400) return 30 + 55 * (v - 2400) / 400;
    else if (v > 2200) return 10 + 20 * (v - 2200) / 200;
    else return 10 * (v - 2000) / 200;
}

const char* get_weather_icon(int code) {
    if (code == 0 || code == 1) return SYM_W_SUN; 
    if (code == 2) return SYM_W_CLOUD_SUN; 
    if (code == 3 || code == 45 || code == 48) return SYM_W_CLOUD; 
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return SYM_W_RAIN; 
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return SYM_W_SNOW; 
    if (code >= 95) return SYM_W_STORM; 
    return SYM_W_SUN; 
}

void encode_url_spaces(const char *src, char *dest) {
    while (*src) {
        if (*src == ' ') { *dest++ = '%'; *dest++ = '2'; *dest++ = '0'; }
        else { *dest++ = *src; }
        src++;
    }
    *dest = '\0';
}


// ==========================================================
// 4. TASKI GŁÓWNE (FreeRTOS)
// ==========================================================
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
    ESP_LOGI("TIME", "Zadanie czasu uruchomione");
    char saved_ssid[33] = {0};
    char saved_pass[64] = {0};
    static bool sntp_initialized = false; 

    while(1) {
        if (load_wifi_credentials(saved_ssid, saved_pass, sizeof(saved_pass))) {
            wifi_ap_record_t ap_info;
            bool connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
            
            if (!connected) {
                ESP_LOGW("TIME", "Brak polaczenia. Inicjuje WiFi...");
                is_wifi_connecting = true; 
                connected = wifi_connect_station(saved_ssid, saved_pass);
                is_wifi_connecting = false;
            }
            
            if (connected) { 
                if (!sntp_initialized) {
                    ESP_LOGI("TIME", "Pierwsza synchronizacja czasu SNTP...");
                    sntp_init_module(); 
                    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); 
                    tzset();
                    wait_for_time_sync(); 
                    sntp_initialized = true;
                }
                vTaskDelay(pdMS_TO_TICKS(3600 * 1000)); 
            } else { 
                vTaskDelay(pdMS_TO_TICKS(30000)); 
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10000)); 
        }
    }
}

void fetch_weather_task(void *pvParameters) {
    const char* pl_days[] = {"NIE", "PON", "WTO", "ŚRO", "CZW", "PIĄ", "SOB"};
    int weather_timer = 1800; 

    while(1) {
        time_t now; struct tm timeinfo;
        time(&now); localtime_r(&now, &timeinfo);
        bool is_time_synced = (timeinfo.tm_year > (2020 - 1900));

        wifi_ap_record_t ap_info;
        bool is_wifi_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);

        if (is_wifi_connected && is_time_synced && (force_weather_update || weather_timer >= 1800)) {
            ESP_LOGI(TAG, "Pobieranie najnowszej pogody dla: %s", current_city);
            force_weather_update = false; 
            weather_timer = 0;            
            
            char encoded_city[128];
            encode_url_spaces(current_city, encoded_city);

            char geo_url[256];
            snprintf(geo_url, sizeof(geo_url), "http://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=pl&format=json", encoded_city);

            esp_http_client_config_t config = { .url = geo_url, .method = HTTP_METHOD_GET, .timeout_ms = 5000 };
            esp_http_client_handle_t client = esp_http_client_init(&config);
            esp_err_t err = esp_http_client_open(client, 0);

            float lat = 200.0, lon = 200.0; 

            if (err == ESP_OK) {
                esp_http_client_fetch_headers(client);
                char *buffer = malloc(4096); 
                if (buffer) {
                    int total_read = 0;
                    while (total_read < 4095) {
                        int read_len = esp_http_client_read(client, buffer + total_read, 4095 - total_read);
                        if (read_len <= 0) break;
                        total_read += read_len;
                        vTaskDelay(pdMS_TO_TICKS(1));
                    }
                    buffer[total_read] = 0;
                    
                    cJSON *root = cJSON_Parse(buffer);
                    if (root) {
                        cJSON *results = cJSON_GetObjectItem(root, "results");
                        if (cJSON_IsArray(results) && cJSON_GetArraySize(results) > 0) {
                            cJSON *first_result = cJSON_GetArrayItem(results, 0);
                            lat = cJSON_GetObjectItem(first_result, "latitude")->valuedouble;
                            lon = cJSON_GetObjectItem(first_result, "longitude")->valuedouble;
                        }
                        cJSON_Delete(root);
                    }
                    free(buffer);
                }
            }
            esp_http_client_cleanup(client);

            if (lat != 200.0) {
                char weather_url[256];
                snprintf(weather_url, sizeof(weather_url), "http://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=Europe%%2FWarsaw", lat, lon);
                
                esp_http_client_config_t w_config = { .url = weather_url, .method = HTTP_METHOD_GET, .timeout_ms = 5000 };
                esp_http_client_handle_t w_client = esp_http_client_init(&w_config);
                err = esp_http_client_open(w_client, 0);

                if (err == ESP_OK) {
                    esp_http_client_fetch_headers(w_client);
                    char *w_buffer = malloc(4096); 
                    if (w_buffer) {
                        int total_read = 0;
                        while (total_read < 4095) {
                            int read_len = esp_http_client_read(w_client, w_buffer + total_read, 4095 - total_read);
                            if (read_len <= 0) break;
                            total_read += read_len;
                        }
                        w_buffer[total_read] = 0;

                        cJSON *root = cJSON_Parse(w_buffer);
                        if (root) {
                            cJSON *daily = cJSON_GetObjectItem(root, "daily");
                            if (daily) {
                                cJSON *w_codes = cJSON_GetObjectItem(daily, "weather_code");
                                cJSON *t_max = cJSON_GetObjectItem(daily, "temperature_2m_max");
                                cJSON *t_min = cJSON_GetObjectItem(daily, "temperature_2m_min");

                                if (w_codes && t_max && t_min) {
                                    int today_wday = timeinfo.tm_wday; 
                                    lvgl_port_lock(-1);
                                    for(int i = 0; i < 7; i++) {
                                        cJSON *code_item = cJSON_GetArrayItem(w_codes, i);
                                        cJSON *max_item = cJSON_GetArrayItem(t_max, i);
                                        cJSON *min_item = cJSON_GetArrayItem(t_min, i);

                                        if (code_item && max_item && min_item) {
                                            lv_label_set_text(forecast_icon_labels[i], get_weather_icon(code_item->valueint));
                                            lv_label_set_text_fmt(forecast_temp_labels[i], "%d°C\n%d°C", (int)max_item->valuedouble, (int)min_item->valuedouble);
                                            
                                            if (i == 0) lv_label_set_text(forecast_day_labels[i], "DZIŚ");
                                            else if (i == 1) lv_label_set_text(forecast_day_labels[i], "JUTRO");
                                            else lv_label_set_text(forecast_day_labels[i], pl_days[(today_wday + i) % 7]);
                                        }
                                    }
                                    lvgl_port_unlock();
                                }
                            }
                            cJSON_Delete(root);
                        }
                        free(w_buffer);
                    }
                }
                esp_http_client_cleanup(w_client);
            }
        }
        weather_timer++;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void update_ui_task(void *pvParameters) {
    SensorData receivedData = {0};
    char buf[64];
    time_t now; 
    struct tm timeinfo;
    int start_delay_counter = 5; 
    int chart_update_counter = 590; 
    int last_day = -1; 
    bool first_chart_point = true; 
    
    while(1) {
        xQueueReceive(xSensorQueue, &receivedData, 0);
        uint8_t battery_level = get_battery_level(receivedData.battery_mv);
        
        time(&now);
        localtime_r(&now, &timeinfo);
        
        wifi_ap_record_t ap_info;
        esp_err_t res = esp_wifi_sta_get_ap_info(&ap_info);

        lvgl_port_lock(-1); 

        sprintf(buf, "%.2f °C", receivedData.temp_hundredths / 100.0f);
        lv_label_set_text(lbl_temp, buf); 
        sprintf(buf, "%.2f %%", receivedData.hum_x1024 / 1024.0f);
        lv_label_set_text(lbl_hum, buf);
        sprintf(buf, "%.0f hPa", receivedData.pressure_pa / 100.0f);
        lv_label_set_text(lbl_press, buf);

        if (timeinfo.tm_year > (2020 - 1900)) {
            if (last_day == -1) last_day = timeinfo.tm_mday;
            if (last_day != timeinfo.tm_mday) {
                lv_chart_set_all_value(chart_temp, ser_temp, LV_CHART_POINT_NONE);
                lv_chart_set_all_value(chart_hum, ser_hum, LV_CHART_POINT_NONE);
                lv_chart_set_all_value(chart_press, ser_press, LV_CHART_POINT_NONE);
                last_day = timeinfo.tm_mday;
                first_chart_point = true; 
            }
        }

        chart_update_counter++;
        if (chart_update_counter >= 1800) {  
            if (ser_temp != NULL && receivedData.pressure_pa > 0 && timeinfo.tm_year > (2020 - 1900)) {
                chart_update_counter = 0; 
                int32_t t = receivedData.temp_hundredths / 100;
                int32_t h = receivedData.hum_x1024 / 1024;
                int32_t p = receivedData.pressure_pa / 100;

                if (t > 80) t = 80; else if (t < -40) t = -40;
                if (h > 100) h = 100; else if (h < 0) h = 0;
                if (p > 1200) p = 1200; else if (p < 800) p = 800;

                uint16_t point_index = (timeinfo.tm_hour * 4) + (timeinfo.tm_min / 15);

                if (point_index < 96) {
                    lv_chart_set_value_by_id(chart_temp, ser_temp, point_index, t);
                    lv_chart_set_value_by_id(chart_hum, ser_hum, point_index, h);
                    lv_chart_set_value_by_id(chart_press, ser_press, point_index, p);
                    
                    if (first_chart_point && point_index > 0) {
                        lv_chart_set_value_by_id(chart_temp, ser_temp, point_index - 1, t);
                        lv_chart_set_value_by_id(chart_hum, ser_hum, point_index - 1, h);
                        lv_chart_set_value_by_id(chart_press, ser_press, point_index - 1, p);
                        first_chart_point = false; 
                    }
                }
            } else if (receivedData.pressure_pa == 0) {
                chart_update_counter = 1798; 
            }
        }
        
        // Zmiana ikon baterii -> (Używamy zdefiniowanych wcześniej makr, zadeklarowanych np. w ui.h, ale my wrzuciliśmy to do pliku main żeby łatwiej sterować).
        // (Ponieważ symboli używamy tylko tutaj, a zostały w ui.c, dla bezpieczeństwa wysyłamy poprawne wywołania do ui.c lub odświeżamy z własnych makr).
        // W ui.c mamy: #define MY_SYM_BAT_100  "\xEF\x89\x80"
        // Jeśli kompilator zgłosi brak makra, przenieś to powiązanie do ui.h. Aby uniknąć tego błędu, dodam te makra tutaj:
        #define LOCAL_SYM_BAT_100  "\xEF\x89\x80" 
        #define LOCAL_SYM_BAT_50   "\xEF\x89\x82" 
        #define LOCAL_SYM_BAT_0    "\xEF\x89\x84" 

        if (battery_level > 75) {
            lv_label_set_text(icon_battery, LOCAL_SYM_BAT_100);
            lv_obj_set_style_text_color(icon_battery, lv_palette_main(LV_PALETTE_GREEN), 0);
        } else if (battery_level > 25) {
            lv_label_set_text(icon_battery, LOCAL_SYM_BAT_50);
            lv_obj_set_style_text_color(icon_battery, lv_palette_main(LV_PALETTE_YELLOW), 0);
        } else {
            lv_label_set_text(icon_battery, LOCAL_SYM_BAT_0);
            lv_obj_set_style_text_color(icon_battery, lv_palette_main(LV_PALETTE_RED), 0);
        }

        if (timeinfo.tm_year > (2020 - 1900)) {
            sprintf(buf, "%02d.%02d.%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
            lv_label_set_text(lbl_date, buf);
            sprintf(buf, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            lv_label_set_text(lbl_time, buf);
        }

        if (start_delay_counter > 0) {
            start_delay_counter--;
        } else {
            if (is_wifi_connecting) {
                update_wifi_icon(0, false); 
            } else {
                update_wifi_icon(ap_info.rssi, (res == ESP_OK));
            }
        }

        lvgl_port_unlock(); 
        vTaskDelay(pdMS_TO_TICKS(500)); 
    }
}

void night_mode_task(void *pvParameters) {
    bool last_is_night = false;
    bool first_run = true;

    while(1) {
        time_t now; struct tm timeinfo;
        time(&now); localtime_r(&now, &timeinfo);

        if (timeinfo.tm_year > (2020 - 1900)) { 
            bool is_night = (timeinfo.tm_hour >= 22 || timeinfo.tm_hour < 8);

            if (is_night != last_is_night || first_run) {
                last_is_night = is_night;
                first_run = false;
                ESP_LOGI(TAG, "Przelaczanie na tryb: %s", is_night ? "NOCNY" : "DZIENNY");

                lvgl_port_lock(-1); 
                lv_obj_report_style_change(NULL); 
                lv_disp_t * disp = lv_disp_get_default();
                lv_theme_t * th = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), is_night, LV_FONT_DEFAULT);
                lv_disp_set_theme(disp, th);
                lvgl_port_unlock();

                uint32_t target_duty = is_night ? ((night_brightness * 255) / 100) : ((day_brightness * 255) / 100);
                uint32_t current_duty = ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

                if (current_duty < target_duty) {
                    for (uint32_t d = current_duty; d <= target_duty; d += 5) {
                        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, d);
                        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
                        vTaskDelay(pdMS_TO_TICKS(20)); 
                    }
                } else if (current_duty > target_duty) {
                    for (uint32_t d = current_duty; d >= target_duty; d -= 5) {
                        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, d);
                        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
                        vTaskDelay(pdMS_TO_TICKS(20));
                    }
                }
                ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, target_duty);
                ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000)); 
    }
}

// ==========================================================
// 5. GŁÓWNA FUNKCJA STARTOWA APP_MAIN
// ==========================================================
void app_main(void) {
    ESP_LOGI(TAG, "Start stacji pogodowej z LVGL...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    load_brightness_settings();
    load_location_settings();

    // 1. Inicjalizacja sprzętu
    xSpiMutex = xSemaphoreCreateMutex();
    xSensorQueue = xQueueCreate(1, sizeof(SensorData));
    
    gpio_init();
    spi_init();
    nrf_init(&nrf_handle);
    backlight_pwm_init();
    lcd_init();
    lcd_touch_init(VSPI_HOST);

    // 2. Rysowanie początkowego interfejsu z modułu ui.c
    lvgl_port_lock(-1);
    init_button_styles(); 
    create_weather_ui();
    create_settings_ui();
    create_graph_screen();
    create_weather_info_screen();
    lv_scr_load(main_screen); 
    lvgl_port_unlock();
    
    // 3. Startowanie zadań FreeRTOS
    xTaskCreate(nrf_receiver_task, "NRF_TASK", 4096, NULL, 4, NULL);
    xTaskCreate(collect_time_task, "TIME_TASK", 8192, NULL, 4, NULL); 
    xTaskCreate(update_ui_task, "GUI_UPDATE_TASK", 8192, NULL, 5, NULL);
    xTaskCreate(night_mode_task, "NIGHT_MODE_TASK", 4096, NULL, 5, NULL);
    xTaskCreate(fetch_weather_task, "FETCH_WEATHER", 8192, NULL, 4, NULL);

    ESP_LOGI(TAG, "System dziala!");
    
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}