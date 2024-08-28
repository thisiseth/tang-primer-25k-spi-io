#include "quake_main.h"
#include "esp_log.h"
#include "fatfs_proxy.h"

static const char TAG[] = "quake";

void esp32_quake_main(int argc, char **argv, const char *basedir, const char *pakPath, uint32_t pakSize, const void *pakMmap);

void quake_main(const char *basedir, const char *pakPath, uint32_t pakSize, const void *pakMmap)
{
    const char *argv[] = { "quake" };
    int argc = sizeof(argv) / sizeof(argv[0]);
    
    fatfs_proxy_init(xTaskGetCurrentTaskHandle());

    ESP_LOGI(TAG, "starting quake...");
    esp32_quake_main(argc, (char**)argv, basedir, pakPath, pakSize, pakMmap);
    ESP_LOGI(TAG, "exiting quake :(");
}