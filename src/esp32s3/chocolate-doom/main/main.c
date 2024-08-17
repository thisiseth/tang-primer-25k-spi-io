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
#include "esp_vfs.h"
#include "esp_vfs_fat.h"

#include "fpga_driver.h"
#include "pmod_board.h"

#include "doom_main.h"

static const char TAG[] = "main";

const char flash_rw_base[] = "/flash";
static wl_handle_t flash_rw_wl_handle = WL_INVALID_HANDLE;

void user_task(void *arg)
{
    doom_main();

    if (esp_vfs_fat_spiflash_unmount_rw_wl(flash_rw_base, flash_rw_wl_handle) != ESP_OK)
        ESP_LOGE(TAG, "unable to unmount flash_rw");

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
    {
        ESP_LOGE(TAG, "failed to init driver");
        return;
    }

    ESP_LOGI(TAG, "waiting for fpga...");

    while (!fpga_driver_is_connected())
        vTaskDelay(1);

    ESP_LOGI(TAG, "fpga is responsive");

    const esp_vfs_fat_mount_config_t mount_config = 
    {
            .max_files = 4,
            .format_if_mount_failed = false,
            .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
    };

    esp_err_t err;

    err = esp_vfs_fat_spiflash_mount_rw_wl(flash_rw_base, "storage", &mount_config, &flash_rw_wl_handle);
    
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        return;
    }

    if (xTaskCreatePinnedToCore(user_task, "user_task", 16384, NULL, tskIDLE_PRIORITY+1, NULL, 1) != pdPASS)
        ESP_LOGE(TAG, "failed to start user task");
}