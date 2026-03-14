#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "wifi_project.h"

extern bool is_wifi_connecting;

static const char *TAG = "WIFI_MODULE";

static EventGroupHandle_t s_wifi_event_group = NULL;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
#define MAX_RETRY 5

static bool is_wifi_initialized = false;
static bool s_is_switching_network = false;

wifi_ap_record_t wifi_list[10]; 
uint16_t wifi_count = 0; 

// --- HANDLER ZDARZEŃ ---
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // TUTAJ BYŁ BŁĄD! Usunąłem automatyczne esp_wifi_connect().
        // Nic tu nie robimy, łączymy się świadomie wywołując funkcję na końcu.
        ESP_LOGI(TAG, "Radio WiFi wystartowalo. Czekam na komendy...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        
        if (s_is_switching_network) {
            ESP_LOGI(TAG, "Celowe rozlaczenie - wstrzymuje auto-reconnect.");
            return;
        }
        
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            is_wifi_connecting = true; 
            ESP_LOGI(TAG, "Ponawianie proby polaczenia (%d/%d)...", s_retry_num, MAX_RETRY);
        } else {
            is_wifi_connecting = false; 
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGW(TAG, "Przekroczono limit prob polaczenia! Wyswietlam blad na ekranie.");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_retry_num = 0;
        is_wifi_connecting = false; 
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// --- INICJALIZACJA BAZOWA ---
static void ensure_wifi_initialized(void) {
    if (is_wifi_initialized) return;

    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    is_wifi_initialized = true;
    ESP_LOGI(TAG, "Modul WiFi poprawnie zainicjowany.");
}

// --- ŁĄCZENIE Z SIECIĄ ---
bool wifi_connect_station(const char *ssid, const char *password) {
    ensure_wifi_initialized(); 

    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    s_is_switching_network = true; 
    
    // Bezpiecznie rozłączamy starą sieć
    esp_wifi_disconnect(); 
    vTaskDelay(pdMS_TO_TICKS(100)); // Dać mu 100ms na ogarnięcie rozłączenia
    
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    
    // Ustawienia kompatybilności dla nowych ruterów i hotspotów
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    if (strlen(password) > 0) {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    // MECHANIZM AWARYJNY: Jeśli próba wgrania konfigu zgłosi błąd stanu, używamy spadochronu (stop/start)
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Ostrzezenie: sterownik byl zajety (%s). Wymuszam twardy reset radia...", esp_err_to_name(err));
        esp_wifi_stop();
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        esp_wifi_start();
    }
    
    s_is_switching_network = false;
    s_retry_num = 0;
    
    // Startujemy łączenie ręcznie!
    esp_err_t conn_err = esp_wifi_connect();
    if (conn_err != ESP_OK) {
        ESP_LOGE(TAG, "Ostrzezenie przy wywolaniu connect: %s", esp_err_to_name(conn_err));
    }

    ESP_LOGI(TAG, "Oczekiwanie na polaczenie z %s...", ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Sukces! Posiadamy IP.");
        return true;
    } else {
        ESP_LOGW(TAG, "Nie udalo sie polaczyc z %s", ssid);
        return false;
    }
}

// --- SKANOWANIE SIECI ---
void wifi_scan_networks(void) {
    ensure_wifi_initialized(); 

    wifi_scan_config_t scan_config = {
        .ssid = 0,
        .bssid = 0,
        .channel = 0,
        .show_hidden = false
    };

    ESP_LOGI(TAG, "Rozpoczynam skanowanie...");
    
    bool resume_connection = false; // <-- Flaga pamiętająca o wznowieniu
    
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    
    // Jeśli sterownik odrzucił skanowanie bo jest w trakcie łączenia
    if (err == ESP_ERR_WIFI_STATE) {
        ESP_LOGW(TAG, "Skanowanie zablokowane. Wymuszam rozlaczenie na czas skanu...");
        
        s_is_switching_network = true; 
        esp_wifi_disconnect();         
        vTaskDelay(pdMS_TO_TICKS(100)); 
        
        err = esp_wifi_scan_start(&scan_config, true);
        
        s_is_switching_network = false; 
        resume_connection = true; // Zaznaczamy: "Na końcu musisz wznowić połączenie!"
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Nie udalo sie wykonac skanowania: %s", esp_err_to_name(err));
        wifi_count = 0;
        if (resume_connection) esp_wifi_connect(); // Wznawiamy nawet w razie błędu skanera
        return;
    }

    uint16_t number = 10;
    
    // 1. NAJPIERW POBIERAMY ZESKANOWANE SIECI Z BUFORA
    err = esp_wifi_scan_get_ap_records(&number, wifi_list);
    if (err == ESP_OK) {
        wifi_count = number;
        ESP_LOGI(TAG, "Znaleziono %d sieci", wifi_count);
    } else {
        ESP_LOGE(TAG, "Blad pobierania wynikow skanowania: %s", esp_err_to_name(err));
        wifi_count = 0;
    }

    // 2. DOPIERO TERAZ, KIEDY WYNIKI SĄ BEZPIECZNE, WZNAWIAMY ŁĄCZENIE
    if (resume_connection) {
        ESP_LOGI(TAG, "Skanowanie zakonczone. Wznawiam przerwane polaczenie...");
        esp_wifi_connect();
    }
}