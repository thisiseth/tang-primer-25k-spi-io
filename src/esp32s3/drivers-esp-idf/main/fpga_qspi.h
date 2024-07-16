#include <stdint.h>
#include "driver/spi_master.h"

typedef struct 
{
    spi_device_handle_t spi_gpu;
    spi_device_handle_t spi_io;
} fpga_qspi_t;

bool fpga_qspi_init(fpga_qspi_t *qspi, int pinCsGpu, int pinCsIo, int pinSclk, int pinD0, int pinD1, int pinD2, int pinD3);

bool fpga_qspi_send_gpu(fpga_qspi_t *qspi, uint8_t command, uint64_t address, int addressLengthBits, uint8_t *sendBuf, int sendCount, uint8_t *receiveBuf, int receiveCount);
bool fpga_qspi_send_io(fpga_qspi_t *qspi, uint8_t command, uint64_t address, int addressLengthBits, uint8_t *sendBuf, int sendCount, uint8_t *receiveBuf, int receiveCount);