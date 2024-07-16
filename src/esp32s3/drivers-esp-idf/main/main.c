#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_vfs_dev.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_err.h"

#include "fpga_qspi.h"

#define FPGA_SPI_CS0 41
#define FPGA_SPI_CS1 39
#define FPGA_SPI_SCLK 2

#define FPGA_SPI_D0 5
#define FPGA_SPI_D1 7
#define FPGA_SPI_D2 16
#define FPGA_SPI_D3 18

void app_main(void)
{
    esp_err_t ret;

    printf("123\n");

    fpga_qspi_t qspi;

    printf("init: %d\n", fpga_qspi_init(&qspi, FPGA_SPI_CS0, FPGA_SPI_CS1, FPGA_SPI_SCLK, FPGA_SPI_D0, FPGA_SPI_D1, FPGA_SPI_D2, FPGA_SPI_D3));

    uint8_t receiveBuf[128] = { 0 };

    for (;;)
    {
        printf("send: %d\n", fpga_qspi_send_gpu(&qspi, 0b01000000, 0, 0, NULL, 0, receiveBuf, 2));
        //printf("send: %d\n", fpga_qspi_send_gpu(&qspi, 0b00000100, 0, 0, NULL, 0, receiveBuf, 1));
        //printf("send: %d\n", fpga_qspi_send_gpu(&qspi, 0b01001000, 0, 0, NULL, 0, receiveBuf, 1));
        printf("recv: 0x%02X%02X\n", receiveBuf[0], receiveBuf[1]);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}