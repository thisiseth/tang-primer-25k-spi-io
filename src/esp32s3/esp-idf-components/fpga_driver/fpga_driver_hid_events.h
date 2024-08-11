#pragma once

#include <stdint.h>

#ifdef __GNUC__
    #define FPGA_DRIVER_HID_KEY_MODIFIER_TO_CODE(modifier) ((uint8_t)(0xE0 | __builtin_ctz(modifier)))
#else
    #error no builtin_ctz
#endif

typedef enum 
{
    FPGA_DRIVER_HID_KEY_CODE_LEFT_CTRL      = 0xE0,
    FPGA_DRIVER_HID_KEY_CODE_LEFT_SHIFT     = 0xE1,
    FPGA_DRIVER_HID_KEY_CODE_LEFT_ALT       = 0xE2,
    FPGA_DRIVER_HID_KEY_CODE_LEFT_GUI       = 0xE3,
    FPGA_DRIVER_HID_KEY_CODE_RIGHT_CTRL     = 0xE4,
    FPGA_DRIVER_HID_KEY_CODE_RIGHT_SHIFT    = 0xE5,
    FPGA_DRIVER_HID_KEY_CODE_RIGHT_ALT      = 0xE6,
    FPGA_DRIVER_HID_KEY_CODE_RIGHT_GUI      = 0xE7,
} fpga_driver_hid_key_codes_t;

typedef enum 
{
    FPGA_DRIVER_HID_KEY_MODIFIER_LEFT_CTRL      = 1,
    FPGA_DRIVER_HID_KEY_MODIFIER_LEFT_SHIFT     = 2,
    FPGA_DRIVER_HID_KEY_MODIFIER_LEFT_ALT       = 4,
    FPGA_DRIVER_HID_KEY_MODIFIER_LEFT_GUI       = 8,
    FPGA_DRIVER_HID_KEY_MODIFIER_RIGHT_CTRL     = 16,
    FPGA_DRIVER_HID_KEY_MODIFIER_RIGHT_SHIFT    = 32,
    FPGA_DRIVER_HID_KEY_MODIFIER_RIGHT_ALT      = 64,
    FPGA_DRIVER_HID_KEY_MODIFIER_RIGHT_GUI      = 128
} fpga_driver_hid_key_modifiers_t;

typedef enum
{
    FPGA_DRIVER_HID_MOUSE_BUTTON_LEFT   = 1,
    FPGA_DRIVER_HID_MOUSE_BUTTON_RIGHT  = 2,
    FPGA_DRIVER_HID_MOUSE_BUTTON_MIDDLE = 4,
    FPGA_DRIVER_HID_MOUSE_BUTTON_4      = 8,
    FPGA_DRIVER_HID_MOUSE_BUTTON_5      = 16,
    FPGA_DRIVER_HID_MOUSE_BUTTON_6      = 32,
    FPGA_DRIVER_HID_MOUSE_BUTTON_7      = 64,
    FPGA_DRIVER_HID_MOUSE_BUTTON_8      = 128
} fpga_driver_hid_mouse_buttons_t;

typedef enum
{
    FPGA_DRIVER_HID_EVENT_KEY_DOWN,
    FPGA_DRIVER_HID_EVENT_KEY_UP,
    FPGA_DRIVER_HID_EVENT_MOUSE_MOVE,
    FPGA_DRIVER_HID_EVENT_MOUSE_BUTTON_DOWN,
    FPGA_DRIVER_HID_EVENT_MOUSE_BUTTON_UP,
} fpga_driver_hid_event_type_t;

typedef struct
{
    uint8_t keyCode;
    uint8_t modifiers;
} fpga_driver_hid_key_event_t;

typedef struct
{
    uint8_t buttonCode;
} fpga_driver_hid_mouse_button_event_t;

typedef struct
{
    int32_t moveX;
    int32_t moveY;
    int32_t moveWheel;
    uint8_t pressedButtons;
} fpga_driver_hid_mouse_move_event_t;

