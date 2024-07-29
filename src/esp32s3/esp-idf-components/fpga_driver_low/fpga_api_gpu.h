#pragma once

#include "fpga_qspi.h"

#define STATUS0_GET_HBLANK(status0) (((status0) & 0b00000100) >> 2)
#define STATUS0_GET_VBLANK(status0) (((status0) & 0b00000010) >> 1)

bool fpga_api_gpu_read_status0(fpga_qspi_t *qspi, uint8_t *result);

bool fpga_api_gpu_enable_output(fpga_qspi_t *qspi);
bool fpga_api_gpu_disable_output(fpga_qspi_t *qspi);

bool fpga_api_gpu_set_palette(fpga_qspi_t *qspi, uint8_t *palette);
bool fpga_api_gpu_get_palette(fpga_qspi_t *qspi, uint8_t *palette);

bool fpga_api_gpu_framebuffer_write(fpga_qspi_t *qspi, uint32_t startIdx, uint8_t *pixels, int pixelCount);
bool fpga_api_gpu_framebuffer_read(fpga_qspi_t *qspi, uint32_t startIdx, uint8_t *pixels, int pixelCount);

bool fpga_api_gpu_audio_buffer_read_status(fpga_qspi_t *qspi, uint16_t *status);
bool fpga_api_gpu_audio_buffer_write(fpga_qspi_t *qspi, uint8_t *samples, int sampleCount, uint16_t *status);

bool fpga_api_gpu_framebuffer_wait_for_vblank_write(fpga_qspi_t *qspi, uint32_t startIdx, uint8_t *pixels, int pixelCount);


