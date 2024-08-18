#include "doom_main.h"
#include "esp_log.h"
#include <dirent.h>
#include <stdio.h>

static const char TAG[] = "doom";

static void list_files(const char *path) 
{
    struct dirent *entry;
    DIR *dir = opendir(path);

    if (dir == NULL) 
    {
        printf("opendir error\n");
        return;
    }

    printf("Listing files in directory: %s\n", path);
    while ((entry = readdir(dir)) != NULL) 
        printf("%s\n", entry->d_name);

    closedir(dir);
}

//from 'libesp-idf-chocolate-doom' i_main.c
int esp32_doom_main(int argc, const char **argv, const char *wadName, uint32_t wadSize, const void *wadMmap);

void doom_main(const char *wadName, uint32_t wadSize, const void *wadMmap)
{
    list_files("/flash/wad/");
    list_files("/flash/config/");
    list_files("/flash/tmp/");

    const char *argv[] = { "doom", "-mb", "6", "-iwad", wadName };
    int argc = sizeof(argv) / sizeof(argv[0]);

    ESP_LOGI(TAG, "starting doom...");
    esp32_doom_main(argc, argv, wadName, wadSize, wadMmap);
    ESP_LOGI(TAG, "exiting doom :(");
}