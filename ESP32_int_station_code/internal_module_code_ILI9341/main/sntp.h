#pragma once
// Inicjalizuje SNTP i ustawia serwer czasu
void sntp_init_module(void);

// Czeka (blokująco) aż czas zostanie zsynchronizowany
// Zwraca 1 jeśli sukces, 0 jeśli timeout
int wait_for_time_sync(void);