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
#include "fpga_api_gpu.h"

#define FPGA_SPI_CS0 41
#define FPGA_SPI_CS1 39
#define FPGA_SPI_SCLK 2

#define FPGA_SPI_D0 5
#define FPGA_SPI_D1 7
#define FPGA_SPI_D2 16
#define FPGA_SPI_D3 18

DMA_ATTR uint8_t sendBuf[320*240] = { 0 };
DMA_ATTR uint8_t receiveBuf[128] = { 0 };

#define PIXEL_IDX(x, y) ((x) + (y)*320)

void app_main(void)
{
    esp_err_t ret;

    printf("123\n");

    fpga_qspi_t qspi;

    printf("init: %d\n", fpga_qspi_init(&qspi, FPGA_SPI_CS0, FPGA_SPI_CS1, FPGA_SPI_SCLK, FPGA_SPI_D0, FPGA_SPI_D1, FPGA_SPI_D2, FPGA_SPI_D3));

    printf("paletted palette\n");

    for (int color = 0; color < 256; ++color)
    {
        sendBuf[3*color] = 30; //R
        sendBuf[3*color+1] = color/2; //G
        sendBuf[3*color+2] = color; //B
    }

    sendBuf[0] = 128;
    sendBuf[1] = 0;
    sendBuf[2] = 0;

    sendBuf[765] = 0;
    sendBuf[766] = 128;
    sendBuf[767] = 0;

    printf("set palette: %d\n", fpga_api_gpu_set_palette(&qspi, sendBuf));

    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 320; ++x)
        {
            if (x == 0 || x == 319 || y == 0 || y == 239)
            {
                sendBuf[PIXEL_IDX(x, y)] = 0;
                continue;
            }

            int tileX = x / 20; //16*16 tiles
            int tileY = y / 15;

            sendBuf[PIXEL_IDX(x, y)] = tileY * 16 + tileX;
        }

    printf("tiled framebuffer\n");

    printf("write frame: %d\n", fpga_api_gpu_framebuffer_write(&qspi, 0, sendBuf, 76800));

    //int pipixel = 0;
    //uint8_t pipixel_tmp;

    for (;;)
    {
        //printf("send: %d\n", fpga_qspi_send_gpu(&qspi, 0b01000000, 0, 0, NULL, 0, receiveBuf, 2));
        //printf("send: %d\n", fpga_qspi_send_gpu(&qspi, 0b00000100, 0, 0, NULL, 0, receiveBuf, 1));
        //printf("send: %d\n", fpga_qspi_send_gpu(&qspi, 0b01001000, 0, 0, NULL, 0, receiveBuf, 1));
        //printf("send: %d\n", fpga_api_gpu_read_status0(&qspi, receiveBuf));
        //printf("recv: 0x%02X%02X\n", receiveBuf[0], receiveBuf[1]);

        //fpga_api_gpu_enable_output(&qspi);

        //for (int color = 0; color < 768; ++color)
        //    sendBuf[color] += 1;
    
        //printf("set palette: %d\n", fpga_api_gpu_set_palette(&qspi, sendBuf));
        
        //fpga_api_gpu_disable_output(&qspi);

        // pipixel_tmp = sendBuf[pipixel];
        // sendBuf[pipixel] = 255;

        // fpga_api_gpu_framebuffer_write(&qspi, 0, sendBuf, 76800);

        // sendBuf[pipixel] = pipixel_tmp;

        // ++pipixel;
        // pipixel = pipixel < (320*240) ? pipixel : 0;

        //vTaskDelay(1);

        for (int i = 0; i < (320*240); ++i)
            sendBuf[i] = ~sendBuf[i];

        fpga_api_gpu_framebuffer_wait_for_vblank_write(&qspi, 0, sendBuf, 76800);

        //vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}