module spi_io
(
    input logic reset,

    input logic cs, 
    
    input logic sclk,
    inout logic mosi_d0,
    inout logic miso_d1,
    inout logic d2,
    inout logic d3,

    output logic hid_read,

    input logic hid_keyboard_connected, hid_mouse_connected,

    input logic [7:0] hid_keyboard_modifiers,
    input logic [7:0] hid_keyboard_keycodes [0:5],

    input logic [7:0] hid_mouse_buttons,
    input logic signed [31:0] hid_mouse_x,
    input logic signed [31:0] hid_mouse_y,
    input logic signed [31:0] hid_mouse_wheel,

    output logic test_led_ready, test_led_done,
    output logic [7:0] test_led
);

    //spi stuff
    //

    localparam int WRITE_DUMMY_CYCLES = 2;

    typedef enum 
    { //funny values for 4-led presentation
        IDLE = 0,        //initial state
        COMMAND = 1,     //reading 8 bits of command code
        READ = 2,        //'receive'
        WRITE_DUMMY = 4, //transmit dummy
        WRITE = 8,       //'transmit'
        DONE = 15        //set on last falling SCLK edge, waiting for CS to go high
    } spi_state;

    spi_state current_state, next_state;

    wire spi_reset = cs | reset;

    logic [3:0] data_in;
    logic [3:0] data_out;

    assign {d3, d2, miso_d1, mosi_d0} = current_state == WRITE ? data_out : 4'bZZZZ;
    assign data_in = {d3, d2, miso_d1, mosi_d0};

    //internal registers
    //

    int counter;
    logic [3:0] tmp1, tmp2, tmp3;
    logic [7:0] tmp4, tmp5, tmp6;
    logic [23:0] tmp7, tmp8, tmp9;
    logic [31:0] tmp10, tmp11, tmp12;

    //commands
    //

    logic read_done, write_done;

    typedef enum bit[7:0] 
    {
        COMMAND_USB_HID_GET_STATUS = 8'b01010000
    } command_code;

    logic [7:0] command_bits;

    command_code command_enum;

    assign command_enum = command_code'(command_bits);

    //hid
    //

    assign hid_read = ~cs;

    //CPOL = 0, CPHA = 0:
    //out clock triggers first - on negedge cs and negedge sclk,
    //in clock triggers second = on posedge sclk

    //sample edge
    // & logic affecting next_state calculation
    always_ff @(posedge sclk, posedge (cs | reset))
    begin  
        if (cs | reset)
        begin
            read_done <= 0;
            write_done <= 0;
        end
        else if (!cs)
        begin
            unique0 case (current_state)
                IDLE : command_bits <= {command_bits[3:0], data_in};
                COMMAND : command_bits <= {command_bits[3:0], data_in};
                READ : 
                begin
                end
                WRITE : 
                begin
                    unique0 case (command_enum)
                        COMMAND_USB_HID_GET_STATUS : write_done <= counter >= (6*8 - 1);
                    endcase
                end
                DONE : ;
            endcase
        end
    end

    //output edge
    //is NOT triggered on cs down, so input edge happens first when state is still IDLE
    always_ff @(negedge sclk, posedge (cs | reset))
    begin
        if (cs | reset)
        begin
            current_state <= IDLE;
            counter <= 0;

            tmp10 <= 0;
        end
        else if (!cs)
        begin
            //runs AFTER sample cycle executed with this state
            unique0 case (current_state)
                //// at start spare 1 idle and 1 command cycle for preparing stuff while sample edge reads command
                IDLE :
                begin
                end
                COMMAND : 
                begin
                end
                ////
                READ :      
                begin
                end
                WRITE :             
                begin
                    unique0 case (command_enum)
                        COMMAND_USB_HID_GET_STATUS :
                        begin
                            unique case (counter)
                                (8 - 1)   : {data_out, tmp10[31:4]} <= {hid_mouse_buttons, hid_keyboard_modifiers, hid_keyboard_keycodes[0], hid_keyboard_keycodes[1]};
                                (8*2 - 1) : {data_out, tmp10[31:4]} <= {hid_keyboard_keycodes[2], hid_keyboard_keycodes[3], hid_keyboard_keycodes[4], hid_keyboard_keycodes[5]};
                                (8*3 - 1) : {data_out, tmp10[31:4]} <= hid_mouse_x;
                                (8*4 - 1) : {data_out, tmp10[31:4]} <= hid_mouse_y;
                                (8*5 - 1) : {data_out, tmp10[31:4]} <= hid_mouse_wheel;
                                default : {data_out, tmp10[31:4]} <= tmp10;
                            endcase
                        end
                    endcase
                end
                DONE : ;
            endcase

            if (current_state == next_state)
                counter <= counter + 1;
            else
            begin
                counter <= 0;

                current_state <= next_state;
                
                //prepare stuff for sample edge / next output cycle
                unique0 case (next_state)
                    READ : ;
                    WRITE :             
                    begin
                        unique0 case (command_enum)
                            COMMAND_USB_HID_GET_STATUS : {data_out, tmp10[31:4]} <= 32'hABCDEF12; //status tmp
                        endcase
                    end
                    DONE :
                    begin
                    end
                endcase
            end
        end
    end

    function automatic bit command_defined(command_code command);
        command_code i = i.first();

        if (command == i)
            return 1;

        do 
        begin
            i = i.next();

            if (command == i)
                return 1;
        end
        while (i != i.last());

        return 0;
    endfunction

    function bit command_has_read(command_code command);
        return command_defined(command) & command[7];
    endfunction

    function bit command_has_write(command_code command);
        return command_defined(command) & command[6];
    endfunction

    always_comb
    begin
        if (counter == 32'hFFFFFFFF) //just in case
            next_state = DONE;
        else
            unique case (current_state)
                IDLE : next_state = COMMAND;
                COMMAND : next_state = command_has_read(command_enum) ? READ : (command_has_write(command_enum) ? WRITE_DUMMY : DONE);
                READ : next_state = read_done ? (command_has_write(command_enum) ? WRITE_DUMMY : DONE) : READ;
                WRITE_DUMMY : next_state = counter >= (WRITE_DUMMY_CYCLES-1) ? WRITE : WRITE_DUMMY;
                WRITE : next_state = write_done ? DONE : WRITE;
                DONE : next_state = DONE;
                default: next_state = IDLE;
            endcase;
    end

    assign test_led_ready = command_defined(command_enum);
    assign test_led_done = cs;
    assign test_led = command_bits;
    //assign test_led[3:0] = counter[3:0];
    //assign test_led[7:4] = current_state[3:0];

endmodule