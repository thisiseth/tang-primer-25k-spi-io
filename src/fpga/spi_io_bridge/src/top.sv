`define GW_IDE

module top 
(
    input logic clk_50m,

    output logic [2:0] hdmi_tx_tmds,
    output logic hdmi_tx_tmds_clk,

    inout logic hdmi_tx_ddc_sda,
    output logic hdmi_tx_ddc_scl,
    input logic hdmi_tx_hpd,
    inout logic hdmi_tx_cec,
    input logic hdmi_tx_heac,

    inout logic usb_host_dp,
    inout logic usb_host_dn,

    output logic usb_cpu_uart_tx,
    input logic usb_cpu_uart_rx,

    input logic spi_cs0,
    input logic spi_cs1,
    input logic spi_sclk,
    inout logic spi_mosi_d0,
    inout logic spi_miso_d1,
    inout logic spi_d2,
    inout logic spi_d3,

    output logic led_ready,
    output logic led_done, 

    input logic button_s1,
    input logic button_s2,

    output logic [7:0] test_led
);

    logic pll_hdmi_lock, pll_usb_lock;

    logic clk_pixel;
    logic clk_pixel_x5;
    logic clk_usb_48m;
    logic clk_usb_cpu_bram_96m;

    wire clk_audio;
    wire reset;

    assign clk_audio = 0;
    assign reset = 0;

    assign hdmi_tx_ddc_sda = 1;
    assign hdmi_tx_ddc_scl = 1;
    assign hdmi_tx_cec = 1;

    gowin_pll_hdmi_1080 pll_hdmi (
        .lock(pll_hdmi_lock), 
        .clkout0(clk_pixel),
        .clkout1(clk_pixel_x5),
        .clkin(clk_50m)
    );

    gowin_pll_usb pll_usb (
        .lock(pll_usb_lock), 
        .clkout0(clk_usb_48m), //~47.91 
        .clkout1(clk_usb_cpu_bram_96m), //x2, phase 45
        .clkin(clk_50m) 
    );

    logic [15:0] audio_sample_word [1:0];

    assign audio_sample_word = '{16'd0, 16'd0};

    logic [23:0] rgb;
    logic [11:0] cx, cy, screen_start_x, screen_start_y, frame_width, frame_height, screen_width, screen_height;

    logic [7:0] framebuffer_rgb_in;
    logic [7:0] framebuffer_rgb_out;
    logic [23:0] framebuffer_palette_in;
    logic [23:0] framebuffer_palette_out;

    logic[16:0] framebuffer_rgb_addr;
    logic[7:0] framebuffer_palette_addr;

    logic framebuffer_clk_rgb, framebuffer_clk_palette;
    logic framebuffer_wren_rgb, framebuffer_wren_palette;

    logic framebuffer_hblank, framebuffer_vblank;

    // Border test (left = red, top = green, right = blue, bottom = blue, fill = black)
    //always @(posedge clk_pixel)
    //  rgb <= {cx == 0 ? ~8'd0 : 8'd0, 
    //          cy == 0 ? ~8'd0 : 8'd0, 
    //          cx == screen_width - 1'd1 || cy == screen_height - 1'd1 ? ~8'd0 : 8'd0};

    //always @(posedge clk_pixel)
    //    rgb <= {8'(cx % 255),
    //            8'(cy % 255),
    //            8'((cx + cy) % 255)};

    hdmi #(.VIDEO_ID_CODE(16), .DVI_OUTPUT(1)) hdmi
    (
        .clk_pixel_x5(clk_pixel_x5),
        .clk_pixel(clk_pixel),
        .clk_audio(clk_audio),
        .reset(reset),
        .rgb(rgb),
        .audio_sample_word(audio_sample_word),
        .tmds(hdmi_tx_tmds),
        .tmds_clock(hdmi_tx_tmds_clk),
        .cx(cx),
        .cy(cy),
        .frame_width(frame_width),
        .frame_height(frame_height),
        .screen_width(screen_width),
        .screen_height(screen_height)
    );

    framebuffer framebuffer
    (
        .rgb_in(framebuffer_rgb_in),
        .rgb_out(framebuffer_rgb_out),
        .palette_in(framebuffer_palette_in),
        .palette_out(framebuffer_palette_out),

        .rgb_addr(framebuffer_rgb_addr),
        .palette_addr(framebuffer_palette_addr),

        .clk_rgb(framebuffer_clk_rgb), .clk_palette(framebuffer_clk_palette),
        .wren_rgb(framebuffer_wren_rgb), .wren_palette(framebuffer_wren_palette),

        .hblank(framebuffer_hblank), .vblank(framebuffer_vblank),

        .clk_pixel(clk_pixel),
        .screen_rgb_out(rgb),
        .cx(cx),
        .cy(cy),
        .screen_width(screen_width),
        .screen_height(screen_height)
    );

    spi_gpu spi0
    (   
        .reset(1'b0),
        .cs(spi_cs0),
        .sclk(spi_sclk),
        .mosi_d0(spi_mosi_d0),
        .miso_d1(spi_miso_d1),
        .d2(spi_d2),
        .d3(spi_d3),

        .framebuffer_rgb_in(framebuffer_rgb_in),
        .framebuffer_rgb_out(framebuffer_rgb_out),
        .framebuffer_palette_in(framebuffer_palette_in),
        .framebuffer_palette_out(framebuffer_palette_out),

        .framebuffer_rgb_addr(framebuffer_rgb_addr),
        .framebuffer_palette_addr(framebuffer_palette_addr),

        .framebuffer_clk_rgb(framebuffer_clk_rgb), .framebuffer_clk_palette(framebuffer_clk_palette),
        .framebuffer_wren_rgb(framebuffer_wren_rgb), .framebuffer_wren_palette(framebuffer_wren_palette),

        .framebuffer_hblank(framebuffer_hblank), .framebuffer_vblank(framebuffer_vblank),

        .test_led_ready(led_ready),
        .test_led_done(led_done),

        .test_led(test_led)
    );

    usb_host usb_host 
    (
        .clk_48m(clk_usb_48m),
        .clk_cpu_bram_96m(clk_usb_cpu_bram_96m),
        .reset(1'b0),

        .usb_dp(usb_host_dp),
        .usb_dn(usb_host_dn),

        .cpu_uart_tx(usb_cpu_uart_tx),
        .cpu_uart_rx(usb_cpu_uart_rx)
    );

endmodule