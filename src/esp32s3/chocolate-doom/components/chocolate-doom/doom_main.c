#include "doom_main.h"
#include "esp_log.h"
#include "doom_misc.h"

static const char TAG[] = "doom";

//from 'libesp-idf-chocolate-doom' i_main.c
int esp32_doom_main(int argc, const char **argv, const char *wadName, uint32_t wadSize, const void *wadMmap);

void doom_main(const char *wadName, uint32_t wadSize, const void *wadMmap)
{
    doom_list_files("/flash/config/");
    doom_list_files("/flash/tmp/");

    //doom_print_file("/flash/config/default.cfg");
    //doom_print_file("/flash/config/esp-idf-chocolate-doom.cfg");

    const char *argv[] = { "doom", "-novert", /*"-fpsdots",*/ "-iwad", wadName };
    int argc = sizeof(argv) / sizeof(argv[0]);

    ESP_LOGI(TAG, "starting doom...");
    esp32_doom_main(argc, argv, wadName, wadSize, wadMmap);
    ESP_LOGI(TAG, "exiting doom :(");
}