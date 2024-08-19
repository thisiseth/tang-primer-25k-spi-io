# tang-primer-25k-spi-io

## Intro
Main goal of this project was to learn about FPGA programming through writing something marginally useful.

A reasonably priced (~35$) [Sipeed Tang Primer 25K](https://wiki.sipeed.com/tang25k) enthusiast-level board caught my eye. 
It is built around Gowin GW5A-25 FPGA chip and has plenty of resources for tinkering around with, including 1 megabit of BRAM, ~23K LEs and 6 PLLs.

After writing my first blinky, impressed by RP2040 outputting VGA or even DVI video using PIO, i realized, that now i have something much better than PIO...

## SPI IO: HDMI and USB HID
The project consists of two parts:

#### Tang Primer 25K board
* HDMI output: GW5A-25 can do 1080p DVI (no audio) or 720p HDMI (with audio) with no issues
* USB HID handling: since the board already has a type-A USB port, i decided to offload this job to the FPGA too
* Frame and audio buffers: without a 40-pin SDRAM module, 1 megabit of on-chip BRAM is enough for one 320*240@256 color framebuffer, 24 bit palette and about 1 frame of audio
* Has Quad SPI slave interface with two CS lines: 'GPU' for HDMI and 'IO' for USB HID

#### ESP32-S3 DIY PMOD
* Has a lot of conventional computating power, two cores, high clock, FPU and more
* Has up to 32 megabytes of flash and 8/16 megabytes of PSRAM
* Acts as an SPI master with a simple driver layer handling FPGA polling, vsync, audio buffering etc.
* Driver provides simple interface for frame presentation, synchronized audio and handling HID events

![plot](./doc/setup.jpg)

## Specs
Combining FPGA based GPU-peripheral and an actual CPU together results in a something resembling a DOS-era PC:
* The only video mode is 320*240@256 customizable color palette, integer scaled and pillarboxed to 720p@60 HDMI
* 16 bit 48 KHz stereo sound through HDMI
* Quad SPI communication between MCU and FPGA at 80 MHz, giving plenty of bandwidth for 320*240@60hz
* USB HID: uses small RISC-V CPU to handle USB1.1 mice, keyboards, combined mouse-keyboard devices and hubs

## Hardware
Disregard my homebrew HDMI PMOD, at some point i was thinking of decoding HDMI ARC - for just HDMI output [Sipeed DVI(HDMI) PMOD](https://wiki.sipeed.com/hardware/en/tang/tang-PMOD/FPGA_PMOD.html#PMOD_DVI) is fine.

ESP32-S3 PMOD just wires out 7 ESP32-S3 GPIOs: 4 bidirectional Quad SPI data lines, SCK and two CS lines. 

Any 7 pins can be used for this using SPI over GPIO matrix.
Works fine, although wire length can be a problem at 80 MHz if regular devkit and dupont jumpers to FPGA are used, in this case 40 or even 20 MHz should still be okay. 
I used bare [ESP32-S3-WROOM module](./doc/pmod_esp32s3_front.jpg) on a perfboard.

## Credits
### Gowin GW5A-25 FPGA side
* HDMI video and audio encoder: [hdmi](https://github.com/hdl-util/hdmi/) by [Sameer Puri](https://github.com/sameer), dual-licensed under MIT and Apache 2.0
* USB HID mouse and keyboard handling: based on [usb_host](https://github.com/emard/usb_host) by [emard](https://github.com/emard), includes BSD licensed code

### ESP32S3 'CPU' side
#### Doom port
* [Chocolate Doom](https://github.com/chocolate-doom/chocolate-doom) by https://www.chocolate-doom.org/, licensed under GPLv2
* [libsamplerate](https://github.com/libsndfile/libsamplerate) by [Erik de Castro Lopo](mailto:erikd@mega-nerd.com), licensed under BSD 2-Clause
* Doom optimizations: [rp2040-doom](https://github.com/kilograham/rp2040-doom) by [Graham Sanderson](https://github.com/kilograham), licensed under GPLv2

## Licence
All code being a part or derived from existing project (e.g. chocolate doom code) retains its original licence,
everything else is licensed under MIT
