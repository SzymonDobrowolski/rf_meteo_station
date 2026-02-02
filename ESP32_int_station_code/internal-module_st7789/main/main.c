#include <stdio.h>
#include "gpio_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h" // POTRZEBNE DO MUTEXA
#include "freertos/queue.h"
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

#include "Config.h"

#define GRAPH_X_START 40
#define GRAPH_Y_START 130 // Startujemy na 130 (zostaje 30px na legendę u góry)
#define GRAPH_WIDTH   240
#define GRAPH_HEIGHT  70  // Wysokość 70. Koniec wykresu wypadnie na Y=200.
#define GRAPH_POINTS  100

// Zakresy do skalowania (Wartości Min i Max dla osi Y każdego parametru)
// Można dostosować: Temp 15-35C, Hum 0-100%, Press 980-1040hPa
#define MIN_TEMP 15.0f
#define MAX_TEMP 35.0f
#define MIN_HUM  20.0f
#define MAX_HUM  80.0f
#define MIN_PRESS 980.0f
#define MAX_PRESS 1040.0f

// Bufory historii (cykliczne)
float hist_temp[GRAPH_POINTS] = {0};
float hist_hum[GRAPH_POINTS]  = {0};
float hist_press[GRAPH_POINTS]= {0};
int   hist_idx = 0;           // Gdzie wpisać następny punkt
bool  hist_full = false;      // Czy zapełniliśmy bufor chociaż raz

// --- ZMIENNE GLOBALNE ---
TFT_t lcd;

spi_device_handle_t nrf_handle = NULL;
// spi_device_handle_t lcd_handle = NULL; // To jest w strukturze lcd, tu niepotrzebne
FontxFile fx[2];

// Mutex do ochrony SPI (Kluczowe!)
SemaphoreHandle_t xSpiMutex = NULL;
QueueHandle_t xSensorQueue = NULL;

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
    SensorData localData = {0};

    while(1) {
        // Zabezpieczenie SPI Mutexem
        if (xSpiMutex != NULL && xSemaphoreTake(xSpiMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            
            // Sprawdzamy, czy są dane (zakładam, że nrf_receive_data zwraca true/false lub status)
            // Jeśli twoja funkcja nrf_receive_data zwraca void, musisz zmienić jej logikę,
            // żeby informowała, czy COKOLWIEK odebrała.
            // Zakładam tutaj wariant optymistyczny - pobieramy zawsze:
            
            nrf_receive_data(nrf_handle, &localData);
            
            xSemaphoreGive(xSpiMutex);

            // Wyślij do kolejki (Overwrite nadpisuje stary element, jeśli LCD go nie odebrał)
            // To sygnalizuje taskowi LCD: "MAMY NOWE DANE!"
            xQueueOverwrite(xSensorQueue, &localData);
        }
        
        // Sprawdzaj co 100ms lub rzadziej (zależnie od potrzeb)
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// --- TASK POBIERANIA CZASU (WIFI) ---
void collect_time_task(void *pvParameters) {
    // 1. Połącz z WiFi
    if (wifi_connect_station(SSID, PASSWORD)) { // Użyj poprawnej funkcji z my_wifi.h
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

// Funkcja pomocnicza: Mapuje wartość na piksel Y wykresu
uint16_t map_val(float value, float min_v, float max_v) {
    if (value < min_v) value = min_v;
    if (value > max_v) value = max_v;
    
    // Proporcja (0.0 do 1.0)
    float ratio = (value - min_v) / (max_v - min_v);
    
    // Odwracamy Y (bo na LCD 0 jest na górze, a my chcemy wysokie wartości na górze wykresu)
    return GRAPH_Y_START + GRAPH_HEIGHT - (uint16_t)(ratio * GRAPH_HEIGHT);
}

// Funkcja dodająca nowy pomiar do historii
void add_to_history(SensorData *d) {
    hist_temp[hist_idx] = d->temp_hundredths / 100.0f;
    hist_hum[hist_idx]  = d->hum_x1024 / 1024.0f;
    hist_press[hist_idx]= d->pressure_pa / 100.0f;

    hist_idx++;
    if (hist_idx >= GRAPH_POINTS) {
        hist_idx = 0;
        hist_full = true;
    }
}

// Funkcja rysująca wykres
void draw_chart_lines() {
    // 1. Wyczyść tło wykresu (tylko obszar wykresu)
    lcdDrawFillRect(&lcd, GRAPH_X_START, GRAPH_Y_START, GRAPH_X_START + GRAPH_WIDTH, GRAPH_Y_START + GRAPH_HEIGHT, WHITE);
    
    // 2. Rysuj ramkę
    lcdDrawRect(&lcd, GRAPH_X_START, GRAPH_Y_START, GRAPH_X_START + GRAPH_WIDTH, GRAPH_Y_START + GRAPH_HEIGHT, BLACK);

    // 3. Rysuj linie przerywane (siatkę) - opcjonalnie, np. w połowie
    uint16_t y_mid = GRAPH_Y_START + (GRAPH_HEIGHT / 2);
    lcdDrawLine(&lcd, GRAPH_X_START, y_mid, GRAPH_X_START + GRAPH_WIDTH, y_mid, GRAY);

    // 4. Pętla rysująca linie
    // Musimy przejść od najstarszego punktu do najnowszego
    int count = hist_full ? GRAPH_POINTS : hist_idx;
    int start_i = hist_full ? hist_idx : 0; // W buforze cyklicznym najstarszy element jest "za" indeksem zapisu
    
    // Krok X w pikselach
    float step_x = (float)GRAPH_WIDTH / (float)(GRAPH_POINTS - 1);

    for (int i = 0; i < count - 1; i++) {
        // Oblicz indeksy w tablicy (modulo obsługuje zawijanie bufora)
        int idx_now = (start_i + i) % GRAPH_POINTS;
        int idx_next = (start_i + i + 1) % GRAPH_POINTS;

        // Współrzędne X
        uint16_t x1 = GRAPH_X_START + (uint16_t)(i * step_x);
        uint16_t x2 = GRAPH_X_START + (uint16_t)((i + 1) * step_x);

        // --- TEMPERATURA (CZERWONY) ---
        uint16_t y1_t = map_val(hist_temp[idx_now], MIN_TEMP, MAX_TEMP);
        uint16_t y2_t = map_val(hist_temp[idx_next], MIN_TEMP, MAX_TEMP);
        lcdDrawLine(&lcd, x1, y1_t, x2, y2_t, RED);

        // --- WILGOTNOŚĆ (NIEBIESKI) ---
        uint16_t y1_h = map_val(hist_hum[idx_now], MIN_HUM, MAX_HUM);
        uint16_t y2_h = map_val(hist_hum[idx_next], MIN_HUM, MAX_HUM);
        lcdDrawLine(&lcd, x1, y1_h, x2, y2_h, BLUE);

        // --- CIŚNIENIE (ZIELONY) ---
        uint16_t y1_p = map_val(hist_press[idx_now], MIN_PRESS, MAX_PRESS);
        uint16_t y2_p = map_val(hist_press[idx_next], MIN_PRESS, MAX_PRESS);
        lcdDrawLine(&lcd, x1, y1_p, x2, y2_p, GREEN);
    }
}

// --- GŁÓWNY TASK GUI (EKRAN) ---
void lcd_gui_task(void *pvParameters) {
    char buffer[64];
    char axis_buf[16]; 
    char time_buf[32];
    char date_buf[32];
    
    SensorData currentData = {0}; 
    SensorData receivedData = {0};      
    
    bool firstRun = true;
    bool hasData = false; 
    time_t now;
    struct tm timeinfo;
    int last_sec = -1;

    // --- BLOK STARTOWY ---
    if (xSemaphoreTake(xSpiMutex, portMAX_DELAY) == pdTRUE) {
        lcdFillScreen(&lcd, WHITE);
        
        // Górne ramki
        lcdDrawRect(&lcd, 0, 0, 200, 100, BLACK);
        lcdDrawString(&lcd, fx, 5, 25, (uint8_t *)"Dane z czujnika", BLACK);
        
        lcdDrawRect(&lcd, 201, 0, 320, 100, BLACK);
        lcdDrawString(&lcd, fx, 215, 25, (uint8_t *)"DATA", BLACK);
        lcdDrawString(&lcd, fx, 215, 65, (uint8_t *)"CZAS", BLACK);
        
        // --- LEGENDA (Nad wykresem) ---
        int leg_y = GRAPH_Y_START - 20; 
        int box_s = 10;

        // 1. Temp (Czerwony)
        lcdDrawFillRect(&lcd, 40, leg_y, 40 + box_s, leg_y + box_s, RED);
        lcdDrawString(&lcd, fx, 55, leg_y + 10, (uint8_t *)"Temp", BLACK);

        // 2. Wilg (Niebieski)
        lcdDrawFillRect(&lcd, 130, leg_y, 130 + box_s, leg_y + box_s, BLUE);
        lcdDrawString(&lcd, fx, 145, leg_y + 10, (uint8_t *)"Wilg", BLACK);
        
        // 3. Cisn (Zielony)
        lcdDrawFillRect(&lcd, 220, leg_y, 220 + box_s, leg_y + box_s, GREEN);
        lcdDrawString(&lcd, fx, 235, leg_y + 10, (uint8_t *)"Cisn", BLACK);

        // --- OPIS OSI Y (Min/Max) ---
        
        // Lewa Oś (Temperatura)
        sprintf(axis_buf, "%.0fC", MAX_TEMP);
        lcdDrawString(&lcd, fx, 2, GRAPH_Y_START, (uint8_t *)axis_buf, RED); 
        
        sprintf(axis_buf, "%.0fC", MIN_TEMP);
        // Rysujemy na wysokości końca wykresu minus trochę (żeby było widać)
        lcdDrawString(&lcd, fx, 2, GRAPH_Y_START + GRAPH_HEIGHT - 10, (uint8_t *)axis_buf, RED); 

        // Prawa Oś (Wilgotność)
        sprintf(axis_buf, "%.0f%%", MAX_HUM);
        lcdDrawString(&lcd, fx, GRAPH_X_START + GRAPH_WIDTH + 2, GRAPH_Y_START, (uint8_t *)axis_buf, BLUE); 
        
        // Prawa Oś (Wilgotność - Dół)
        sprintf(axis_buf, "%.0f%%", MIN_HUM);
        lcdDrawString(&lcd, fx, GRAPH_X_START + GRAPH_WIDTH + 2, GRAPH_Y_START + GRAPH_HEIGHT - 10, (uint8_t *)axis_buf, BLUE); 

        // Rysujemy na sztywno przy dolnej krawędzi (Y = 225)
        // Ekran kończy się na 240, więc napis będzie idealnie na dole.
        lcdDrawString(&lcd, fx, 140, 225, (uint8_t *)"Czas ->", BLACK);

        // Ramka wykresu
        lcdDrawRect(&lcd, GRAPH_X_START, GRAPH_Y_START, GRAPH_X_START + GRAPH_WIDTH, GRAPH_Y_START + GRAPH_HEIGHT, BLACK);

        xSemaphoreGive(xSpiMutex);
    }

    while(1) {
        // ... (reszta pętli while bez zmian - skopiuj ze swojego poprzedniego kodu) ...
        // PAMIĘTAJ: Funkcja draw_chart_lines() używa stałych GRAPH_HEIGHT, 
        // więc po zmianie #define sama się naprawi!
        
        // Czekamy na dane
        BaseType_t status = xQueueReceive(xSensorQueue, &receivedData, pdMS_TO_TICKS(500));
        bool updateSensor = false;

        if (status == pdTRUE) {
            if (receivedData.temp_hundredths != currentData.temp_hundredths ||
                receivedData.hum_x1024 != currentData.hum_x1024 ||
                firstRun) 
            {
                currentData = receivedData;
                add_to_history(&currentData); 
                updateSensor = true;
                firstRun = false;
                hasData = true;
            }
        }

        time(&now);
        localtime_r(&now, &timeinfo);
        bool updateClock = (timeinfo.tm_sec != last_sec); 
        last_sec = timeinfo.tm_sec;

        if ((updateSensor || updateClock) && xSemaphoreTake(xSpiMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            lcdSetFontDirection(&lcd, DIRECTION0);

            if (updateSensor || !hasData) {
                // Rysowanie wartości tekstowych (kopiuj swoje poprzednie sprintf'y tutaj)
                // Temp
                lcdDrawFillRect(&lcd, 5, 30, 170, 50, WHITE);
                if (!hasData) sprintf(buffer, "Temperatura: --.-- C");
                else sprintf(buffer, "Temperatura: %.2f C", currentData.temp_hundredths / 100.0f);
                lcdDrawString(&lcd, fx, 5, 45, (uint8_t*)buffer, BLACK);

                // Wilgotnosc
                lcdDrawFillRect(&lcd, 5, 50, 170, 70, WHITE);
                if (!hasData) sprintf(buffer, "Wilgotnosc: --.-- %%");
                else sprintf(buffer, "Wilgotnosc: %.2f %%", currentData.hum_x1024 / 1024.0f);
                lcdDrawString(&lcd, fx, 5, 65, (uint8_t*)buffer, BLACK);

                // Cisnienie
                lcdDrawFillRect(&lcd, 5, 70, 170, 90, WHITE);
                if (!hasData) sprintf(buffer, "Cisnienie: ---- hPa");
                else sprintf(buffer, "Cisnienie: %.0f hPa", currentData.pressure_pa / 100.0f);
                lcdDrawString(&lcd, fx, 5, 85, (uint8_t*)buffer, BLACK);

                if (hasData) {
                    draw_chart_lines();
                    lcdDrawRect(&lcd, GRAPH_X_START, GRAPH_Y_START, GRAPH_X_START + GRAPH_WIDTH, GRAPH_Y_START + GRAPH_HEIGHT, BLACK);
                }
            }

            if (updateClock) {
                if (timeinfo.tm_year > (2020 - 1900)) {
                    sprintf(date_buf, "%02d.%02d.%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
                    lcdDrawFillRect(&lcd, 210, 30, 315, 50, WHITE);
                    lcdDrawString(&lcd, fx, 215, 45, (uint8_t*)date_buf, BLACK);

                    sprintf(time_buf, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
                    lcdDrawFillRect(&lcd, 210, 70, 315, 90, WHITE);
                    lcdDrawString(&lcd, fx, 215, 85, (uint8_t*)time_buf, BLACK);
                } else {
                    lcdDrawString(&lcd, fx, 215, 85, (uint8_t*)"Synch...", RED);
                }
            }
            xSemaphoreGive(xSpiMutex);
        }
    }
}

void app_main(void) {
    // 1. Inicjalizacja Pamięci
    init_nvs();
    
    // TWORZENIE MUTEXA
    xSpiMutex = xSemaphoreCreateMutex();
    
    // TWORZENIE KOLEJKI (Dla 1 elementu SensorData - wystarczy najnowszy)
    xSensorQueue = xQueueCreate(1, sizeof(SensorData)); // <--- WAŻNE!
    
    if(xSpiMutex == NULL || xSensorQueue == NULL) {
        ESP_LOGE("MAIN", "Błąd alokacji zasobów RTOS!");
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