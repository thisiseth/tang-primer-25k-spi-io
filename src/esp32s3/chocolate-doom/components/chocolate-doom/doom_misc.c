#include "doom_misc.h"
#include <dirent.h>
#include <stdio.h>

void doom_list_files(const char *path) 
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

void doom_print_file(const char *path)
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