#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_vfs_dev.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"

#include "fpga_driver.h"
#include "pmod_board.h"

#include "doom_main.h"

void user_task(void *arg)
{
    doom_main();

    vTaskDelete(NULL);
}

void app_main(void)
{
    gpio_set_direction(PMOD_LED_GREEN, GPIO_MODE_OUTPUT);
    gpio_set_direction(PMOD_LED_PINK, GPIO_MODE_OUTPUT);
    gpio_set_direction(PMOD_BUTTON, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PMOD_BUTTON, GPIO_PULLUP_ONLY);

    fpga_driver_config_t driver_config = 
    {
        .pinCsGpu = PMOD_FPGA_SPI_CS0,
        .pinCsIo = PMOD_FPGA_SPI_CS1,
        .pinSclk = PMOD_FPGA_SPI_SCLK,
        .pinD0 = PMOD_FPGA_SPI_D0,
        .pinD1 = PMOD_FPGA_SPI_D1,
        .pinD2 = PMOD_FPGA_SPI_D2,
        .pinD3 = PMOD_FPGA_SPI_D3
    };

    if (!fpga_driver_init(&driver_config))
        printf("failed to init driver\n");

    xTaskCreatePinnedToCore(user_task, "user_task", 4096, NULL, tskIDLE_PRIORITY+1, NULL, 1);
}