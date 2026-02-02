#include <stdio.h>
#include <time.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"

// Sterowniki sprzętowe
#include "gpio_init.h"
#include "spi_init.h"
#include "uart_init.h"
#include "nrf.h"
#include "lcd.h"
#include "ili9340.h"
#include "fontx.h"

// Twoje moduły
#include "wifi_project.h"
#include "sntp.h"
#include "Config.h" // Upewnij się, że BUTTON_1 jest tu zdefiniowany

// --- KONFIGURACJA WYKRESÓW ---
#define GRAPH_X_START 40
#define GRAPH_Y_START 130
#define GRAPH_WIDTH   240
#define GRAPH_HEIGHT  70  
#define GRAPH_POINTS  100

// --- KONFIGURACJA PEŁNEGO EKRANU ---
#define FS_CHUNK_HEIGHT  60

// Marginesy (Tu robimy miejsce na napisy!)
#define FS_MARGIN_TOP    40
#define FS_MARGIN_BOTTOM 25
#define FS_MARGIN_LEFT   35   // 35px wolnego miejsca z lewej na "35", "15"
#define FS_MARGIN_RIGHT  35   // 35px wolnego miejsca z prawej na "80", "20"

// Obliczamy szerokość SAMEGO OBSZARU RYSOWANIA (Wykresu)
// 320 - 35 - 35 = 250 pikseli szerokości na wykres
#define FS_GRAPH_WIDTH   (LCD_WIDTH - FS_MARGIN_LEFT - FS_MARGIN_RIGHT)
#define FS_GRAPH_HEIGHT  (LCD_HEIGHT - FS_MARGIN_TOP - FS_MARGIN_BOTTOM)

// Start Y bez zmian
#define FS_GRAPH_Y_START FS_MARGIN_TOP

// Rozmiar bufora pozostaje bez zmian (38.4 KB) - buforujemy całą szerokość ekranu
#define MAX_CHART_BUF_SIZE (LCD_WIDTH * FS_CHUNK_HEIGHT * 2)

// ZAKRESY POMIAROWE
#define MIN_TEMP 15.0f
#define MAX_TEMP 35.0f
#define MIN_HUM  15.0f
#define MAX_HUM  80.0f
#define MIN_PRESS 980.0f
#define MAX_PRESS 1040.0f

// --- ZMIENNE GLOBALNE ---
uint16_t *chart_buffer = NULL; // Główny bufor graficzny
uint16_t *text_buffer = NULL;  // Mały bufor na teksty

// Dane historyczne
float hist_temp[GRAPH_POINTS] = {0};
float hist_hum[GRAPH_POINTS]  = {0};
float hist_press[GRAPH_POINTS]= {0};
int   hist_idx = 0;           
bool  hist_full = false;   

// Obiekty systemowe
TFT_t lcd;
spi_device_handle_t nrf_handle = NULL;
FontxFile fx[2];
SemaphoreHandle_t xSpiMutex = NULL;
QueueHandle_t xSensorQueue = NULL;

// --- INICJALIZACJE ---
void init_spiffs() {
    esp_vfs_spiffs_conf_t conf = { .base_path = "/spiflash", .partition_label = NULL, .max_files = 5, .format_if_mount_failed = true };
    esp_vfs_spiffs_register(&conf);
}

void init_nvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) { nvs_flash_erase(); nvs_flash_init(); }
}

// --- TASKS ---
void nrf_receiver_task(void *pvParameters) {
    SensorData localData = {0};
    while(1) {
        if (xSpiMutex && xSemaphoreTake(xSpiMutex, pdMS_TO_TICKS(100))) {
            nrf_receive_data(nrf_handle, &localData);
            xSemaphoreGive(xSpiMutex);
            xQueueOverwrite(xSensorQueue, &localData);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void collect_time_task(void *pvParameters) {
    // Pamiętaj o ustawieniu poprawnego SSID i Hasła!
    if (wifi_connect_station(SSID, PASSWORD)) { 
        sntp_init_module(); 
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); 
        tzset();
        while(1) { wait_for_time_sync(); vTaskDelay(pdMS_TO_TICKS(3600 * 1000)); }
    } else { 
        vTaskDelete(NULL); 
    }
}

// ==========================================
// FUNKCJE GRAFICZNE
// ==========================================

// Rysowanie tekstu bez migotania
void draw_text_buffered(int x, int y, char *str, uint16_t text_color) {
    if (!text_buffer) return; 

    int max_w = 180; 
    int h = 20;  
    int w = max_w;
    if (x + w > LCD_WIDTH) w = LCD_WIDTH - x;

    // Czyścimy bufor (białe tło)
    for (int i = 0; i < w * h; i++) text_buffer[i] = 0xFFFF;

    uint8_t fonts[256];
    uint8_t font_w, font_h;
    int cur_x = 0;
    char *p = str;

    while (*p) {
        if (GetFontx(fx, (uint8_t)*p, fonts, &font_w, &font_h)) {
            for (int cy = 0; cy < font_h; cy++) {
                for (int cx = 0; cx < font_w; cx++) {
                    if (fonts[(cy * ((font_w + 7) / 8)) + (cx / 8)] & (0x80 >> (cx % 8))) {
                        int bx = cur_x + cx;
                        int by = cy + (h - font_h);
                        if (bx < w && by < h && by >= 0) {
                            text_buffer[by * w + bx] = (text_color >> 8) | (text_color << 8);
                        }
                    }
                }
            }
            cur_x += font_w;
        }
        p++;
    }
    ili9341_draw_image(&lcd, x, y - h, w, h, text_buffer);
}

// Funkcje pomocnicze do rysowania w buforze
void drawPixelChart(int x, int y, uint16_t color) {
    chart_buffer[y * GRAPH_WIDTH + x] = (color >> 8) | (color << 8);
}

void drawLineChart(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    while (1) {
        if (x0 >= 0 && x0 < GRAPH_WIDTH && y0 >= 0 && y0 < GRAPH_HEIGHT)
            drawPixelChart(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void drawRectChart(int x, int y, int w, int h, uint16_t color) {
    drawLineChart(x, y, x + w - 1, y, color);
    drawLineChart(x, y + h - 1, x + w - 1, y + h - 1, color);
    drawLineChart(x, y, x, y + h - 1, color);
    drawLineChart(x + w - 1, y, x + w - 1, y + h - 1, color);
}

uint16_t map_val(float value, float min_v, float max_v) {
    if (value < min_v) value = min_v;
    if (value > max_v) value = max_v;
    float ratio = (value - min_v) / (max_v - min_v);
    return GRAPH_HEIGHT - 2 - (uint16_t)(ratio * (GRAPH_HEIGHT - 4));
}

void add_to_history(SensorData *d) {
    hist_temp[hist_idx] = d->temp_hundredths / 100.0f;
    hist_hum[hist_idx]  = d->hum_x1024 / 1024.0f;
    hist_press[hist_idx]= d->pressure_pa / 100.0f;
    hist_idx++;
    if (hist_idx >= GRAPH_POINTS) { hist_idx = 0; hist_full = true; }
}

// RENDEROWANIE MAŁEGO WYKRESU
void render_chart() {
    if (!chart_buffer) return;
    
    // Tło
    for (int i = 0; i < GRAPH_WIDTH * GRAPH_HEIGHT; i++) chart_buffer[i] = 0xFFFF;
    
    // Siatka
    int mid_y = GRAPH_HEIGHT / 2;
    for(int x=1; x<GRAPH_WIDTH-1; x+=4) drawPixelChart(x, mid_y, GRAY);

    // Wykresy
    int count = hist_full ? GRAPH_POINTS : hist_idx;
    int start_i = hist_full ? hist_idx : 0;
    float step_x = (float)(GRAPH_WIDTH - 2) / (float)(GRAPH_POINTS - 1);

    for (int i = 0; i < count - 1; i++) {
        int idx_now = (start_i + i) % GRAPH_POINTS;
        int idx_next = (start_i + i + 1) % GRAPH_POINTS;
        int x1 = 1 + (int)(i * step_x);
        int x2 = 1 + (int)((i + 1) * step_x);
        
        drawLineChart(x1, map_val(hist_temp[idx_now], MIN_TEMP, MAX_TEMP), x2, map_val(hist_temp[idx_next], MIN_TEMP, MAX_TEMP), RED);
        drawLineChart(x1, map_val(hist_hum[idx_now], MIN_HUM, MAX_HUM), x2, map_val(hist_hum[idx_next], MIN_HUM, MAX_HUM), BLUE);
        drawLineChart(x1, map_val(hist_press[idx_now], MIN_PRESS, MAX_PRESS), x2, map_val(hist_press[idx_next], MIN_PRESS, MAX_PRESS), GREEN);
    }
    
    // Ramka
    drawRectChart(0, 0, GRAPH_WIDTH, GRAPH_HEIGHT, BLACK);

    // Wysyłanie
    int chunk = 10; 
    for (int y = 0; y < GRAPH_HEIGHT; y += chunk) {
        int h = (y + chunk > GRAPH_HEIGHT) ? (GRAPH_HEIGHT - y) : chunk;
        ili9341_draw_image(&lcd, GRAPH_X_START, GRAPH_Y_START + y, GRAPH_WIDTH, h, &chart_buffer[y * GRAPH_WIDTH]);
    }
}

// x i y są RELATYWNE względem ramki wykresu (0,0 to lewy górny róg wykresu,
void draw_relative_line(uint16_t *buff, int chunk_start_y, int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (1) {
        // Przeliczamy Y absolutny na lokalny w pasku (chunck)
        int local_y = y0 - chunk_start_y;

        // Sprawdzamy czy punkt jest w buforze
        // UWAGA: Używamy FS_GRAPH_WIDTH zamiast LCD_WIDTH, bo bufor jest teraz węższy!
        if (local_y >= 0 && local_y < FS_CHUNK_HEIGHT && x0 >= 0 && x0 < FS_GRAPH_WIDTH) {
            buff[local_y * FS_GRAPH_WIDTH + x0] = (color >> 8) | (color << 8);
        }

        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// Wersja POGRUBIONA (tylko do linii danych na dużym wykresie)
void draw_relative_line_Thick(uint16_t *buff, int chunk_start_y, int x0, int y0, int x1, int y1, uint16_t color) {
    // 1. Linia główna
    draw_relative_line(buff, chunk_start_y, x0, y0, x1, y1, color);
    // 2. Linia pogrubiająca (1px niżej)
    draw_relative_line(buff, chunk_start_y, x0, y0 + 1, x1, y1 + 1, color);
    // 3. Linia pogrubiająca (1px niżej)
    draw_relative_line(buff, chunk_start_y, x0, y0 + 2, x1, y1 + 2, color);
    // 4. Linia pogrubiająca (1px niżej)
    draw_relative_line(buff, chunk_start_y, x0, y0 + 3, x1, y1 + 3, color);
}

// Funkcja pomocnicza dla Full Screen (z obsługą marginesów bufora)
void draw_fs_line(uint16_t *buff, int chunk_start_y, int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    while (1) {
        int local_y = y0 - chunk_start_y;
        // Sprawdzamy, czy punkt mieści się w AKTUALNYM pasku bufora oraz w szerokości ekranu
        if (local_y >= 0 && local_y < FS_CHUNK_HEIGHT && x0 >= 0 && x0 < LCD_WIDTH) {
            buff[local_y * LCD_WIDTH + x0] = (color >> 8) | (color << 8);
        }
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// RENDEROWANIE PEŁNOEKRANOWE (GRUBSZE LINIE DANYCH, CIENKA RESZTA)
void render_fullscreen_chart() {
    if (!chart_buffer) return;

    #define MAP_FS(val, min, max) ((FS_GRAPH_Y_START + FS_GRAPH_HEIGHT - 1) - (int)(((val - min) / (max - min)) * (FS_GRAPH_HEIGHT - 1)))

    for (int chunk_start_y = FS_GRAPH_Y_START; chunk_start_y < (FS_GRAPH_Y_START + FS_GRAPH_HEIGHT); chunk_start_y += FS_CHUNK_HEIGHT) {
        
        // 1. Czyścimy
        int buf_size = FS_GRAPH_WIDTH * FS_CHUNK_HEIGHT;
        for (int i = 0; i < buf_size; i++) chart_buffer[i] = 0xFFFF;

        // 2. Siatka (CIENKA)
        for (int gy = FS_GRAPH_Y_START; gy < (FS_GRAPH_Y_START + FS_GRAPH_HEIGHT); gy += 40) { 
            int local_y = gy - chunk_start_y;
            if (local_y >= 0 && local_y < FS_CHUNK_HEIGHT) {
                for(int x = 0; x < FS_GRAPH_WIDTH; x+=4) {
                    chart_buffer[local_y * FS_GRAPH_WIDTH + x] = (0xCE79 >> 8) | (0xCE79 << 8);
                }
            }
        }

        // 3. Dane (GRUBSZE)
        float step_x = (float)FS_GRAPH_WIDTH / (float)(GRAPH_POINTS - 1);
        int count = hist_full ? GRAPH_POINTS : hist_idx;
        int start_i = hist_full ? hist_idx : 0;

        for (int i = 0; i < count - 1; i++) {
            int idx_now = (start_i + i) % GRAPH_POINTS;
            int idx_next = (start_i + i + 1) % GRAPH_POINTS;
            
            int x1 = (int)(i * step_x);
            int x2 = (int)((i + 1) * step_x);

            // TU ZOSTAWIONE: draw_relative_line_Thick
            draw_relative_line_Thick(chart_buffer, chunk_start_y, x1, MAP_FS(hist_temp[idx_now], MIN_TEMP, MAX_TEMP), x2, MAP_FS(hist_temp[idx_next], MIN_TEMP, MAX_TEMP), RED);
            draw_relative_line_Thick(chart_buffer, chunk_start_y, x1, MAP_FS(hist_hum[idx_now], MIN_HUM, MAX_HUM), x2, MAP_FS(hist_hum[idx_next], MIN_HUM, MAX_HUM), BLUE);
            draw_relative_line_Thick(chart_buffer, chunk_start_y, x1, MAP_FS(hist_press[idx_now], MIN_PRESS, MAX_PRESS), x2, MAP_FS(hist_press[idx_next], MIN_PRESS, MAX_PRESS), GREEN);
        }

        // 4. Ramka (CIENKA)
        int h_max = FS_GRAPH_HEIGHT - 1; 
        int w_max = FS_GRAPH_WIDTH - 1;  
        
        draw_relative_line(chart_buffer, chunk_start_y, 0, FS_GRAPH_Y_START, 0, FS_GRAPH_Y_START + h_max, BLACK);       
        draw_relative_line(chart_buffer, chunk_start_y, w_max, FS_GRAPH_Y_START, w_max, FS_GRAPH_Y_START + h_max, BLACK); 
        draw_relative_line(chart_buffer, chunk_start_y, 0, FS_GRAPH_Y_START, w_max, FS_GRAPH_Y_START, BLACK);             
        draw_relative_line(chart_buffer, chunk_start_y, 0, FS_GRAPH_Y_START + h_max, w_max, FS_GRAPH_Y_START + h_max, BLACK); 

        // 5. Wyślij
        int h = FS_CHUNK_HEIGHT;
        if (chunk_start_y + h > (FS_GRAPH_Y_START + FS_GRAPH_HEIGHT)) 
            h = (FS_GRAPH_Y_START + FS_GRAPH_HEIGHT) - chunk_start_y;

        if (h > 0) {
            ili9341_draw_image(&lcd, FS_MARGIN_LEFT, chunk_start_y, FS_GRAPH_WIDTH, h, chart_buffer);
        }
        
        vTaskDelay(1); 
    }
}

// --- TASK GUI ---
void lcd_gui_task(void *pvParameters) {
    char buffer[64];
    SensorData currentData = {0}, receivedData = {0};      
    bool firstRun = true, hasData = false; 
    time_t now; struct tm timeinfo; int last_sec = -1;
    int current_screen = 0, last_screen = -1;   
    bool button_pressed = false;

    // Początkowe czyszczenie
    xSemaphoreTake(xSpiMutex, portMAX_DELAY);
    lcdFillScreen(&lcd, WHITE);
    xSemaphoreGive(xSpiMutex);

    while(1) {
        // Obsługa przycisku
        if (gpio_get_level(BUTTON_2) == 0) { 
            if (!button_pressed) {
                current_screen = !current_screen; 
                button_pressed = true;
                vTaskDelay(pdMS_TO_TICKS(100)); // Debouncing
            }
        } else { 
            button_pressed = false; 
        }

        // Odbiór danych
        BaseType_t status = xQueueReceive(xSensorQueue, &receivedData, pdMS_TO_TICKS(100));
        bool updateSensor = false;

        if (status == pdTRUE) {
            if (receivedData.temp_hundredths != currentData.temp_hundredths ||
                receivedData.hum_x1024 != currentData.hum_x1024 || firstRun) 
            {
                currentData = receivedData;
                add_to_history(&currentData); 
                updateSensor = true;
                firstRun = false;
                hasData = true;
            }
        }

        time(&now); localtime_r(&now, &timeinfo);
        bool updateClock = (timeinfo.tm_sec != last_sec); 
        last_sec = timeinfo.tm_sec;

        if (xSemaphoreTake(xSpiMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            lcdSetFontDirection(&lcd, DIRECTION0);

            // --- FAZA 1: RYSOWANIE TŁA/SZABLONU (TYLKO RAZ PRZY ZMIANIE) ---
            if (current_screen != last_screen) {
                lcdFillScreen(&lcd, WHITE); 
                
                if (current_screen == 0) {
                    // === SZABLON EKRANU GŁÓWNEGO ===
                    lcdDrawRect(&lcd, 0, 0, 200, 100, BLACK);
                    lcdDrawRect(&lcd, 201, 0, 320, 100, BLACK);
                    
                    lcdDrawString(&lcd, fx, 5, 25, (uint8_t *)"Dane z czujnika", BLACK);
                    lcdDrawString(&lcd, fx, 215, 25, (uint8_t *)"DATA", BLACK);
                    lcdDrawString(&lcd, fx, 215, 65, (uint8_t *)"CZAS", BLACK);
                    
                    // Legenda
                    int leg_y = GRAPH_Y_START - 20;
                    lcdDrawFillRect(&lcd, 40, leg_y, 50, leg_y + 10, RED);
                    lcdDrawString(&lcd, fx, 55, leg_y+15, (uint8_t *)"Temp", BLACK);
                    lcdDrawFillRect(&lcd, 130, leg_y, 140, leg_y + 10, BLUE);
                    lcdDrawString(&lcd, fx, 145, leg_y+15, (uint8_t *)"Wilg", BLACK);
                    lcdDrawFillRect(&lcd, 220, leg_y, 230, leg_y + 10, GREEN);
                    lcdDrawString(&lcd, fx, 235, leg_y+15, (uint8_t *)"Cisn", BLACK);
                    
                    // Osie dla małego wykresu
                    char ax[10];
                    sprintf(ax, "%.0fC", MAX_TEMP); lcdDrawString(&lcd, fx, 2, GRAPH_Y_START, (uint8_t *)ax, RED); 
                    sprintf(ax, "%.0fC", MIN_TEMP); lcdDrawString(&lcd, fx, 2, GRAPH_Y_START + GRAPH_HEIGHT - 10, (uint8_t *)ax, RED); 
                    sprintf(ax, "%.0f%%", MAX_HUM); lcdDrawString(&lcd, fx, GRAPH_X_START + GRAPH_WIDTH + 2, GRAPH_Y_START, (uint8_t *)ax, BLUE); 
                    sprintf(ax, "%.0f%%", MIN_HUM); lcdDrawString(&lcd, fx, GRAPH_X_START + GRAPH_WIDTH + 2, GRAPH_Y_START + GRAPH_HEIGHT - 10, (uint8_t *)ax, BLUE); 
                    lcdDrawString(&lcd, fx, 140, 225, (uint8_t *)"Czas ->", BLACK);
                } 
                else {
                    // === SZABLON EKRANU PEŁNEGO (LEGENDA + OSIE) ===
                    
                    // 1. Legenda na górze (bez zmian)
                    int leg_y = 10; 
                    lcdDrawFillRect(&lcd, 10, leg_y, 20, leg_y + 10, RED);
                    lcdDrawString(&lcd, fx, 25, leg_y + 10, (uint8_t *)"Temp", BLACK);
                    lcdDrawFillRect(&lcd, 80, leg_y, 90, leg_y + 10, BLUE);
                    lcdDrawString(&lcd, fx, 95, leg_y + 10, (uint8_t *)"Wilg", BLACK);
                    lcdDrawFillRect(&lcd, 150, leg_y, 160, leg_y + 10, GREEN);
                    lcdDrawString(&lcd, fx, 165, leg_y + 10, (uint8_t *)"Cisn", BLACK);

                    // 2. Osie Y - TERAZ NA MARGINESACH
                    char ax[16];
                    
                    // Lewa oś (Temp - Czerwony) - Margines ma 35px, piszemy na x=2
                    sprintf(ax, "%.0f", MAX_TEMP); 
                    lcdDrawString(&lcd, fx, 2, FS_GRAPH_Y_START + 12, (uint8_t *)ax, RED); 
                    
                    sprintf(ax, "%.0f", MIN_TEMP); 
                    lcdDrawString(&lcd, fx, 2, FS_GRAPH_Y_START + FS_GRAPH_HEIGHT - 2, (uint8_t *)ax, RED); 
                    
                    // Prawa oś (Wilg - Niebieski) - Piszemy na x = 320 - 30 = 290
                    sprintf(ax, "%.0f", MAX_HUM); 
                    lcdDrawString(&lcd, fx, LCD_WIDTH - 30, FS_GRAPH_Y_START + 12, (uint8_t *)ax, BLUE); 
                    
                    sprintf(ax, "%.0f", MIN_HUM); 
                    lcdDrawString(&lcd, fx, LCD_WIDTH - 30, FS_GRAPH_Y_START + FS_GRAPH_HEIGHT - 2, (uint8_t *)ax, BLUE); 
                    
                    // 3. Oś X (Czas) - Na dole, pod wykresem
                    // Wykres kończy się na Y = 215 (40 + 175). Mamy miejsce do 240.
                    lcdDrawString(&lcd, fx, 130, LCD_HEIGHT - 5, (uint8_t *)"Czas ->", BLACK);
                }
                updateSensor = true; 
                updateClock = true;
                last_screen = current_screen;
            }

            // --- FAZA 2: AKTUALIZACJA DANYCH (DYNAMICZNA) ---
            if (current_screen == 0) {
                // EKRAN GŁÓWNY
                if (updateSensor || !hasData) {
                    if (!hasData) sprintf(buffer, "Temperatura: --.-- C");
                    else sprintf(buffer, "Temperatura: %.2f C", currentData.temp_hundredths / 100.0f);
                    draw_text_buffered(5, 45, buffer, BLACK);

                    if (!hasData) sprintf(buffer, "Wilgotnosc: --.-- %%");
                    else sprintf(buffer, "Wilgotnosc: %.2f %%", currentData.hum_x1024 / 1024.0f);
                    draw_text_buffered(5, 65, buffer, BLACK);

                    if (!hasData) sprintf(buffer, "Cisnienie: ---- hPa");
                    else sprintf(buffer, "Cisnienie: %.0f hPa", currentData.pressure_pa / 100.0f);
                    draw_text_buffered(5, 85, buffer, BLACK);

                    if (hasData) render_chart(); 
                }
                // Zegar
                if (updateClock) {
                     if (timeinfo.tm_year > (2020 - 1900)) {
                        sprintf(buffer, "%02d.%02d.%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
                        draw_text_buffered(215, 45, buffer, BLACK);
                        sprintf(buffer, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
                        draw_text_buffered(215, 85, buffer, BLACK);
                     } else {
                        draw_text_buffered(215, 85, "Synch...", RED);
                     }
                }
            } 
            else { 
                // EKRAN PEŁNY
                if (updateSensor && hasData) {
                    render_fullscreen_chart();
                    // Opcjonalnie aktualny odczyt
                    sprintf(buffer, "%.1f C", currentData.temp_hundredths / 100.0f);
                    draw_text_buffered(240, 25, buffer, BLACK);
                }
            }

            xSemaphoreGive(xSpiMutex);
        }
    }
}

// --- APP MAIN ---
void app_main(void) {
    init_nvs();
    
    // 1. REZERWACJA PAMIĘCI GRAFICZNEJ (BARDZO WAŻNE!)
    // Alokujemy pamięć raz na starcie. Zapobiega to fragmentacji i błędom.
    ESP_LOGI("MEM", "Rezerwacja RAM dla grafiki...");
    chart_buffer = heap_caps_malloc(MAX_CHART_BUF_SIZE, MALLOC_CAP_DMA);
    text_buffer  = heap_caps_malloc(180 * 20 * 2, MALLOC_CAP_DMA);

    if (!chart_buffer || !text_buffer) {
        ESP_LOGE("MEM", "Brak pamieci RAM na starcie! Restart...");
        esp_restart();
    }
    
    // Wstępne czyszczenie
    for(int i=0; i<MAX_CHART_BUF_SIZE/2; i++) chart_buffer[i] = 0xFFFF;

    // 2. Inicjalizacja RTOS
    xSpiMutex = xSemaphoreCreateMutex();
    xSensorQueue = xQueueCreate(1, sizeof(SensorData));
    
    // 3. Inicjalizacja Sprzętu
    gpio_init();
    spi_init();
    uart_init();
    nrf_init(&nrf_handle);
    init_spiffs();
    lcd_init();
    
    // Fonty
    InitFontx(fx, "/spiflash/ILMH16XB.FNT", ""); 
    OpenFontx(fx);

    // 4. Start Tasków
    // GUI dostaje więcej pamięci stosu (16KB) dla bezpieczeństwa przy dużych operacjach
    xTaskCreate(nrf_receiver_task, "NRF", 4096, NULL, 5, NULL);
    xTaskCreate(collect_time_task, "TIME", 8192, NULL, 5, NULL); 
    xTaskCreate(lcd_gui_task, "GUI", 16384, NULL, 5, NULL); 
}