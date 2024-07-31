#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gptimer.h"

#include "fpga_driver.h"
#include "fpga_api_gpu.h"
#include "fpga_api_io.h"
#include "fpga_qspi.h"

static const char TAG[] = "fpga_driver";

#ifdef NDEBUG
#define FPGA_DRIVER_ERROR_CHECK(f) do { bool _ret = (f); (void)sizeof(_ret); } while(0)
#else
#define FPGA_DRIVER_ERROR_CHECK(f) do { if (!(f)) ESP_LOGE(TAG, "fpga api call returned false: %s", #f); } while(0)
#endif

#define SWAP_FOUR_BYTES(data)   \
( (((data) >> 24) & 0x000000FF) | (((data) >>  8) & 0x0000FF00) | \
  (((data) <<  8) & 0x00FF0000) | (((data) << 24) & 0xFF000000) ) 

#define FPGA_DRIVER_MAIN_TASK_PRIORITY      12
#define FPGA_DRIVER_MAIN_TASK_STACKSIZE     4 * 1024
#define FPGA_DRIVER_MAIN_TASK_NAME          "fpga_drv_main"
#define FPGA_DRIVER_MAIN_TASK_PINNED_CORE   0

#define FPGA_DRIVER_MAIN_TASK_TICK_US       500

#define FPGA_DRIVER_AUDIO_TASK_PRIORITY     10
#define FPGA_DRIVER_AUDIO_TASK_STACKSIZE    4 * 1024
#define FPGA_DRIVER_AUDIO_TASK_NAME         "fpga_drv_audio"
#define FPGA_DRIVER_AUDIO_TASK_PINNED_CORE  0

static volatile bool init = false;

static fpga_qspi_t qspi;
static gptimer_handle_t driver_timer = NULL;
static TaskHandle_t driver_main_task = NULL;
static TaskHandle_t driver_audio_task = NULL;

static portMUX_TYPE driver_spinlock = portMUX_INITIALIZER_UNLOCKED;

static volatile uint32_t fpga_connected = 0;

static volatile int32_t framebuffer_idx_to_present = -1;
static volatile bool present_in_progress = false;

static DMA_ATTR uint8_t palette0[FPGA_DRIVER_PALETTE_SIZE_BYTES];
static DMA_ATTR uint8_t palette1[FPGA_DRIVER_PALETTE_SIZE_BYTES];
static DMA_ATTR uint8_t framebuffer0[FPGA_DRIVER_FRAMEBUFFER_SIZE_BYTES];
static DMA_ATTR uint8_t framebuffer1[FPGA_DRIVER_FRAMEBUFFER_SIZE_BYTES];

static volatile bool audio_send_in_progress = false;

static volatile uint32_t audio_hdmi_fifo_wnum = 0;

static DMA_ATTR uint8_t next_audio_buffer[FPGA_DRIVER_AUDIO_BUFFER_WRITE_MAX_BYTES];
static volatile uint32_t next_audio_buffer_ready_samples = 0;

static volatile fpga_driver_audio_requested_cb_t audio_requested_callback = NULL;

static bool driver_timer_tick(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx);
static void driver_task_function_main(void *arg);
static void driver_task_function_audio(void *arg);

bool fpga_driver_init(fpga_driver_config_t *config)
{
    ESP_LOGI(TAG, "fpga driver init starting");

    if (init)
        return false;

    if (!fpga_qspi_init(&qspi, config->pinCsGpu, config->pinCsIo, config->pinSclk, config->pinD0, config->pinD1, config->pinD2, config->pinD3))
        return false;

    if (xTaskCreatePinnedToCore(driver_task_function_main, 
                                FPGA_DRIVER_MAIN_TASK_NAME, 
                                FPGA_DRIVER_MAIN_TASK_STACKSIZE, 
                                NULL, 
                                FPGA_DRIVER_MAIN_TASK_PRIORITY, 
                                &driver_main_task, 
                                FPGA_DRIVER_MAIN_TASK_PINNED_CORE) != pdPASS)
        return false;

    gptimer_config_t timer_config = 
    {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, //tick = 1us
        .flags.intr_shared = 0
    };

    if (gptimer_new_timer(&timer_config, &driver_timer) != ESP_OK)
        return false;

    gptimer_alarm_config_t alarm_config = 
    {
        .reload_count = 0, // counter will reload with 0 on alarm event
        .alarm_count = FPGA_DRIVER_MAIN_TASK_TICK_US,
        .flags.auto_reload_on_alarm = true, // enable auto-reload
    };

    if (gptimer_set_alarm_action(driver_timer, &alarm_config) != ESP_OK)
        return false;

    gptimer_event_callbacks_t callbacks =  { .on_alarm = driver_timer_tick };

    if (gptimer_register_event_callbacks(driver_timer, &callbacks, NULL) != ESP_OK)
        return false;

    if (gptimer_enable(driver_timer) != ESP_OK)
        return false;

    if (gptimer_start(driver_timer) != ESP_OK)
        return false;
        
    if (xTaskCreatePinnedToCore(driver_task_function_audio, 
                                FPGA_DRIVER_AUDIO_TASK_NAME, 
                                FPGA_DRIVER_AUDIO_TASK_STACKSIZE, 
                                NULL, 
                                FPGA_DRIVER_AUDIO_TASK_PRIORITY, 
                                &driver_audio_task, 
                                FPGA_DRIVER_AUDIO_TASK_PINNED_CORE) != pdPASS)
        return false;

    ESP_LOGI(TAG, "fpga driver init successful");
    return init = true;
}

bool fpga_driver_is_connected(void)
{
    taskENTER_CRITICAL(&driver_spinlock);

    bool connected = fpga_connected;

    taskEXIT_CRITICAL(&driver_spinlock);

    return connected;
}

void fpga_driver_get_framebuffer(uint8_t **palette, uint8_t **framebuffer)
{
    taskENTER_CRITICAL(&driver_spinlock);

    *palette = framebuffer_idx_to_present == 0 ? palette1 : palette0;
    *framebuffer = framebuffer_idx_to_present == 0 ? framebuffer1 : framebuffer0;

    taskEXIT_CRITICAL(&driver_spinlock);
}

void fpga_driver_present_frame(uint8_t **palette, uint8_t **framebuffer, fpga_driver_vsync_mode_t vsync)
{
    int framebuffer_idx;

    if (*palette == palette0 && *framebuffer == framebuffer0)
        framebuffer_idx = 0;
    else if (*palette == palette1 && *framebuffer == framebuffer1)
        framebuffer_idx = 1;
    else
    {
        ESP_LOGE(TAG, "present frame: provided framebuffer pointers do not match allocated buffers");
        return;
    }

    for (;;)
    {
        bool done = false;

        taskENTER_CRITICAL(&driver_spinlock);

        if (vsync == FPGA_DRIVER_VSYNC_DONT_WAIT_OVERWRITE_PREVIOUS)
        {
            if (!present_in_progress)
            {
                framebuffer_idx_to_present = framebuffer_idx;
                done = true;
            }
        }
        else if (vsync == FPGA_DRIVER_VSYNC_WAIT_IF_PREVIOUS_NOT_PRESENTED)
        {
            if (framebuffer_idx_to_present < 0 && !present_in_progress)
            {
                framebuffer_idx_to_present = framebuffer_idx;
                done = true;
            }
        }

        taskEXIT_CRITICAL(&driver_spinlock);

        if (done)
            break;

        taskYIELD();
    }

    *palette = framebuffer_idx ? palette0 : palette1;
    *framebuffer = framebuffer_idx ? framebuffer0 : framebuffer1;
}

void fpga_driver_register_audio_requested_cb(fpga_driver_audio_requested_cb_t callback)
{
    taskENTER_CRITICAL(&driver_spinlock);

    audio_requested_callback = callback;

    taskEXIT_CRITICAL(&driver_spinlock);
}

static bool IRAM_ATTR driver_timer_tick(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    vTaskNotifyGiveFromISR(driver_main_task, &xHigherPriorityTaskWoken);

    return xHigherPriorityTaskWoken == pdTRUE;
}

static void IRAM_ATTR driver_task_function_main(void *arg)
{
    ESP_LOGI(TAG, "fpga driver main task started");

    uint8_t status0;
    uint16_t audio_buffer_status;

    bool vblank = false;

    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (!fpga_connected)
        {
            bool connected = false;

            FPGA_DRIVER_ERROR_CHECK(fpga_api_gpu_read_magic_number(&qspi, &connected));

            taskENTER_CRITICAL(&driver_spinlock);

            fpga_connected = connected;

            taskEXIT_CRITICAL(&driver_spinlock);
            continue;
        }

        FPGA_DRIVER_ERROR_CHECK(fpga_api_gpu_read_status0(&qspi, &status0));

        if (!vblank && FPGA_API_GPU_STATUS0_GET_VBLANK(status0))
        {   //at most one tick after the vblank started - only chance to update the frame
            taskENTER_CRITICAL(&driver_spinlock);

            int buffer_to_present = framebuffer_idx_to_present;
            present_in_progress = true;

            taskEXIT_CRITICAL(&driver_spinlock);

            if (buffer_to_present >= 0)
            {   
                FPGA_DRIVER_ERROR_CHECK(fpga_api_gpu_set_palette(&qspi, buffer_to_present ? palette1 : palette0));
                FPGA_DRIVER_ERROR_CHECK(fpga_api_gpu_framebuffer_write(&qspi, 0, buffer_to_present ? framebuffer0 : framebuffer1, FPGA_DRIVER_FRAMEBUFFER_SIZE_BYTES));
                
                taskENTER_CRITICAL(&driver_spinlock);

                framebuffer_idx_to_present = -1;
                present_in_progress = false;

                taskEXIT_CRITICAL(&driver_spinlock);
            }
        }

        vblank = FPGA_API_GPU_STATUS0_GET_VBLANK(status0);

        taskENTER_CRITICAL(&driver_spinlock);

        if (next_audio_buffer_ready_samples > 0)
            audio_send_in_progress = true;
        
        taskEXIT_CRITICAL(&driver_spinlock);

        if (audio_send_in_progress)
        {
            FPGA_DRIVER_ERROR_CHECK(fpga_api_gpu_audio_buffer_write(&qspi, next_audio_buffer, next_audio_buffer_ready_samples, &audio_buffer_status));
            
            taskENTER_CRITICAL(&driver_spinlock);

            next_audio_buffer_ready_samples = 0;
            audio_hdmi_fifo_wnum = FPGA_API_GPU_AUDIO_BUFFER_STATUS_GET_WNUM(audio_buffer_status);
            audio_send_in_progress = false;

            taskEXIT_CRITICAL(&driver_spinlock);
        }
        else
        {
            FPGA_DRIVER_ERROR_CHECK(fpga_api_gpu_audio_buffer_read_status(&qspi, &audio_buffer_status));

            taskENTER_CRITICAL(&driver_spinlock);

            audio_hdmi_fifo_wnum = FPGA_API_GPU_AUDIO_BUFFER_STATUS_GET_WNUM(audio_buffer_status);

            taskEXIT_CRITICAL(&driver_spinlock);
        }

        if (audio_hdmi_fifo_wnum < (FPGA_DRIVER_AUDIO_HDMI_FIFO_SAMPLES - FPGA_DRIVER_AUDIO_BUFFER_WRITE_MAX_SAMPLES - 10))
            xTaskNotifyGive(driver_audio_task);
    }

    vTaskDelete(NULL);
}

static void IRAM_ATTR driver_task_function_audio(void *arg)
{    
    ESP_LOGI(TAG, "fpga driver audio task started");

    WORD_ALIGNED_ATTR uint32_t generatedSamples[FPGA_DRIVER_AUDIO_BUFFER_WRITE_MAX_SAMPLES];

    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        taskENTER_CRITICAL(&driver_spinlock);

        fpga_driver_audio_requested_cb_t callback = audio_requested_callback;
        uint32_t bufferedSamples = next_audio_buffer_ready_samples;
        
        taskEXIT_CRITICAL(&driver_spinlock);

        if (bufferedSamples != 0 || callback == NULL)
            continue;
        
        int generatedSampleCount;

        callback(generatedSamples, &generatedSampleCount, FPGA_DRIVER_AUDIO_BUFFER_WRITE_MAX_SAMPLES);

        if (generatedSampleCount == 0)
            continue;

        if (generatedSampleCount < 0 || generatedSampleCount > FPGA_DRIVER_AUDIO_BUFFER_WRITE_MAX_SAMPLES)
        {
            ESP_LOGE(TAG, "audio requested callback returned sampleCount %d out of range", generatedSampleCount);
            continue;
        }

        for (int i = 0; i < generatedSampleCount; ++i)
            ((uint32_t*)next_audio_buffer)[i] = SWAP_FOUR_BYTES(generatedSamples[i]); //fpga expects MSB-first bigendian 32 bit sample
                                                                                      //just have to remember which one is left and right

        taskENTER_CRITICAL(&driver_spinlock);

        next_audio_buffer_ready_samples = generatedSampleCount;
        
        taskEXIT_CRITICAL(&driver_spinlock);
    }
}