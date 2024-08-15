#include "doom_main.h"

int esp32_doom_main(int argc, char **argv);

void doom_main(void)
{
    char *argv[] = { "" };

    esp32_doom_main(0, argv);
}