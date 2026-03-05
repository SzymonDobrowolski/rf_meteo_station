#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_wifi.h"

// Deklaracje zmiennych globalnych, zdefiniowanych w wifi_project.c
extern wifi_ap_record_t wifi_list[10]; 
extern uint16_t wifi_count;

// Deklaracje funkcji
bool wifi_connect_station(const char *ssid, const char *password);
void wifi_scan_networks(void);