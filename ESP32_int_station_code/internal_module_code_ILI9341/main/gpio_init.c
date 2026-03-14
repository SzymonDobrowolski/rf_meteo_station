#include "driver/gpio.h"
#include "gpio_init.h"
#include "Config.h"
void gpio_init(void)
{
    gpio_input_enable(BUTTON_1); //BUTTON_1
    gpio_input_enable(BUTTON_2); //BUTTON_2
}
