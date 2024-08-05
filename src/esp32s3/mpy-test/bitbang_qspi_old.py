from machine import Pin
import time

SPI_CS0 = 41
SPI_CS1 = 39
SPI_SCLK = 2
SPI_D0 = 5
SPI_D1 = 7
SPI_D2 = 16
SPI_D3 = 18

cs0 = Pin(SPI_CS0, mode=Pin.OUT, value=True)
cs1 = Pin(SPI_CS0, mode=Pin.OUT, value=True)
sclk = Pin(SPI_SCLK, mode=Pin.OUT, value=False)

def reset():
    sclk(0)
    cs0(1)
    cs0(1)
    
def step(skip):
    if skip:
        return True
    
    return input() == 'f'

globalSkipSteps = False

def qspi(cs, command, sendData, receiveLength):
    skipSteps = globalSkipSteps
    ret = []
    
    d0 = Pin(SPI_D0, mode=Pin.OUT)
    d1 = Pin(SPI_D1, mode=Pin.OUT)
    d2 = Pin(SPI_D2, mode=Pin.OUT)
    d3 = Pin(SPI_D3, mode=Pin.OUT)
    
    cs(0)
    #sclk(1)#
    #sclk(0)#
    
    d0((command & 0b1000_0000) != 0)
    d1((command & 0b0100_0000) != 0)
    d2((command & 0b0010_0000) != 0)
    d3((command & 0b0001_0000) != 0)
    sclk(1)
    
    if not skipSteps: print(f'command[7:4]: {d0()}{d1()}{d2()}{d3()}')
    skipSteps = step(skipSteps)
    
    sclk(0)

    d0((command & 0b0000_1000) != 0)
    d1((command & 0b0000_0100) != 0)
    d2((command & 0b0000_0010) != 0)
    d3((command & 0b0000_0001) != 0)
    sclk(1)
    
    if not skipSteps: print(f'command[3:0]: {d0()}{d1()}{d2()}{d3()}')
    skipSteps = step(skipSteps)
    
    for sendByte in sendData:
        sclk(0)
        
        if not skipSteps: print('<output edge>')
        skipSteps = step(skipSteps)
        
        d0((sendByte & 0b1000_0000) != 0)
        d1((sendByte & 0b0100_0000) != 0)
        d2((sendByte & 0b0010_0000) != 0)
        d3((sendByte & 0b0001_0000) != 0)
        sclk(1)
        
        if not skipSteps: print(f'send[7:4]: {d0()}{d1()}{d2()}{d3()}')
        skipSteps = step(skipSteps)
        
        sclk(0)
        
        if not skipSteps: print('<output edge>')
        skipSteps = step(skipSteps)

        d0((sendByte & 0b0000_1000) != 0)
        d1((sendByte & 0b0000_0100) != 0)
        d2((sendByte & 0b0000_0010) != 0)
        d3((sendByte & 0b0000_0001) != 0)
        sclk(1)
        
        if not skipSteps: print(f'send[3:0]: {d0()}{d1()}{d2()}{d3()}')
        skipSteps = step(skipSteps)
            
    d0 = Pin(SPI_D0, mode=Pin.IN)
    d1 = Pin(SPI_D1, mode=Pin.IN)
    d2 = Pin(SPI_D2, mode=Pin.IN)
    d3 = Pin(SPI_D3, mode=Pin.IN)
    
    sclk(0)
    
    if not skipSteps: print('output edge')
    
    #print(f'prereceive[3:0]: {d0()}{d1()}{d2()}{d3()}')
    #skipSteps = step(skipSteps)
    
    for i in range(receiveLength):
        currentByte = 0;
                
        #if (i>765):
        #    print(i)
        #    input()
        
        skipSteps = step(skipSteps)
        
        sclk(1)
        currentByte |= d0() << 7;
        currentByte |= d1() << 6;
        currentByte |= d2() << 5;
        currentByte |= d3() << 4;
        
        if not skipSteps: print(f'receive[3:0]: {d0()}{d1()}{d2()}{d3()}')
        
        sclk(0)
        
        skipSteps = step(skipSteps)
        
        sclk(1)
        currentByte |= d0() << 3;
        currentByte |= d1() << 2;
        currentByte |= d2() << 1;
        currentByte |= d3();
        
        if not skipSteps: print(f'receive[3:0]: {d0()}{d1()}{d2()}{d3()}')
        if not skipSteps: print(f'read: {currentByte:#010b}')
        
        sclk(0)
        
        ret.append(currentByte)
        
    cs(1)
    
    return ret
    
pal = []
frame = []

for abc in range(300):
    pal += [abc, abc, abc]
    
for abc in range(40000):
    frame += [(abc//25)%256]
    
pal[0] = 0;
pal[1] = 250;
pal[2] = 0;
pal[765] = 250;
pal[766] = 0;
pal[767] = 250;

def write_frame(x, y):
    reset()
    
    pixel_idx = x+y*320
    
    qspi(cs0, 0b1000_0010, [(pixel_idx >> 12) & 0xFF, (pixel_idx >> 4) & 0xFF, (pixel_idx & 0xF) << 4]+frame, 0)
    
    
def write_line(y, color):
    reset()
    
    pixel_idx = y*320
    
    line = []
    
    for abc in range(320):
        line += [color]
    
    qspi(cs0, 0b1000_0010, [(pixel_idx >> 12) & 0xFF, (pixel_idx >> 4) & 0xFF, (pixel_idx & 0xF) << 4]+line, 0)
    
def write_line2(y, color):
    reset()
    
    pixel_idx = y*320
    
    line = []
    
    for abc in range(320):
        line += [color]
        
    line[0] = 0;
    line[319] = 255;
    
    qspi(cs0, 0b1000_0010, [(pixel_idx >> 12) & 0xFF, (pixel_idx >> 4) & 0xFF, (pixel_idx & 0xF) << 4]+line, 0)
    
def write_gradient(x, y):
    reset()
    
    pixel_idx = x+y*320
    
    line = []
    
    for abc in range(256):
        line += [abc]
    
    qspi(cs0, 0b1000_0010, [(pixel_idx >> 12) & 0xFF, (pixel_idx >> 4) & 0xFF, (pixel_idx & 0xF) << 4]+line, 0)   
    
def loopa():
    global globalSkipSteps
    
    globalSkipSteps = True
    cur_color = 50;
    
    for cur_y in range(240):
        write_line(cur_y, cur_color)
        cur_color = 0 if cur_color == 255 else (cur_color + 1)
         
    globalSkipSteps = False
    
def poopa():
    global globalSkipSteps
    
    globalSkipSteps = True
    
    reset()
    qspi(cs0, 0b1000_0011, pal, 0)
    
    loopa()
    globalSkipSteps = True
    
    write_line2(0, 60)
    write_line(20, 50)
    write_gradient(5, 5)
    write_gradient(20, 103)
    
    globalSkipSteps = False