#include "driver/gpio.h"
#include "gpio_init.h"
void gpio_init(void)
{
    gpio_input_enable(GPIO_NUM_4); //BUTTON_1
    gpio_input_enable(GPIO_NUM_16); //BUTTON_2
}
