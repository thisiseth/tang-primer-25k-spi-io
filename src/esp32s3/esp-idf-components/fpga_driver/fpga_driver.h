#pragma once

#include <stdint.h>

typedef struct 
{
    int pinCsGpu;
    int pinCsIo;
    int pinSclk;
    int pinD0;
    int pinD1;
    int pinD2;
    int pinD3;
} fpga_driver_config_t;

bool fpga_driver_init(fpga_driver_config_t *config);

bool fpga_driver_is_connected(void);


