// Handle HID functions
//
//

#include "sys.h"
#include "usb.h"
#include "regs.h"

#define KBD          0x01
#define MSE          0x02

#define PRINT_REPORTS

enum hid_state {
    hid_init, hid_mouse1, hid_mouse2, hid_keybd1, hid_keybd2, hid_idle
};

struct hid_data {
    uint8_t flags;
    uint8_t ms_ep;
    uint8_t ms_toggle;
    uint8_t ms_pkt[4];
    uint8_t kbd_ep;
    uint8_t kbd_toggle;
    uint8_t kbd_pkt[8];
};

#define local ((struct hid_data *)task->data)

uint32_t *hid_output = (uint32_t *)0x22000000;

static uint32_t reg_status = 0;
static uint32_t reg_keys1 = 0;
static uint32_t reg_keys2 = 0;
static int32_t reg_mouse_x = 0;
static int32_t reg_mouse_y = 0;
static int32_t reg_mouse_wheel = 0;

static void update_hid_regs(void);

// Driver for HID keyboard and mouse
//
void drv_hid(TASK *task, uint8_t *config)
{
    REQ  *req;
    IFC_DESC *iface;
    EPT_DESC *ept;
    
    switch (task->state) {
    
    // Read configuration data, find interfaces for
    // a boot keyboard (3,1,1) and/or a boot mouse (3,1,2).
    //
    case hid_init:
        printf("HID connected\n");
        
        task->data = malloc(sizeof(struct hid_data));
        while (1) {
            if ((iface = find_desc(config, IFC_ID)) == NULL)
                break;
            config = (uint8_t *)iface + iface->bLength;

            if(1)
            {
                if (iface->bInterfaceClass == 3 && iface->bInterfaceSubClass == 1) {
                    switch (iface->bInterfaceProtocol) {
                    case KBD:   task->state   = hid_keybd1;
                                local->flags |= KBD;
                                ept = find_desc(config, EPT_ID);
                                local->kbd_ep  = ept->bEndpointAddress & 0x0f;
                                printf("std keyboard detected (%d)\n", local->kbd_ep);
                                break;

                    case MSE:   task->state   = hid_mouse1;
                                local->flags |= MSE;
                                ept = find_desc(config, EPT_ID);
                                local->ms_ep  = ept->bEndpointAddress & 0x0f;
                                printf("std mouse detected (%d)\n", local->ms_ep);
                                break;

                    default:    printf("HID boot device not recognised\n");
                                continue;
                    }
                }
            }
            else
            {
                // detect any device and read it like keyboard
                task->state   = hid_keybd1;
                local->flags |= KBD;
                ept = find_desc(config, EPT_ID);
                local->kbd_ep  = ept->bEndpointAddress & 0x0f;
                printf("device detected (%d)\n", local->kbd_ep);
            }
        }
        if (local->flags == 0) {
            printf("No boot HID device found\n");
            task->state = hid_idle;
        }
        return;
    
    // Read the keyboard and/or mouse data, alternating between the two
    // for combined devices (e.g. keyboard with a trackpad)
    //
    case hid_mouse1:
        data_req(task, local->ms_ep, IN, local->ms_pkt, 4);
        task->req->toggle = local->ms_toggle;
        task->state = hid_mouse2;
        return;
        
    case hid_mouse2:
        if (task->req->resp == PID_STALL) {
            printf("stalled\n");
            task->state = hid_idle;
            return;
        }
        task->state = (local->flags & KBD) ? hid_keybd1 : hid_mouse1;
        task->when = now_ms() + (local->flags & KBD) ? 0 : 10;
        if (task->req->resp == REQ_OK) {
            local->ms_toggle = task->req->toggle;

            reg_keys1 = (reg_keys1 & 0x00FFFFFF) | (local->ms_pkt[0] << 24);
            reg_mouse_x += (int8_t)local->ms_pkt[1];
            reg_mouse_y += (int8_t)local->ms_pkt[2];
            reg_mouse_wheel += (int8_t)local->ms_pkt[3];

            update_hid_regs();

#ifdef PRINT_REPORTS
            printf("MOUSE: ");
            for(int i=0; i<4; i++) printf("%x ", local->ms_pkt[i]);
            printf("\n");
#endif
        }
        return;
    
    case hid_keybd1:
        data_req(task, local->kbd_ep, IN, local->kbd_pkt, 8);
        task->req->toggle =local->kbd_toggle;
        task->state = hid_keybd2;
        return;
        
    case hid_keybd2:
        if (task->req->resp == PID_STALL) {
            printf("stalled\n");
            task->state = hid_idle;
            return;
        }
        task->state = (local->flags & MSE) ? hid_mouse1 : hid_keybd1;
        task->when = now_ms() + 10;
        if (task->req->resp == REQ_OK) {
            local->kbd_toggle = task->req->toggle;

            reg_keys1 = (reg_keys1 & 0xFF000000) | 
                (local->kbd_pkt[0] << 16) |
                (local->kbd_pkt[2] << 8) |
                local->kbd_pkt[3];

            reg_keys2 = (local->kbd_pkt[4] << 24) |
                (local->kbd_pkt[5] << 16) |
                (local->kbd_pkt[6] << 8) |
                local->kbd_pkt[7];

            update_hid_regs();

#ifdef PRINT_REPORTS
            printf("KEYBD: ");
            for(int i=0; i<8; i++) printf("%x ", local->kbd_pkt[i]);
            printf("\n");
#endif
        }
        return;

    case hid_idle:
        task->when = now_ms() + 255;
        return;

    }
    printf("HID driver step failed (%x, %x)\n", task->state, task->req->resp);
    task->state = dev_stall;
    return;
}

static void update_hid_regs(void)
{
    hid_output[REG_HID_OUTPUT_STATUS] = HID_STATUS_BUSY | reg_status;

    hid_output[REG_HID_OUTPUT_REG_KEYS_1] = reg_keys1;
    hid_output[REG_HID_OUTPUT_REG_KEYS_2] = reg_keys2;
    hid_output[REG_HID_OUTPUT_MOUSE_X] = reg_mouse_x;
    hid_output[REG_HID_OUTPUT_MOUSE_Y] = reg_mouse_y;
    hid_output[REG_HID_OUTPUT_MOUSE_WHEEL] = reg_mouse_wheel;

    ///////////////////
    hid_output[REG_HID_OUTPUT_REG_KEYS_1] = 0xFFEEEEEE;
    hid_output[REG_HID_OUTPUT_REG_KEYS_2] = 0xDDDDDDDD;
    hid_output[REG_HID_OUTPUT_MOUSE_X] = 0xCCCCCCCC;
    hid_output[REG_HID_OUTPUT_MOUSE_Y] = 0xBBBBBBBB;
    hid_output[REG_HID_OUTPUT_MOUSE_WHEEL] = 0xAAAAAAAA;
////////////////////////

    hid_output[REG_HID_OUTPUT_STATUS] = reg_status;
}