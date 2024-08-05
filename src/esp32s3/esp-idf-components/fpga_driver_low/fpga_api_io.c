#include "fpga_api_gpu.h"
#include "esp_log.h"

//static const char TAG[] = "fpga_api_io";

typedef enum 
{
    COMMAND_USB_HID_GET_STATUS = 0b01010000
} FPGA_IO_COMMAND;

bool IRAM_ATTR fpga_api_io_hid_get_status(fpga_qspi_t *qspi, uint8_t *result)
{
    return fpga_qspi_send_io(qspi, COMMAND_USB_HID_GET_STATUS, 0, 0, NULL, 0, result, 6*4);
}