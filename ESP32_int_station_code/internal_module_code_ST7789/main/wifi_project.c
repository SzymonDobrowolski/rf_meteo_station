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

// Grupa zdarzeń do synchronizacji (żebyśmy mogli poczekać na połączenie)
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
#define MAX_RETRY 5

// --- OBSŁUGA ZDARZEŃ (TO JEST "MÓZG" WIFI) ---
// W wifi_project.c

wifi_ap_record_t wifi_list[10]; // Tablica do przechowywania wyników skanowania
uint16_t wifi_count = 0; // Zmienna globalna do przechowywania liczby znalezionych sieci

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        is_wifi_connecting = true; 
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            is_wifi_connecting = true;
            ESP_LOGI(TAG, "Ponawianie proby polaczenia...");
        } else {
            is_wifi_connecting = false;
            // DODAJ TO: Informujemy funkcje wifi_connect_station o bledzie
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        is_wifi_connecting = false; 
        s_retry_num = 0;
        // DODAJ TO: Informujemy funkcje wifi_connect_station o sukcesie
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}


bool wifi_connect_station(const char *ssid, const char *password)
{
    is_wifi_connecting = true; // Ustawiamy flagę, że zaczynamy łączyć się

    // 1. Tworzymy grupę zdarzeń
    s_wifi_event_group = xEventGroupCreate();

    // 2. Inicjalizacja warstwy sieciowej (Netif)
    ESP_ERROR_CHECK(esp_netif_init());

    // 3. Tworzymy domyślną pętlę zdarzeń systemowych
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // 4. Domyślna konfiguracja WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 5. Rejestracja funkcji 'event_handler' do nasłuchiwania zdarzeń
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    // 6. Ustawienie SSID i Hasła
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK, // Wymagane WPA2 (bezpieczeństwo)
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    // Kopiujemy bezpiecznie stringi do struktury (bo struktura ma tablice o stałej długości)
    strlcpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

    // 7. Wgranie konfiguracji
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) ); // Tryb Station (Klient)
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    
    // 8. START!
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "Oczekiwanie na polaczenie...");

    // 9. Czekanie na wynik (blokujemy program w tym miejscu aż dostaniemy IP lub Błąd)
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    // 10. Analiza wyniku
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Polaczono z SSID: %s", ssid);
        return true;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Blad polaczenia z SSID: %s", ssid);
        return false;
    } else {
        ESP_LOGE(TAG, "Nieoczekiwane zdarzenie");
        return false;
    }
}

void wifi_scan_networks(void) {
    wifi_scan_config_t scan_config = {
        .ssid = 0,
        .bssid = 0,
        .channel = 0,
        .show_hidden = false
    };

    ESP_LOGI("WIFI", "Rozpoczynam skanowanie...");
    
    // Uruchomienie skanowania (true = blokujące, czeka na zakończenie)
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

    // Ograniczamy liczbę pobieranych wyników do rozmiaru naszej tablicy (10)
    uint16_t number = 10;
    
    // ESP-IDF wpisuje zeskanowane sieci BEZPOŚREDNIO do naszej tablicy wifi_list!
    // Nie musimy robić żadnych pętli ani snprintf.
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, wifi_list));
    
    // Zapisujemy, ile faktycznie sieci znalazł (od 0 do 10)
    wifi_count = number;

    ESP_LOGI("WIFI", "Znaleziono %d sieci", wifi_count);
}