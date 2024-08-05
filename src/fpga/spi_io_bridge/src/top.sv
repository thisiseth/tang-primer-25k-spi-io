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

    assign hdmi_tx_ddc_sda = 1;
    assign hdmi_tx_ddc_scl = 1;
    assign hdmi_tx_cec = 1;

    // clocks

    logic pll_hdmi_lock, pll_usb_lock;

    wire plls_locked = pll_hdmi_lock & pll_usb_lock;

    logic clk_pixel;
    logic clk_pixel_x5;
    logic clk_usb_48m;
    logic clk_usb_cpu_bram_96m;

    logic clk_audio;

    assign reset = 0;

    gowin_pll_hdmi_720 pll_hdmi (
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

    // hdmi

    logic [23:0] rgb;
    logic [11:0] cx, cy, screen_start_x, screen_start_y, frame_width, frame_height, screen_width, screen_height;

    logic [15:0] audio_sample_word [1:0];

    hdmi 
    #(
        .VIDEO_ID_CODE(4), //4: 720p60hz, 16: 1080p60hz
        .DVI_OUTPUT(0), //true HDMI with audio
        .VIDEO_REFRESH_RATE(60.0), 
        .AUDIO_RATE(48000)
    )
    hdmi
    (
        .clk_pixel_x5(clk_pixel_x5),
        .clk_pixel(clk_pixel),
        .clk_audio(clk_audio),
        .reset(~pll_hdmi_lock),
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

    // framebuffer

    logic [7:0] framebuffer_rgb_in;
    logic [7:0] framebuffer_rgb_out;
    logic [23:0] framebuffer_palette_in;
    logic [23:0] framebuffer_palette_out;

    logic[16:0] framebuffer_rgb_addr;
    logic[7:0] framebuffer_palette_addr;

    logic framebuffer_clk_rgb, framebuffer_clk_palette;
    logic framebuffer_wren_rgb, framebuffer_wren_palette;

    logic framebuffer_hblank, framebuffer_vblank;

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

    // audio

    logic audio_fifo_wr_clk, audio_fifo_wren;

    logic [31:0] audio_fifo_in, audio_fifo_out;
    logic [10:0] audio_fifo_wnum;
    logic audio_fifo_empty, audio_fifo_full, audio_fifo_almost_empty, audio_fifo_almost_full;

    gowin_fifo_audio audio_fifo
    (
		.Data(audio_fifo_in),
		.WrClk(audio_fifo_wr_clk),
		.RdClk(~clk_audio), 
		.WrEn(audio_fifo_wren),
		.RdEn(1'b1),
		.Wnum(audio_fifo_wnum),
		.Almost_Empty(audio_fifo_almost_empty),
		.Almost_Full(audio_fifo_almost_full), 
		.Q(audio_fifo_out),
		.Empty(audio_fifo_empty), 
		.Full(audio_fifo_full)
	);

    logic [11:0] audio_div_counter;

    localparam int PIXEL_TO_AUDIO_DIV = 1562; //720p: 75mhz to ~48khz
    //localparam int PIXEL_TO_AUDIO_DIV = 3125; //1080p: 150mhz to 48khz
    
    always_ff @(posedge clk_pixel, negedge pll_hdmi_lock)
    begin
        if (!pll_hdmi_lock)
        begin
            audio_div_counter <= 0;
            clk_audio <= 0;
        end
        else
        begin
            audio_div_counter <= audio_div_counter >= (PIXEL_TO_AUDIO_DIV-1) ? 0 : audio_div_counter + 1;
            clk_audio <= audio_div_counter >= (PIXEL_TO_AUDIO_DIV/2);
        end
    end

    always_ff @(posedge clk_audio)
    begin
                            //   right                  left
        audio_sample_word <= '{audio_fifo_out[31:16], audio_fifo_out[15:0]}; //if fifo is empty last sample should be output
    end

    // usb

    logic hid_read;
    logic hid_keyboard_connected, hid_mouse_connected;
    logic [7:0] hid_keyboard_modifiers;
    logic [7:0] hid_keyboard_keycodes [5:0];
    logic [7:0] hid_mouse_buttons;
    logic signed [31:0] hid_mouse_x, hid_mouse_y, hid_mouse_wheel;

    usb_host usb_host 
    (
        .clk_48m(clk_usb_48m),
        .clk_cpu_bram_96m(clk_usb_cpu_bram_96m),
        .reset(~pll_usb_lock),

        .usb_dp(usb_host_dp),
        .usb_dn(usb_host_dn),

        .cpu_uart_tx(usb_cpu_uart_tx),
        .cpu_uart_rx(usb_cpu_uart_rx),

        .hid_read(hid_read),
        .hid_keyboard_connected(hid_keyboard_connected), .hid_mouse_connected(hid_mouse_connected),
        .hid_keyboard_modifiers(hid_keyboard_modifiers),
        .hid_keyboard_keycodes(hid_keyboard_keycodes),
        .hid_mouse_buttons(hid_mouse_buttons),
        .hid_mouse_x(hid_mouse_x), .hid_mouse_y(hid_mouse_y), .hid_mouse_wheel(hid_mouse_wheel)
    );

    // spi

    wire spi0_d0, spi0_d1, spi0_d2, spi0_d3;
    wire spi1_d0, spi1_d1, spi1_d2, spi1_d3;

    assign {spi_mosi_d0, spi_miso_d1, spi_d2, spi_d3} = 
        !spi_cs0 ? {spi0_d0, spi0_d1, spi0_d2, spi0_d3} :
        !spi_cs1 ? {spi1_d0, spi1_d1, spi1_d2, spi1_d3} :
        4'bZZZZ;

    spi_gpu spi0
    (   
        .reset(reset),
        .cs(spi_cs0),
        .sclk(spi_sclk),
        .mosi_d0(spi0_d0),
        .miso_d1(spi0_d1),
        .d2(spi0_d2),
        .d3(spi0_d3),

        .framebuffer_rgb_in(framebuffer_rgb_in),
        .framebuffer_rgb_out(framebuffer_rgb_out),
        .framebuffer_palette_in(framebuffer_palette_in),
        .framebuffer_palette_out(framebuffer_palette_out),

        .framebuffer_rgb_addr(framebuffer_rgb_addr),
        .framebuffer_palette_addr(framebuffer_palette_addr),

        .framebuffer_clk_rgb(framebuffer_clk_rgb), .framebuffer_clk_palette(framebuffer_clk_palette),
        .framebuffer_wren_rgb(framebuffer_wren_rgb), .framebuffer_wren_palette(framebuffer_wren_palette),

        .framebuffer_hblank(framebuffer_hblank), .framebuffer_vblank(framebuffer_vblank),

        .audio_fifo_wr_clk(audio_fifo_wr_clk), .audio_fifo_wren(audio_fifo_wren),
        .audio_fifo_in(audio_fifo_in),
        .audio_fifo_wnum(audio_fifo_wnum),
        .audio_fifo_full(audio_fifo_full),
        .audio_fifo_almost_full(audio_fifo_almost_full)

        //.test_led_ready(led_ready),
        //.test_led_done(led_done),

        //.test_led(test_led)
    );

    spi_io spi1
    (   
        .reset(reset),
        .cs(spi_cs1),
        .sclk(spi_sclk),
        .mosi_d0(spi1_d0),
        .miso_d1(spi1_d1),
        .d2(spi1_d2),
        .d3(spi1_d3),

        .hid_read(hid_read),
        .hid_keyboard_connected(hid_keyboard_connected), .hid_mouse_connected(hid_mouse_connected),
        .hid_keyboard_modifiers(hid_keyboard_modifiers),
        .hid_keyboard_keycodes(hid_keyboard_keycodes),
        .hid_mouse_buttons(hid_mouse_buttons),
        .hid_mouse_x(hid_mouse_x), .hid_mouse_y(hid_mouse_y), .hid_mouse_wheel(hid_mouse_wheel),

        .test_led_ready(led_ready),
        .test_led_done(led_done),

        .test_led(test_led)
    );

endmodule