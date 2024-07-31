#pragma once

#include "fpga_qspi.h"

#define FPGA_API_GPU_STATUS0_GET_HBLANK(status0) (((status0) & 0b00000100) >> 2)
#define FPGA_API_GPU_STATUS0_GET_VBLANK(status0) (((status0) & 0b00000010) >> 1)

#define FPGA_API_GPU_AUDIO_BUFFER_STATUS_GET_ALMOST_FULL_OCCURRED(status)   (!!((status) & 0b1000000000000000))
#define FPGA_API_GPU_AUDIO_BUFFER_STATUS_GET_FULL_OCCURRED(status)          (!!((status) & 0b0100000000000000))    
#define FPGA_API_GPU_AUDIO_BUFFER_STATUS_GET_CURRENT_ALMOST_FULL(status)    (!!((status) & 0b0010000000000000))
#define FPGA_API_GPU_AUDIO_BUFFER_STATUS_GET_CURRENT_FULL(status)           (!!((status) & 0b0001000000000000))
#define FPGA_API_GPU_AUDIO_BUFFER_STATUS_GET_WNUM(status)                   ((status) & 0xFFF)

bool fpga_api_gpu_read_status0(fpga_qspi_t *qspi, uint8_t *result);
bool fpga_api_gpu_read_magic_number(fpga_qspi_t *qspi, bool *result);

bool fpga_api_gpu_enable_output(fpga_qspi_t *qspi);
bool fpga_api_gpu_disable_output(fpga_qspi_t *qspi);

bool fpga_api_gpu_set_palette(fpga_qspi_t *qspi, uint8_t *palette);
bool fpga_api_gpu_get_palette(fpga_qspi_t *qspi, uint8_t *palette);

bool fpga_api_gpu_framebuffer_write(fpga_qspi_t *qspi, uint32_t startIdx, uint8_t *pixels, int pixelCount);
bool fpga_api_gpu_framebuffer_read(fpga_qspi_t *qspi, uint32_t startIdx, uint8_t *pixels, int pixelCount);

bool fpga_api_gpu_audio_buffer_read_status(fpga_qspi_t *qspi, uint16_t *status);
bool fpga_api_gpu_audio_buffer_write(fpga_qspi_t *qspi, uint8_t *samples, int sampleCount, uint16_t *status);


