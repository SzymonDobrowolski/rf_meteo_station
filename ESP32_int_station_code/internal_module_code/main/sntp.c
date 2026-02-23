#include "sntp.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "Config.h"

static const char *TAG = "SNTP_MODULE";

void sntp_init_module(void)
{
    ESP_LOGI(TAG, "Inicjalizacja SNTP");
    
    // 1. Konfiguracja trybu działania
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    
    // 2. Ustawienie serwera czasu (pool.ntp.org to ogólnoświatowy klaster)
    // Możesz też użyć "pl.pool.ntp.org" dla serwerów w Polsce
    esp_sntp_setservername(0, SNTP_SERVER);
    
    // 3. Start usługi
    esp_sntp_init();
}

int wait_for_time_sync(void)
{
    int retry = 0;
    const int retry_count = 15; // Czekamy max 30 sekund (15 * 2s)
    
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Czekam na czas systemowy... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    if (retry == retry_count) {
        ESP_LOGE(TAG, "Nie udalo sie pobrac czasu!");
        return 0;
    }

    ESP_LOGI(TAG, "Czas zsynchronizowany!");
    return 1;
}