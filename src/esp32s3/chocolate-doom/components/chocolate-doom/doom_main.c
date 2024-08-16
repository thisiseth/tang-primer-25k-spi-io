#include "doom_main.h"
#include "esp_log.h"

static const char TAG[] = "doom";

int esp32_doom_main(int argc, char **argv);

#include <dirent.h>
#include <stdio.h>

void list_files(const char *path) 
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
    {
        // Print the name of each file or directory
        printf("%s\n", entry->d_name);
    }

    closedir(dir);
}

void doom_main(void)
{
    list_files("/flash/wad/");

    char *argv[] = { "doom", "-mb", "1", "-iwad", "/flash/wad/doom1.wad" };
    int argc = sizeof(argv) / sizeof(argv[0]);

    ESP_LOGI(TAG, "starting doom...");
    esp32_doom_main(argc, argv);
    ESP_LOGI(TAG, "exiting doom :(");
}