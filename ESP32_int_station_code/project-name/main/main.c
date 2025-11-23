#include <stdio.h>
#include "gpio_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
void app_main(void)
{
    gpio_init();
}