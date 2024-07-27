//Copyright (C)2014-2024 GOWIN Semiconductor Corporation.
//All rights reserved.
//File Title: Timing Constraints file
//Tool Version: V1.9.9.03 (64-bit) 
//Created Time: 2024-06-28 19:33:15
create_clock -name clk_50m -period 20 -waveform {0 10} [get_ports {clk_50m}]

create_generated_clock -name clk_hdmi_1080 -source [get_ports {clk_50m}] -master_clock clk_50m -multiply_by 3 [get_pins {pll_hdmi/PLLA_inst/CLKOUT0}]
create_generated_clock -name clk_hdmi_1080_x5 -source [get_ports {clk_50m}] -master_clock clk_50m -multiply_by 15 [get_pins {pll_hdmi/PLLA_inst/CLKOUT1}]

//create_generated_clock -name clk_hdmi_720 -source [get_ports {clk_50m}] -master_clock clk_50m -multiply_by 3 -divide_by 2 [get_pins {pll_hdmi/PLLA_inst/CLKOUT0}]
//create_generated_clock -name clk_hdmi_720_x5 -source [get_ports {clk_50m}] -master_clock clk_50m -multiply_by 15 -divide_by 2 [get_pins {pll_hdmi/PLLA_inst/CLKOUT1}]

create_generated_clock -name clk_usb_48m -source [get_ports {clk_50m}] -master_clock clk_50m -multiply_by 23 -divide_by 24 [get_pins {pll_usb/PLLA_inst/CLKOUT0}]
create_generated_clock -name clk_usb_48m_x2 -source [get_ports {clk_50m}] -master_clock clk_50m -multiply_by 23 -divide_by 12 -phase 45 [get_pins {pll_usb/PLLA_inst/CLKOUT1}]

create_generated_clock -name clk_audio_48k -source [get_ports {clk_50m}] -master_clock clk_50m -multiply_by 3 -divide_by 3125 [get_nets {clk_audio}]

create_clock -name clk_spi -period 12.5 -waveform {0 6.25} [get_ports {spi_sclk}]
