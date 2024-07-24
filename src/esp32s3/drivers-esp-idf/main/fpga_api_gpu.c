#include "fpga_api_gpu.h"
#include "esp_log.h"

static const char TAG[] = "fpga_api_gpu";

typedef enum 
{
    COMMAND_FRAMEBUFFER_CONTINUOUS_WRITE    = 0b10000010, //read phase only, read 3 bytes of first pixel idx, then continuously read pixel data in 1 byte blocks until master stops the transaction
    COMMAND_FRAMEBUFFER_CONTINUOUS_READ     = 0b11000010, //read+write, read 3 bytes of first pixel idx, then continuously write pixel data in 1 byte blocks until master stops the transaction
    COMMAND_FRAMEBUFFER_SET_PALETTE         = 0b10000011, //read phase only, 256*3 bytes of palette starting from [0]
    COMMAND_FRAMEBUFFER_GET_PALETTE         = 0b01000011, //write phase only, 256*3 bytes of palette starting from [0]
    COMMAND_READ_STATUS0                    = 0b01000000,
    COMMAND_DISABLE_OUTPUT                  = 0b00000000,
    COMMAND_ENABLE_OUTPUT                   = 0b00000001    
} FPGA_GPU_COMMAND;

bool IRAM_ATTR fpga_api_gpu_read_status0(fpga_qspi_t *qspi, uint8_t *result)
{
    return fpga_qspi_send_gpu(qspi, COMMAND_READ_STATUS0, 0, 0, NULL, 0, result, 1);
}

bool IRAM_ATTR fpga_api_gpu_enable_output(fpga_qspi_t *qspi)
{
    return fpga_qspi_send_gpu(qspi, COMMAND_ENABLE_OUTPUT, 0, 0, NULL, 0, NULL, 0);
}

bool IRAM_ATTR fpga_api_gpu_disable_output(fpga_qspi_t *qspi)
{
    return fpga_qspi_send_gpu(qspi, COMMAND_DISABLE_OUTPUT, 0, 0, NULL, 0, NULL, 0);
}

bool IRAM_ATTR fpga_api_gpu_set_palette(fpga_qspi_t *qspi, uint8_t *palette)
{
    return fpga_qspi_send_gpu(qspi, COMMAND_FRAMEBUFFER_SET_PALETTE, 0, 0, palette, 768, NULL, 0);
}

bool IRAM_ATTR fpga_api_gpu_get_palette(fpga_qspi_t *qspi, uint8_t *palette)
{
    return fpga_qspi_send_gpu(qspi, COMMAND_FRAMEBUFFER_GET_PALETTE, 0, 0, NULL, 0, palette, 768);
}

bool IRAM_ATTR fpga_api_gpu_framebuffer_write(fpga_qspi_t *qspi, uint32_t startIdx, uint8_t *pixels, int pixelCount)
{
    if (startIdx >= 76800)
    {
        ESP_LOGE(TAG, "u mad bro");
        return false;
    }

    while (pixelCount > 0)
    {
        int pixelsToWrite = pixelCount > SPI_MAX_TRANS_BYTES ? SPI_MAX_TRANS_BYTES : pixelCount;

        if (!fpga_qspi_send_gpu(qspi, COMMAND_FRAMEBUFFER_CONTINUOUS_WRITE, startIdx << 4, 24, pixels, pixelsToWrite, NULL, 0))
            return false;

        pixelCount -= pixelsToWrite;
        pixels += pixelsToWrite;
        startIdx += pixelsToWrite;
    }

    return true;
}

bool IRAM_ATTR fpga_api_gpu_framebuffer_read(fpga_qspi_t *qspi, uint32_t startIdx, uint8_t *pixels, int pixelCount)
{
    return false;
}

bool IRAM_ATTR fpga_api_gpu_framebuffer_wait_for_vblank_write(fpga_qspi_t *qspi, uint32_t startIdx, uint8_t *pixels, int pixelCount)
{
    WORD_ALIGNED_ATTR uint8_t buf[4] = { 0 };

    do //wait for vblank end, if in progress
    {
        if (!fpga_qspi_send_gpu(qspi, COMMAND_READ_STATUS0, 0, 0, NULL, 0, buf, 1))
            return false;
    }
    while (STATUS0_GET_VBLANK(buf[0]));

    do //then wait for vblank start
    {
        if (!fpga_qspi_send_gpu(qspi, COMMAND_READ_STATUS0, 0, 0, NULL, 0, buf, 1))
            return false;
    }
    while (!STATUS0_GET_VBLANK(buf[0]));

    return fpga_api_gpu_framebuffer_write(qspi, startIdx, pixels, pixelCount);
}