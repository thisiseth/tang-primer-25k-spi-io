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
#include "pmod_esp32s3.h"

#include "quake_main.h"

static const char TAG[] = "main";

const char flash_rw_base[] = "/flash";
static wl_handle_t flash_rw_wl_handle = WL_INVALID_HANDLE;

static esp_partition_mmap_handle_t pak_mmap_handle;
static const void *pak_mmap;

static void abort_with_error_led(void)
{
#ifndef PMOD_OCTAL_SPI_IN_USE
    pmod_esp32s3_led_set_green(false);
    pmod_esp32s3_led_set_pink(true);
#else
    pmod_esp32s3_led_set_rgb(12, 0, 0);
#endif

    abort();
}

static void loading_led(void)
{
#ifndef PMOD_OCTAL_SPI_IN_USE
    pmod_esp32s3_led_set_green(true);
    pmod_esp32s3_led_set_pink(true);
#else
    pmod_esp32s3_led_set_rgb(4, 2, 0);
#endif
}

static void running_led(void)
{
#ifndef PMOD_OCTAL_SPI_IN_USE
    pmod_esp32s3_led_set_green(true);
    pmod_esp32s3_led_set_pink(false);
#else
    pmod_esp32s3_led_set_rgb(0, 2, 0);
#endif
}
    
void user_task(void *arg)
{
    running_led();

    quake_main("/id1/"ESP32_QUAKE_PAK_NAME, ESP32_QUAKE_PAK_SIZE, pak_mmap);

    esp_partition_munmap(pak_mmap_handle);

    if (esp_vfs_fat_spiflash_unmount_rw_wl(flash_rw_base, flash_rw_wl_handle) != ESP_OK)
        ESP_LOGE(TAG, "unable to unmount flash_rw");

    vTaskDelete(NULL);
}

void app_main(void)
{
    if (!pmod_esp32s3_init())
    {
        ESP_LOGE(TAG, "pmod board init failed");
        return;
    }

    loading_led();

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
        abort_with_error_led();
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
        abort_with_error_led();
    }

    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_UNDEFINED, "pak");

    if (partition == NULL) 
    {
        ESP_LOGE(TAG, "Failed to find pak partition");
        abort_with_error_led();
    }

    if (esp_partition_mmap(partition, 0, ESP32_QUAKE_PAK_SIZE, ESP_PARTITION_MMAP_DATA, &pak_mmap, &pak_mmap_handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mmap pak partition");
        abort_with_error_led();
    }

    if (xTaskCreatePinnedToCore(user_task, "user_task", 100000, NULL, tskIDLE_PRIORITY+1, NULL, 1) != pdPASS)
    {
        ESP_LOGE(TAG, "failed to start user task");
        abort_with_error_led();
    }

    for (;;)
    {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "free heap: %lu", esp_get_free_heap_size());
    }
}