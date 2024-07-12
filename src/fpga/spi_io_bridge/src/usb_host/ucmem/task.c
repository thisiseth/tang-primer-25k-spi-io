
// Manage the root port using the SIE registers.
//

#include "sys.h"
#include "regs.h"
#include "usb.h"

uint32_t *usbh = (uint32_t *)0x21000000;
int sim;

// Put the root port into reset mode
//
void root_reset(void)
{
    // UTMI+ reset is: opmode = 2, termselect = 0, xcvrselect = 0
    // and DP+DM pulldown = 1.
    usbh[REG_CTRL] = CTL_OPMODE2|CTL_XCVRSEL0|CTL_DP_PULLD|CTL_DN_PULLD;
}

// Configure the root port: set speed and SOF generation
//
void root_config(int speed, int enable_sof)
{
    uint32_t val;

    val  = (speed == 1) ? CTL_XCVRSEL1 : (speed == 3) ? CTL_XCVRSEL3 : CTL_XCVRSEL2;
    val |= CTL_OPMODE0|CTL_TERMSEL|CTL_DP_PULLD|CTL_DN_PULLD|CTL_TX_FLSH;
    if (enable_sof) val |= CTL_SOF_EN;

    usbh[REG_CTRL] = val;
}

// Manage the TASK pool. There is one task per USB cable and it holds data
// for both the port and the device end of the cable. This implies that there
// is one task per bus address, talking to one control endpoint.
//
#define MAX_TASK        12

TASK tasks[MAX_TASK];
REQ  requests[MAX_TASK];

TASK *new_task(void)
{
    for (int i = 0; i < MAX_TASK; i++ ) {
        if ( (tasks[i].prt_flags & (ROOT_PORT|HUB_PORT)) == 0)
            return &tasks[i];
    }
    printf("panic: out of tasks\n");
    return NULL;
}

TASK *clr_task(TASK *task)
{
    REQ *req = task->req;

    if (task->driver == &drv_hub)
        free_hub_tasks(task);
    if (task->data)
        free(task->data);
    memset(task, 0, sizeof(TASK));
    memset(req,  0, sizeof(REQ));
    req->maxsz = 8;
    req->task  = task;
    task->req = req;
    return task;
}

// Manage root port connection status
//
static void check_root(TASK *task)
{
    // device disconnected?
    if ((usbh[REG_STAT] & STAT_DETECT) == 0) {
        printf("device disconnected, task addr = %d\n", task->addr);
        clr_task(task);
        root_config(1, 0);
        task->prt_flags = ROOT_PORT|PRT_POWER;
        return;
    }
    
    // connected and nothing to do?
    if (task->prt_flags & (PRT_STALL|PRT_ENABLED)) return;
    
    // wait for connection
    if ((task->prt_flags & (PRT_POWER|PRT_CONNECT)) == PRT_POWER) {
        while ((usbh[REG_STAT] & STAT_DETECT) == 0) ;
        task->prt_speed  = (usbh[REG_STAT] & 1) ? SPEED_FS : SPEED_LS;
        task->prt_flags |= PRT_CONNECT;
        printf("device connect, speed = %x\n", task->prt_speed);
        wait_ms(20);
    }
    
    // reset port / device
    if ((task->prt_flags & (PRT_CONNECT|PRT_RESET)) == PRT_CONNECT) {
        root_reset();
        wait_ms(50);
        root_config(task->prt_speed, (sim ? 0 : 1));
        task->prt_flags |= PRT_RESET;
    }

    // enable port / device 100 ms after reset
    if ((task->prt_flags & (PRT_RESET|PRT_ENABLED)) == PRT_RESET) {
        wait_ms(100);
        task->driver = &enum_dev;
        task->prt_flags |= PRT_ENABLED;
        task->state = 0; // = enum:set_address
    }
}

extern uint8_t end[]; // end of code + data

void main()
{
    TASK *root, *task;

    printf("start %x\n", end - 0x10000000); // keep an eye on code/data size
    sim = is_sim();

    // Initialise the 'tasks' table and set up root task
    for(int i=0; i<MAX_TASK; i++) {
        tasks[i].req = &requests[i];
        clr_task(&tasks[i]);
    }
    root = new_task();
    root->prt_flags = ROOT_PORT|PRT_POWER;

    // Event loop
    while(1) {
        for(int i=0; i<MAX_TASK; i++) {
            task = &tasks[i];

            // Skip inactive tasks, check root task status
            if ((task->prt_flags & (ROOT_PORT|HUB_PORT)) == 0)
                continue;
            if (task->prt_flags & ROOT_PORT)
                check_root(task);
            if ((task->prt_flags & PRT_ENABLED) == 0)
                continue;
            
            // Perform state machine step as needed:
            // - request step when a request is active
            // - task driver step otherwise
            if (task->req->state != rq_idle )
                do_request_step(task->req);
            else {
                if (sim || task->when <= now_ms()) {
                    task->driver(task, NULL);
                    if (task->state == dev_stall)
                        task->prt_flags |= PRT_STALL;
                }
            }
        }
    }
}
