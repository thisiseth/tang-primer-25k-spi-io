module framebuffer
(
    //spi side
    input logic [7:0] rgb_in,
    output logic [7:0] rgb_out,
    input logic [23:0] palette_in,
    output logic [23:0] palette_out,

    input logic[16:0] rgb_addr,
    input logic[7:0] palette_addr,

    input logic clk_rgb, clk_palette,
    input logic wren_rgb, wren_palette,
    
    output logic hblank, vblank,

    //hdmi side
    input logic clk_pixel,
    output logic [23:0] screen_rgb_out,
    input logic [11:0] cx, cy, screen_width, screen_height
);

    localparam int FRAME_WIDTH = 320;
    localparam int FRAME_HEIGHT = 240;
    localparam int SCALE_X = 3;
    localparam int SCALE_Y = 3;

    wire [11:0] frame_border_top_bottom = (screen_height - FRAME_HEIGHT*SCALE_Y)/2;
    wire [11:0] frame_border_left_right = (screen_width - FRAME_WIDTH*SCALE_X)/2;

    bit [7:0] framebuffer [FRAME_WIDTH*FRAME_HEIGHT];
    bit [23:0] palette [256];

    always_ff @(posedge clk_rgb)
    begin
        if (wren_rgb)
            framebuffer[rgb_addr] <= rgb_in;
        else
            rgb_out <= framebuffer[rgb_addr];
    end

    always_ff @(posedge clk_palette)
    begin
        if (wren_palette)
            palette[palette_addr] <= palette_in;
        else
            palette_out <= palette[palette_addr];
    end

    logic [16:0] framebuffer_idx;

    //320*240 mapping with integer scaling and letterboxing
    always_ff @(posedge clk_pixel)
    begin
        automatic logic [8:0] next_framebuffer_x = 9'b0;
        automatic logic [7:0] next_framebuffer_y = 8'b0;

        automatic logic signed [12:0] cx_offset = 13'(cx - frame_border_left_right + 3); //compensate latency :/
        automatic logic signed [12:0] cy_offset = 13'(cy - frame_border_top_bottom);

        if (cx >= screen_width || cy >= screen_height)
        begin
            framebuffer_idx <= 0; //h-blank / v-blank

            hblank <= cx >= screen_width;
            vblank <= cy >= screen_height;
        end
        else 
        begin
            if (cy_offset >= 0 && cy_offset < FRAME_HEIGHT*SCALE_Y && cx_offset < FRAME_WIDTH*SCALE_X) //y is in framebuffer zone, x is in or before framebuffer zone
            begin
                next_framebuffer_x = 9'(cx_offset < 0 ? 0 : 9'(cx_offset / SCALE_X));
                next_framebuffer_y = 8'(cy_offset / SCALE_Y);
            end //else prepare {0;0}
            
            framebuffer_idx <= 17'(next_framebuffer_x + next_framebuffer_y*FRAME_WIDTH);

            hblank <= cx_offset < 0 || cx_offset >= FRAME_WIDTH*SCALE_X;
            vblank <= cy_offset < 0 || cy_offset >= FRAME_HEIGHT*SCALE_Y;
        end
    end

    logic [23:0] next_rgb;
    logic [7:0] next_palette;

    always_ff @(posedge clk_pixel)
    begin
        if (cx < frame_border_left_right || 
            cx >= (FRAME_WIDTH*SCALE_X+frame_border_left_right) || 
            cy < frame_border_top_bottom || 
            cy >= (FRAME_HEIGHT*SCALE_Y+frame_border_top_bottom))
            screen_rgb_out <= {8'(cx), 8'(cy), 8'(cx+cy)};
        else
            screen_rgb_out <= next_rgb;

        next_palette <= framebuffer[framebuffer_idx];
        next_rgb <= palette[next_palette];
    end

endmodule