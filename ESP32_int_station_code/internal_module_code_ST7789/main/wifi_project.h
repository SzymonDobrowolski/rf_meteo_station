#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_wifi.h"

// Struktura do przechowywania danych o sieci (zgodnie z poprzednim krokiem)
typedef struct {
    char ssid[33];
    int8_t rssi;
    bool is_connected;
    bool auto_connect;
} wifi_scan_result_t;

// Deklaracje zmiennych globalnych, które zdefiniujesz w wifi_project.c
extern wifi_ap_record_t wifi_list[10]; 
extern uint16_t wifi_count;

// Deklaracje funkcji
bool wifi_connect_station(const char *ssid, const char *password);
void wifi_scan_networks(void); // Funkcja, którą będziesz wywoływać w menu
void wifi_save_credentials(const char *ssid, const char *password); // Zapis do NVS