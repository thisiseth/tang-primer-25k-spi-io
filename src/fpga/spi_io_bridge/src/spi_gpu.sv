module spi_gpu 
(
    input logic reset,

    input logic cs, 
    
    input logic sclk,
    inout logic mosi_d0,
    inout logic miso_d1,
    inout logic d2,
    inout logic d3,

    output logic [7:0] framebuffer_rgb_in,
    input logic [7:0] framebuffer_rgb_out,
    output logic [23:0] framebuffer_palette_in,
    input logic [23:0] framebuffer_palette_out,

    output logic[16:0] framebuffer_rgb_addr,
    output logic[7:0] framebuffer_palette_addr,

    output logic framebuffer_clk_rgb, framebuffer_clk_palette,
    output logic framebuffer_wren_rgb, framebuffer_wren_palette,

    input logic framebuffer_hblank, framebuffer_vblank,

    output logic audio_fifo_wr_clk, audio_fifo_wren,
    output logic [31:0] audio_fifo_in,
    input logic [10:0] audio_fifo_wnum,
    input logic audio_fifo_full, audio_fifo_almost_full,

    output logic test_led_ready, test_led_done,
    output logic [7:0] test_led
);

    //spi stuff
    //

    localparam int WRITE_DUMMY_CYCLES = 2;

    typedef enum 
    { //funny values for 4-led presentation
        IDLE = 0,       //initial state
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

    logic output_enabled;

    logic [7:0] status_register0;

    assign status_register0 = {5'b10110, framebuffer_hblank, framebuffer_vblank, output_enabled};

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
        COMMAND_FRAMEBUFFER_CONTINUOUS_WRITE    = 8'b10000010, //read phase only, read 3 bytes of first pixel idx, then continuously read pixel data in 1 byte blocks until master stops the transaction
        COMMAND_FRAMEBUFFER_CONTINUOUS_READ     = 8'b11000010, //read+write, read 3 bytes of first pixel idx, then continuously write pixel data in 1 byte blocks until master stops the transaction
        COMMAND_FRAMEBUFFER_SET_PALETTE         = 8'b10000011, //read phase only, 256*3 bytes of palette starting from [0]
        COMMAND_FRAMEBUFFER_GET_PALETTE         = 8'b01000011, //write phase only, 256*3 bytes of palette starting from [0]
        COMMAND_READ_STATUS0                    = 8'b01000000, //write 1 byte of status register 0
        COMMAND_DISABLE_OUTPUT                  = 8'b00000000,
        COMMAND_ENABLE_OUTPUT                   = 8'b00000001,
        COMMAND_AUDIO_BUFFER_READ_STATUS        = 8'b01010000, //write only, 4 bits of flags + 12 bits of number of samples in buffer = 2 bytes
        COMMAND_AUDIO_BUFFER_WRITE              = 8'b11010001  //read+write, read 1 byte (1-256) of how many samples will be written, then read 32bits*number of samples, then write status 2 bytes
    } command_code;

    logic [7:0] command_bits;

    command_code command_enum;

    assign command_enum = command_code'(command_bits);

    //framebuffer manipulation
    //

    logic framebuffer_clk_rgb_pulse_1, framebuffer_clk_rgb_pulse_2;
    logic framebuffer_clk_palette_pulse_1, framebuffer_clk_palette_pulse_2;

    assign framebuffer_clk_rgb = framebuffer_clk_rgb_pulse_1 | framebuffer_clk_rgb_pulse_2;
    assign framebuffer_clk_palette = framebuffer_clk_palette_pulse_1 | framebuffer_clk_palette_pulse_2;

    logic[16:0] framebuffer_rgb_addr_re, framebuffer_rgb_addr_wr;
    logic[7:0] framebuffer_palette_addr_re, framebuffer_palette_addr_wr;

    assign framebuffer_rgb_addr = framebuffer_wren_rgb ? framebuffer_rgb_addr_wr : framebuffer_rgb_addr_re;
    assign framebuffer_palette_addr = framebuffer_wren_palette ? framebuffer_palette_addr_wr : framebuffer_palette_addr_re;

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

            framebuffer_rgb_addr_wr <= 0;
            framebuffer_palette_addr_wr <= -1;

            framebuffer_clk_rgb_pulse_1 <= 0;
            framebuffer_clk_palette_pulse_1 <= 0;

            audio_fifo_wren <= 0;
            audio_fifo_wr_clk <= 0;
            audio_fifo_in <= 0;

            tmp7 <= 0;
            tmp2 <= 0;
            tmp4 <= 0;
            tmp10 <= 0;
        end
        else if (!cs)
        begin
            if (framebuffer_clk_rgb_pulse_1)
                framebuffer_clk_rgb_pulse_1 <= 0;
            if (framebuffer_clk_palette_pulse_1)
                framebuffer_clk_palette_pulse_1 <= 0;

            unique0 case (current_state)
                IDLE : 
                begin
                    command_bits <= {command_bits[3:0], data_in};
                    framebuffer_clk_palette_pulse_1 <= 1; //preloading palette[0] for COMMAND_FRAMEBUFFER_GET_PALETTE
                    audio_fifo_wr_clk <= 1; //need two wr clk pulses to clear previous wnum
                end
                COMMAND : 
                begin
                    command_bits <= {command_bits[3:0], data_in};
                    audio_fifo_wr_clk <= 0;
                end
                READ : 
                begin
                    unique0 case (command_enum)
                        COMMAND_FRAMEBUFFER_SET_PALETTE : 
                        begin
                            read_done <= counter >= 1535;

                            if ((counter % 6) == 5) //set d_in when 24 bits are ready (6 spi cycles)
                            begin
                                framebuffer_palette_in <= {tmp7[19:0], data_in};
                                framebuffer_palette_addr_wr <= 8'(framebuffer_palette_addr_wr + 1); //overflow to 0 at first write
                            end
                            else
                                tmp7 <= {tmp7[19:0], data_in};

                            if ((counter >= 6) && (counter % 6) == 0)
                                framebuffer_clk_palette_pulse_1 <= 1;
                        end
                        COMMAND_FRAMEBUFFER_CONTINUOUS_WRITE : 
                        begin
                            if (counter < 6)
                            begin
                                if (counter <= 4)
                                    framebuffer_rgb_addr_wr <= {framebuffer_rgb_addr_wr[12:0], data_in};
                            end
                            else
                            begin
                                //clk posedge is generated on output edge on counter transition 7-8, 9-10 etc
                                //also when stopping the transaction master is expected to bring SCLK low, so last write should be triggered correctly
                                framebuffer_rgb_in <= {framebuffer_rgb_in[3:0], data_in};

                                if ((counter > 6) && (counter % 2) == 0) //addr increment at counter 8, 10 etc
                                    framebuffer_rgb_addr_wr <= framebuffer_rgb_addr_wr < 76800 //wraparound
                                        ? framebuffer_rgb_addr_wr + 1 
                                        : 0;
                            end
                        end
                        COMMAND_AUDIO_BUFFER_WRITE : 
                        begin
                            read_done <= counter > 1 && counter >= ((tmp4 == 0 ? 256 : tmp4)*8 + 1);

                            if (counter <= 1)
                                tmp4 <= {tmp4[3:0], data_in};
                            else 
                            begin
                                tmp10 <= {tmp10[27:0], data_in};

                                audio_fifo_wren <= 1;

                                if ((counter-2)%8 == 7)
                                begin
                                    audio_fifo_in <= {tmp10[27:0], data_in};
                                    audio_fifo_wr_clk <= 1;

                                    if (audio_fifo_almost_full)
                                        tmp7[15] <= 1;
                                    if (audio_fifo_full)
                                        tmp7[14] <= 1;

                                                  //write response 16 bits: almost_full_occurred, <- sticky
                                                  //                        full_occurred,        <- sticky
                                                  //                        current_almost_full, 
                                                  //                        current_full, 
                                                  //                        12 bits of current wnum
                                    tmp7[13:0] <= {audio_fifo_almost_full, audio_fifo_full, 1'b0, audio_fifo_wnum};
                                end
                                
                                if ((counter-2)%8 == 3)
                                    audio_fifo_wr_clk <= 0;
                            end
                        end
                    endcase
                end
                WRITE_DUMMY :      
                begin
                    unique0 case (command_enum)
                        COMMAND_AUDIO_BUFFER_READ_STATUS : audio_fifo_wr_clk <= ~counter;
                    endcase
                end
                WRITE : 
                begin
                    unique0 case (command_enum)
                        COMMAND_READ_STATUS0 : write_done <= counter > 0;
                        COMMAND_FRAMEBUFFER_GET_PALETTE : write_done <= (counter+1) >= 1536;
                        COMMAND_AUDIO_BUFFER_READ_STATUS, COMMAND_AUDIO_BUFFER_WRITE : write_done <= counter >= 3;
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

            framebuffer_wren_palette <= 0;
            framebuffer_wren_rgb <= 0;
                            
            framebuffer_rgb_addr_re <= 0;
            framebuffer_palette_addr_re <= 0;

            framebuffer_clk_rgb_pulse_2 <= 0;
            framebuffer_clk_palette_pulse_2 <= 0;

            tmp1 <= 0;
            tmp8 <= 0;
        end
        else if (!cs)
        begin
            if (framebuffer_clk_rgb_pulse_2)
                framebuffer_clk_rgb_pulse_2 <= 0;
            if (framebuffer_clk_palette_pulse_2)
                framebuffer_clk_palette_pulse_2 <= 0;

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
                    unique0 case (command_enum)
                        COMMAND_FRAMEBUFFER_SET_PALETTE : framebuffer_wren_palette <= 1;
                        COMMAND_FRAMEBUFFER_CONTINUOUS_WRITE : 
                        begin 
                            if (counter == 0)
                                framebuffer_wren_rgb <= 1;
                            
                            framebuffer_clk_rgb_pulse_2 <= (counter > 6) && ((counter % 2) == 1);
                        end
                    endcase
                end
                WRITE :             
                begin
                    unique0 case (command_enum)
                        COMMAND_READ_STATUS0 : data_out <= tmp1;
                        COMMAND_FRAMEBUFFER_GET_PALETTE : 
                        begin
                            //first palette read happens during IDLE&COMMAND cycles
                            //first latch to tmp8 happens at '-1' cycle - during next_state peek

                            if ((counter % 6) == 5)
                                {data_out, tmp8[23:4]} <= framebuffer_palette_out;
                            else
                                {data_out, tmp8[23:4]} <= tmp8;

                            if ((counter % 6) == 0)
                                framebuffer_palette_addr_re <= 8'(framebuffer_palette_addr_re + 1);

                            if ((counter % 6) == 1)
                                framebuffer_clk_palette_pulse_2 <= 1;
                        end
                        COMMAND_AUDIO_BUFFER_READ_STATUS, COMMAND_AUDIO_BUFFER_WRITE: {data_out, tmp8[15:4]} <= tmp8[15:0];
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
                            COMMAND_READ_STATUS0 : {data_out, tmp1} <= status_register0;
                            COMMAND_FRAMEBUFFER_GET_PALETTE : {data_out, tmp8[23:4]} <= framebuffer_palette_out;
                            COMMAND_AUDIO_BUFFER_READ_STATUS : {data_out, tmp8[15:4]} <= {2'b0, audio_fifo_almost_full, audio_fifo_full, 1'b0, audio_fifo_wnum};
                            COMMAND_AUDIO_BUFFER_WRITE : {data_out, tmp8[15:4]} <= tmp7[15:0];
                        endcase
                    end
                    DONE :
                    begin
                        unique0 case (command_enum)
                            COMMAND_FRAMEBUFFER_SET_PALETTE : framebuffer_clk_palette_pulse_2 <= 1; //workarounds for generating last clock pulse
                            COMMAND_ENABLE_OUTPUT : output_enabled <= 1;
                            COMMAND_DISABLE_OUTPUT : output_enabled <= 0;
                        endcase
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
                WRITE_DUMMY : next_state = counter >= (WRITE_DUMMY_CYCLES-1) ? WRITE : WRITE_DUMMY; //4 dummy cycles
                WRITE : next_state = write_done ? DONE : WRITE;
                DONE : next_state = DONE;
                default: next_state = IDLE;
            endcase;
    end

    assign test_led_ready = command_defined(command_enum);
    assign test_led_done = output_enabled;
    //assign test_led[3:0] = counter[3:0];
    //assign test_led[7:4] = current_state[3:0];

    //assign test_led_ready = in_clk;
    //assign test_led_done = out_clk;
    assign test_led = command_bits;
    //assign test_led[3:0] = {3'b000, data_out_enable};

    //assign test_led_done = framebuffer_clk_rgb;
    //assign test_led_ready = framebuffer_wren_rgb;
    //assign test_led = framebuffer_rgb_in;

endmodule