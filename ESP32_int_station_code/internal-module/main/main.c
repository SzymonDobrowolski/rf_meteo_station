#include <stdio.h>
#include "gpio_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h" // POTRZEBNE DO MUTEXA
#include "esp_system.h"
#include "spi_init.h"
#include "uart_init.h"
#include "nrf.h"
#include "ili9340.h"
#include "fontx.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "lcd.h"
#include "logo.h"
#include "nvs_flash.h"
#include "wifi_project.h" // Upewnij się, że nazwa pliku to wifi_project.h lub my_wifi.h
#include "sntp.h"      // Twój plik od czasu
#include <time.h>

// --- ZMIENNE GLOBALNE ---
TFT_t lcd;
SensorData data;
spi_device_handle_t nrf_handle = NULL;
// spi_device_handle_t lcd_handle = NULL; // To jest w strukturze lcd, tu niepotrzebne
FontxFile fx[2];

// Mutex do ochrony SPI (Kluczowe!)
SemaphoreHandle_t xSpiMutex = NULL;

void init_spiffs() {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiflash",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    if (esp_vfs_spiffs_register(&conf) != ESP_OK) {
        ESP_LOGE("SPIFFS", "Błąd montowania SPIFFS");
    } else {
        ESP_LOGI("SPIFFS", "SPIFFS zamontowany");
    }
}

void init_nvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

// --- TASK ODBIORU DANYCH (NRF) ---
void nrf_receiver_task(void *pvParameters) {
    while(1) {
        // Zabezpieczenie SPI Mutexem
        if (xSpiMutex != NULL && xSemaphoreTake(xSpiMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            nrf_receive_data(nrf_handle, &data);
            xSemaphoreGive(xSpiMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// --- TASK POBIERANIA CZASU (WIFI) ---
void collect_time_task(void *pvParameters) {
    // 1. Połącz z WiFi
    if (wifi_connect_station("Internet domowy Lisiak", "LismaN1410")) { // Użyj poprawnej funkcji z my_wifi.h
        ESP_LOGI("WIFI", "Polaczono!");
        
        // 2. Inicjalizacja SNTP (Raz)
        sntp_init_module(); 
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
        tzset();

        while(1) {
            // 3. Czekaj na synchronizację
            if (wait_for_time_sync()) {
                ESP_LOGI("TIME", "Czas zaktualizowany z NTP");
            }
            // Aktualizacja co godzinę
            vTaskDelay(pdMS_TO_TICKS(3600 * 1000));
        }
    } else {
        ESP_LOGE("WIFI", "Nie udalo sie polaczyc. Zegar nie bedzie dzialal poprawnie.");
        vTaskDelete(NULL); // Zabij taska jeśli brak wifi
    }
}

// --- GŁÓWNY TASK GUI (EKRAN) ---
void lcd_gui_task(void *pvParameters) {
    char buffer[64];
    char time_buf[32]; // ZWIĘKSZONO BUFOR (fix błędu sprintf)
    char date_buf[32]; // ZWIĘKSZONO BUFOR
    
    time_t now;
    struct tm timeinfo;

    // Rysowanie statyczne (tylko raz) - MUSI BYĆ CHRONIONE MUTEXEM
    if (xSemaphoreTake(xSpiMutex, portMAX_DELAY) == pdTRUE) {
        lcdFillScreen(&lcd, WHITE);
        
        lcdDrawRect(&lcd, 0, 0, 200, 100, BLACK); // Dane
        lcdDrawString(&lcd, fx, 5, 25, (uint8_t *)"Dane z czujnika", BLACK);
        
        lcdDrawRect(&lcd, 201, 0, 320, 100, BLACK); // Zegar
        lcdDrawString(&lcd, fx, 215, 25, (uint8_t *)"DATA", BLACK);
        lcdDrawString(&lcd, fx, 215, 65, (uint8_t *)"CZAS", BLACK);
        
        lcdDrawRect(&lcd, 0, 101, 320, 240, BLACK); // Dolny obszar
        
        // Logo (opcjonalnie)
        ili9341_draw_image(&lcd, 110, 120, LOGO_WIDTH, LOGO_HEIGHT, image_data_logo_pp_100px);
        
        xSemaphoreGive(xSpiMutex);
    }

    while(1) {
        // --- PRZYGOTOWANIE DANYCH (SZYBKIE, BEZ SPI) ---
        time(&now);
        localtime_r(&now, &timeinfo);

        // --- RYSOWANIE (CHRONIONE MUTEXEM) ---
        if (xSemaphoreTake(xSpiMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
            
            lcdSetFontDirection(&lcd, DIRECTION0);

            // 1. Temperatura
            lcdDrawFillRect(&lcd, 5, 30, 150, 50, WHITE);
            sprintf(buffer, "Temp: %.2f C", data.temp_hundredths / 100.0f);
            lcdDrawString(&lcd, fx, 5, 45, (uint8_t*)buffer, BLACK);

            // 2. Wilgotność
            lcdDrawFillRect(&lcd, 5, 50, 150, 70, WHITE);
            sprintf(buffer, "Hum: %.2f %%", data.hum_x1024 / 1024.0f);
            lcdDrawString(&lcd, fx, 5, 65, (uint8_t*)buffer, BLACK);

            // 3. Ciśnienie
            lcdDrawFillRect(&lcd, 5, 70, 150, 90, WHITE);
            sprintf(buffer, "Press: %.0f hPa", data.pressure_pa / 100.0f);
            lcdDrawString(&lcd, fx, 5, 85, (uint8_t*)buffer, BLACK);

            // 4. Zegar
            if (timeinfo.tm_year > (2020 - 1900)) {
                // Data
                sprintf(date_buf, "%02d.%02d.%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
                lcdDrawFillRect(&lcd, 210, 30, 315, 50, WHITE);
                lcdDrawString(&lcd, fx, 215, 45, (uint8_t*)date_buf, BLACK);

                // Czas
                sprintf(time_buf, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
                lcdDrawFillRect(&lcd, 210, 70, 315, 90, WHITE);
                lcdDrawString(&lcd, fx, 215, 85, (uint8_t*)time_buf, BLACK);
            } else {
                lcdDrawString(&lcd, fx, 215, 85, (uint8_t*)"Synch...", RED);
            }

            xSemaphoreGive(xSpiMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void) {
    // 1. Inicjalizacja Pamięci
    init_nvs();
    
    // 2. TWORZENIE MUTEXA (BARDZO WAŻNE!)
    xSpiMutex = xSemaphoreCreateMutex();
    if(xSpiMutex == NULL) {
        ESP_LOGE("MAIN", "Nie udalo sie stworzyc Mutexa!");
        return;
    }

    // 3. Inicjalizacja Sprzętu
    gpio_init();
    spi_init();
    uart_init();
    nrf_init(&nrf_handle);
    init_spiffs();
    lcd_init();
    
    // 4. Fonty - drugi argument MUSI być "", nie NULL
    InitFontx(fx, "/spiflash/ILMH16XB.FNT", ""); 
    if (!OpenFontx(fx)) {
        ESP_LOGE("FONT", "Błąd fontu");
    } else {
        ESP_LOGI("FONT", "Font OK");
    }

    // 5. Start Tasków
    xTaskCreate(nrf_receiver_task, "NRF", 4096, NULL, 5, NULL);
    xTaskCreate(collect_time_task, "TIME", 4096, NULL, 5, NULL);
    xTaskCreate(lcd_gui_task, "GUI", 8192, NULL, 5, NULL);
}