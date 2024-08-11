#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gptimer.h"

#include "fpga_driver.h"
#include "fpga_api_gpu.h"
#include "fpga_api_io.h"
#include "fpga_qspi.h"
#include <string.h>

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

#define FPGA_DRIVER_AUDIO_TASK_PRIORITY     11
#define FPGA_DRIVER_AUDIO_TASK_STACKSIZE    4 * 1024
#define FPGA_DRIVER_AUDIO_TASK_NAME         "fpga_drv_audio"
#define FPGA_DRIVER_AUDIO_TASK_PINNED_CORE  0

#define FPGA_DRIVER_HID_TASK_PRIORITY       10
#define FPGA_DRIVER_HID_TASK_STACKSIZE      4 * 1024
#define FPGA_DRIVER_HID_TASK_NAME           "fpga_drv_hid"
#define FPGA_DRIVER_HID_TASK_PINNED_CORE    0

#define FPGA_DRIVER_HID_KEY_IS_ERROR(code)  ((code) >= 1 && (code) <= 3)

static bool init = false;

static fpga_qspi_t qspi;
static gptimer_handle_t driver_timer = NULL;
static TaskHandle_t driver_main_task = NULL;
static TaskHandle_t driver_audio_task = NULL;
static TaskHandle_t driver_hid_task = NULL;

static portMUX_TYPE driver_spinlock = portMUX_INITIALIZER_UNLOCKED;

static uint32_t fpga_connected = 0;

//video

static int32_t framebuffer_idx_to_present = -1;
static bool present_in_progress = false;

static DMA_ATTR uint8_t palette0[FPGA_DRIVER_PALETTE_SIZE_BYTES];
static DMA_ATTR uint8_t palette1[FPGA_DRIVER_PALETTE_SIZE_BYTES];
static DMA_ATTR uint8_t framebuffer0[FPGA_DRIVER_FRAMEBUFFER_SIZE_BYTES];
static DMA_ATTR uint8_t framebuffer1[FPGA_DRIVER_FRAMEBUFFER_SIZE_BYTES];

//audio

static bool audio_send_in_progress = false;

static uint32_t audio_hdmi_fifo_wnum = 0;

static DMA_ATTR uint8_t next_audio_buffer[FPGA_DRIVER_AUDIO_BUFFER_WRITE_MAX_BYTES];
static uint32_t next_audio_buffer_ready_samples = 0;

static fpga_driver_audio_requested_cb_t audio_requested_callback = NULL;

//hid

static fpga_driver_hid_status_t previous_hid_status, current_hid_status;
static fpga_driver_hid_event_cb_t hid_event_callback = NULL;

static bool driver_timer_tick(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *userCtx);
static void driver_task_function_main(void *arg);
static void driver_task_function_audio(void *arg);
static void driver_task_function_hid(void *arg);

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

    if (xTaskCreatePinnedToCore(driver_task_function_hid, 
                                FPGA_DRIVER_HID_TASK_NAME, 
                                FPGA_DRIVER_HID_TASK_STACKSIZE, 
                                NULL, 
                                FPGA_DRIVER_HID_TASK_PRIORITY, 
                                &driver_hid_task, 
                                FPGA_DRIVER_HID_TASK_PINNED_CORE) != pdPASS)
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

void fpga_driver_hid_get_status(fpga_driver_hid_status_t *status)
{
    taskENTER_CRITICAL(&driver_spinlock);

    *status = current_hid_status;

    taskEXIT_CRITICAL(&driver_spinlock);
}

void fpga_driver_register_hid_event_cb(fpga_driver_hid_event_cb_t callback)
{
    taskENTER_CRITICAL(&driver_spinlock);

    hid_event_callback = callback;

    taskEXIT_CRITICAL(&driver_spinlock);
}

static bool IRAM_ATTR driver_timer_tick(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *userCtx)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    vTaskNotifyGiveFromISR(driver_main_task, &xHigherPriorityTaskWoken);

    return xHigherPriorityTaskWoken == pdTRUE;
}

static void IRAM_ATTR driver_task_function_main(void *arg)
{
    ESP_LOGI(TAG, "fpga driver main task started");

    bool pollHid = true;

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

        //framebuffer and palette

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
                FPGA_DRIVER_ERROR_CHECK(fpga_api_gpu_framebuffer_write(&qspi, 0, buffer_to_present ? framebuffer1 : framebuffer0, FPGA_DRIVER_FRAMEBUFFER_SIZE_BYTES));
                
                taskENTER_CRITICAL(&driver_spinlock);

                framebuffer_idx_to_present = -1;
                present_in_progress = false;

                taskEXIT_CRITICAL(&driver_spinlock);
            }
        }

        vblank = FPGA_API_GPU_STATUS0_GET_VBLANK(status0);

        //audio buffers

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

        //hid kb&mouse

        if (pollHid)
        {
            WORD_ALIGNED_ATTR uint8_t hid_status_buffer[6*4];

            FPGA_DRIVER_ERROR_CHECK(fpga_api_io_hid_get_status(&qspi, hid_status_buffer));

            taskENTER_CRITICAL(&driver_spinlock);

            fpga_driver_hid_status_t previous_current = current_hid_status;

            current_hid_status = (fpga_driver_hid_status_t)
            {
                .mouseKeys = hid_status_buffer[4],
                .keyboardModifiers = hid_status_buffer[5],
                .keyboardKeys = 
                { 
                    hid_status_buffer[6], 
                    hid_status_buffer[7], 
                    hid_status_buffer[8],
                    hid_status_buffer[9], 
                    hid_status_buffer[10],
                    hid_status_buffer[11] 
                },
                .mouseX = (int32_t)(hid_status_buffer[12] << 24 | hid_status_buffer[13] << 16 | hid_status_buffer[14] << 8 | hid_status_buffer[15]),
                .mouseY = (int32_t)(hid_status_buffer[16] << 24 | hid_status_buffer[17] << 16 | hid_status_buffer[18] << 8 | hid_status_buffer[19]),
                .mouseWheel = (int32_t)(hid_status_buffer[20] << 24 | hid_status_buffer[21] << 16 | hid_status_buffer[22] << 8 | hid_status_buffer[23])
            };

            taskEXIT_CRITICAL(&driver_spinlock);

            if (memcmp(&previous_current, &current_hid_status, sizeof(fpga_driver_hid_status_t)))
                xTaskNotifyGive(driver_hid_task);
        }

        pollHid = !pollHid; //every second tick - no hid device works at 2000hz...
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

static inline void driver_helper_hid_map_keys(const uint8_t oldKeys[6], const uint8_t newKeys[6], uint8_t unmappedNewKeys[6], int *unmappedNewKeysCount)
{
    *unmappedNewKeysCount = 0;

    for (int i = 0; i < 6; ++i)
    {
        if (newKeys[i] == 0)
            continue;

        bool keyFound = false;

        for (int j = 0; j < 6; ++j)
        {
            if (newKeys[i] != oldKeys[j])
                continue;

            keyFound = true;
            break;
        }

        if (keyFound)
            continue;

        unmappedNewKeys[*unmappedNewKeysCount] = newKeys[i];
        ++(*unmappedNewKeysCount);
    }
}

static void IRAM_ATTR driver_task_function_hid(void *arg)
{
    ESP_LOGI(TAG, "fpga driver hid task started");

    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        taskENTER_CRITICAL(&driver_spinlock);

        fpga_driver_hid_event_cb_t callback = hid_event_callback;
        fpga_driver_hid_status_t currentStatus = current_hid_status;

        taskEXIT_CRITICAL(&driver_spinlock);

        if (!memcmp(&currentStatus, &previous_hid_status, sizeof(fpga_driver_hid_status_t)))
            continue;

        if (callback != NULL)
        {
            fpga_driver_hid_event_t event;

            //key events

            event.keyEvent.modifiers = currentStatus.keyboardModifiers; //whatever

            if (currentStatus.keyboardModifiers != previous_hid_status.keyboardModifiers)
                for (int i = 1; i < 256; i <<= 1)
                {
                    if ((currentStatus.keyboardModifiers & i) == (previous_hid_status.keyboardModifiers & i))
                        continue;

                    event.type = currentStatus.keyboardModifiers & i 
                        ? FPGA_DRIVER_HID_EVENT_KEY_DOWN 
                        : FPGA_DRIVER_HID_EVENT_KEY_UP;

                    event.keyEvent.keyCode = FPGA_DRIVER_HID_KEY_MODIFIER_TO_CODE(i);

                    callback(event);
                }

            if (!(FPGA_DRIVER_HID_KEY_IS_ERROR(currentStatus.keyboardKeys[0]) || 
                  FPGA_DRIVER_HID_KEY_IS_ERROR(currentStatus.keyboardKeys[1]) || 
                  FPGA_DRIVER_HID_KEY_IS_ERROR(currentStatus.keyboardKeys[2]) || 
                  FPGA_DRIVER_HID_KEY_IS_ERROR(currentStatus.keyboardKeys[3]) || 
                  FPGA_DRIVER_HID_KEY_IS_ERROR(currentStatus.keyboardKeys[4]) || 
                  FPGA_DRIVER_HID_KEY_IS_ERROR(currentStatus.keyboardKeys[5])))  //if rollover error or else just skip keys
            {
                uint8_t unmappedKeys[6];
                int unmappedKeysCount;

                //dont feel like writing super optimized code for this
                driver_helper_hid_map_keys(currentStatus.keyboardKeys, previous_hid_status.keyboardKeys, unmappedKeys, &unmappedKeysCount);

                event.type = FPGA_DRIVER_HID_EVENT_KEY_UP;

                for (int i = 0; i < unmappedKeysCount; ++i)
                {
                    event.keyEvent.keyCode = unmappedKeys[i];
                    callback(event);
                }

                driver_helper_hid_map_keys(previous_hid_status.keyboardKeys, currentStatus.keyboardKeys, unmappedKeys, &unmappedKeysCount);
                
                event.type = FPGA_DRIVER_HID_EVENT_KEY_DOWN;

                for (int i = 0; i < unmappedKeysCount; ++i)
                {
                    event.keyEvent.keyCode = unmappedKeys[i];
                    callback(event);
                }
            }

            //mouse events

            if (currentStatus.mouseKeys != previous_hid_status.mouseKeys)
                for (int i = 1; i < 256; i <<= 1)
                {
                    if ((currentStatus.mouseKeys & i) == (previous_hid_status.mouseKeys & i))
                        continue;

                    event.type = currentStatus.mouseKeys & i 
                        ? FPGA_DRIVER_HID_EVENT_MOUSE_BUTTON_DOWN 
                        : FPGA_DRIVER_HID_EVENT_MOUSE_BUTTON_UP;

                    event.mouseButtonEvent.buttonCode = i;

                    callback(event);
                }

            event.mouseMoveEvent.moveX = currentStatus.mouseX - previous_hid_status.mouseX; //this should work even when int32 overflows
            event.mouseMoveEvent.moveY = currentStatus.mouseY - previous_hid_status.mouseY;
            event.mouseMoveEvent.moveWheel = currentStatus.mouseWheel - previous_hid_status.mouseWheel;

            if (event.mouseMoveEvent.moveX != 0 || 
                event.mouseMoveEvent.moveY != 0 || 
                event.mouseMoveEvent.moveWheel != 0)
            {
                event.type = FPGA_DRIVER_HID_EVENT_MOUSE_MOVE;

                event.mouseMoveEvent.pressedButtons = currentStatus.mouseKeys;
                callback(event);
            }
        }

        previous_hid_status = currentStatus;
    }
}