#include "fpga_qspi.h"
#include "esp_log.h"
#include "esp_err.h"

static const char TAG[] = "fpga_qspi";

#define SPI_DEVICE SPI2_HOST
#define SPI_FREQ SPI_MASTER_FREQ_80M
#define SPI_MAX_TRANS_BYTES 4092*4

#define SPI_INPUT_DELAY_NS 18

#define FPGA_QSPI_COMMAND_BITS 8
#define FPGA_QSPI_READ_DUMMY_CYCLES 2

bool fpga_qspi_init(fpga_qspi_t *qspi, int pinCsGpu, int pinCsIo, int pinSclk, int pinD0, int pinD1, int pinD2, int pinD3)
{
    spi_device_handle_t spiGpu = NULL, spiIo = NULL;

    spi_bus_config_t busCfg = 
    {
        .sclk_io_num = pinSclk,
        .data0_io_num = pinD0,
        .data1_io_num = pinD1,
        .data2_io_num = pinD2,
        .data3_io_num = pinD3,
        .max_transfer_sz = SPI_MAX_TRANS_BYTES,
    };
    
    if (spi_bus_initialize(SPI_DEVICE, &busCfg, SPI_DMA_CH_AUTO) != ESP_OK)
        goto cleanup;
    
    spi_device_interface_config_t gpuCfg = 
    {
        .clock_speed_hz = SPI_FREQ,
        .mode = 0,
        .spics_io_num = pinCsGpu,
        .queue_size = 2,
        .flags = SPI_DEVICE_HALFDUPLEX /*| SPI_DEVICE_NO_DUMMY*/,
        .input_delay_ns = SPI_INPUT_DELAY_NS,
        .command_bits = FPGA_QSPI_COMMAND_BITS
    };

    if (spi_bus_add_device(SPI_DEVICE, &gpuCfg, &spiGpu) != ESP_OK)
        goto cleanup;

    spi_device_interface_config_t ioCfg = 
    {
        .clock_speed_hz = SPI_FREQ,
        .mode = 0,
        .spics_io_num = pinCsIo,
        .queue_size = 2,
        .flags = SPI_DEVICE_HALFDUPLEX /*| SPI_DEVICE_NO_DUMMY*/,
        .input_delay_ns = SPI_INPUT_DELAY_NS,
        .command_bits = FPGA_QSPI_COMMAND_BITS
    };

    if (spi_bus_add_device(SPI_DEVICE, &ioCfg, &spiIo) != ESP_OK)
        goto cleanup;

    qspi->spi_gpu = spiGpu;
    qspi->spi_io = spiIo;

    int actualFreq;

    spi_device_get_actual_freq(spiGpu, &actualFreq);
    ESP_LOGI(TAG, "fpga spi gpu actual freq: %d", actualFreq);

    spi_device_get_actual_freq(spiIo, &actualFreq);
    ESP_LOGI(TAG, "fpga spi io actual freq: %d", actualFreq);

    return true;

    //release spi if error
cleanup:
    if (spiGpu != NULL)
        spi_bus_remove_device(spiGpu);
    if (spiIo != NULL)
        spi_bus_remove_device(spiIo);
    
    spi_bus_free(SPI_DEVICE);

    return false;
}

static inline IRAM_ATTR bool fpga_qspi_send(spi_device_handle_t device, uint8_t command, uint64_t address, int addressLengthBits, uint8_t *sendBuf, int sendCount, uint8_t *receiveBuf, int receiveCount)
{
    esp_err_t err = ~ESP_OK;
    spi_transaction_ext_t *lastTrans = NULL;

    if (spi_device_acquire_bus(device, portMAX_DELAY) != ESP_OK)
        return false;

    spi_transaction_ext_t transTx = 
    {
        .base = 
        {
            .flags = SPI_TRANS_MODE_QIO | 
                     SPI_TRANS_MULTILINE_CMD | 
                     SPI_TRANS_MULTILINE_ADDR | 
                     SPI_TRANS_VARIABLE_ADDR | 
                     (receiveCount > 0 ? SPI_TRANS_CS_KEEP_ACTIVE : 0),
            .cmd = command,
            .addr = address,
            .length = sendCount * 8,
            .tx_buffer = sendBuf,
            .rxlength = 0,
            .rx_buffer = NULL
        },
        .address_bits = addressLengthBits
    };

    spi_transaction_ext_t transRx = 
    {
        .base = 
        {
            .flags = SPI_TRANS_MODE_QIO | 
                     SPI_TRANS_MULTILINE_CMD | 
                     SPI_TRANS_MULTILINE_ADDR | 
                     SPI_TRANS_VARIABLE_ADDR | 
                     SPI_TRANS_VARIABLE_CMD |
                     SPI_TRANS_VARIABLE_DUMMY,
            .cmd = command,
            .addr = address,
            .length = 0,
            .tx_buffer = NULL,
            .rxlength = receiveCount * 8,
            .rx_buffer = receiveBuf
        },
        .address_bits = sendCount > 0 ? 0 : addressLengthBits,
        .command_bits = sendCount > 0 ? 0 : FPGA_QSPI_COMMAND_BITS,
        .dummy_bits = FPGA_QSPI_READ_DUMMY_CYCLES //its cycles, not 'bits'
    };

    if (sendCount > 0 || (sendCount == 0 && receiveCount == 0))
    {
        err = spi_device_queue_trans(device, &transTx.base, 0);

        if (err != ESP_OK)
            goto cleanup;
        
        lastTrans = &transTx;
    }

    if (receiveCount > 0)
    {
        err = spi_device_queue_trans(device, &transRx.base, 0);
        
        if (err != ESP_OK)
            goto cleanup;

        lastTrans = &transRx;
    }

    spi_transaction_t *completedTrans = NULL;

    while (completedTrans != &lastTrans->base)
    {
        err = spi_device_get_trans_result(device, &completedTrans, portMAX_DELAY);

        if (err != ESP_OK)
            goto cleanup;
    }

cleanup:
    spi_device_release_bus(device);

    return err == ESP_OK;
}

bool IRAM_ATTR fpga_qspi_send_gpu(fpga_qspi_t *qspi, uint8_t command, uint64_t address, int addressLengthBits, uint8_t *sendBuf, int sendCount, uint8_t *receiveBuf, int receiveCount)
{
    return fpga_qspi_send(qspi->spi_gpu, command, address, addressLengthBits, sendBuf, sendCount, receiveBuf, receiveCount);
}

bool IRAM_ATTR fpga_qspi_send_io(fpga_qspi_t *qspi, uint8_t command, uint64_t address, int addressLengthBits, uint8_t *sendBuf, int sendCount, uint8_t *receiveBuf, int receiveCount)
{
    return fpga_qspi_send(qspi->spi_io, command, address, addressLengthBits, sendBuf, sendCount, receiveBuf, receiveCount);
}