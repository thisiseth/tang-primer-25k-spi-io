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
        printf("opendir error: %s\n", path);
        return;
    }

    printf("Listing files in directory: %s\n", path);
    while ((entry = readdir(dir)) != NULL) 
        printf("%s\n", entry->d_name);

    closedir(dir);
}

static void print_file(const char *path)
{
    FILE *file = fopen(path, "r");

    if (file == NULL) 
    {
        printf("can't open %s\n", path);
        return ;
    }

    printf("contents of %s:\n\n", path);

    char buffer[1024];  // Buffer to hold chunks of the file content
    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        printf("%s", buffer);  // Print each line of the file
    }

    fclose(file);

    printf("\nend of %s\n", path);
}

//from 'libesp-idf-chocolate-doom' i_main.c
int esp32_doom_main(int argc, const char **argv, const char *wadName, uint32_t wadSize, const void *wadMmap);

void doom_main(const char *wadName, uint32_t wadSize, const void *wadMmap)
{
    list_files("/flash/config/");
    list_files("/flash/tmp/");

    print_file("/flash/config/default.cfg");
    print_file("/flash/config/esp-idf-chocolate-doom.cfg");

    const char *argv[] = { "doom", "-novert", /*"-fpsdots",*/ "-iwad", wadName };
    int argc = sizeof(argv) / sizeof(argv[0]);

    ESP_LOGI(TAG, "starting doom...");
    esp32_doom_main(argc, argv, wadName, wadSize, wadMmap);
    ESP_LOGI(TAG, "exiting doom :(");
}