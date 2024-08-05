from machine import Pin
import time

SPI_CS0 = 41
SPI_CS1 = 39
SPI_SCLK = 2
SPI_D0 = 5
SPI_D1 = 7
SPI_D2 = 16
SPI_D3 = 18

WRITE_DUMMY = 2

cs0 = Pin(SPI_CS0, mode=Pin.OUT, value=True)
cs1 = Pin(SPI_CS1, mode=Pin.OUT, value=True)
sclk = Pin(SPI_SCLK, mode=Pin.OUT, value=False)

def reset():
    sclk(0)
    cs0(1)
    cs1(1)
    
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
    
    d3((command & 0b1000_0000) != 0)
    d2((command & 0b0100_0000) != 0)
    d1((command & 0b0010_0000) != 0)
    d0((command & 0b0001_0000) != 0)
    sclk(1)
    
    if not skipSteps: print(f'command[7:4]: {d0()}{d1()}{d2()}{d3()}')
    skipSteps = step(skipSteps)
    
    sclk(0)

    d3((command & 0b0000_1000) != 0)
    d2((command & 0b0000_0100) != 0)
    d1((command & 0b0000_0010) != 0)
    d0((command & 0b0000_0001) != 0)
    sclk(1)
    
    if not skipSteps: print(f'command[3:0]: {d0()}{d1()}{d2()}{d3()}')
    skipSteps = step(skipSteps)
    
    for sendByte in sendData:
        sclk(0)
        
        if not skipSteps: print('<output edge>')
        skipSteps = step(skipSteps)
        
        d3((sendByte & 0b1000_0000) != 0)
        d2((sendByte & 0b0100_0000) != 0)
        d1((sendByte & 0b0010_0000) != 0)
        d0((sendByte & 0b0001_0000) != 0)
        sclk(1)
        
        if not skipSteps: print(f'send[7:4]: {d0()}{d1()}{d2()}{d3()}')
        skipSteps = step(skipSteps)
        
        sclk(0)
        
        if not skipSteps: print('<output edge>')
        skipSteps = step(skipSteps)

        d3((sendByte & 0b0000_1000) != 0)
        d2((sendByte & 0b0000_0100) != 0)
        d1((sendByte & 0b0000_0010) != 0)
        d0((sendByte & 0b0000_0001) != 0)
        sclk(1)
        
        if not skipSteps: print(f'send[3:0]: {d0()}{d1()}{d2()}{d3()}')
        skipSteps = step(skipSteps)
            
    d0 = Pin(SPI_D0, mode=Pin.IN)
    d1 = Pin(SPI_D1, mode=Pin.IN)
    d2 = Pin(SPI_D2, mode=Pin.IN)
    d3 = Pin(SPI_D3, mode=Pin.IN)
    
    sclk(0)
    
    dummy = WRITE_DUMMY
    
    while dummy > 0:
        sclk(1)
        sclk(0)
        dummy = dummy - 1
    
    if not skipSteps: print('output edge')
    
    #print(f'prereceive[3:0]: {d0()}{d1()}{d2()}{d3()}')
    #skipSteps = step(skipSteps)
    
    for i in range(receiveLength):
        currentByte = 0;
                
        #if (i>765):
        #    print(i)
        #    input()
        
        #skipSteps = step(skipSteps)
        
        sclk(1)
        currentByte |= d3() << 7;
        currentByte |= d2() << 6;
        currentByte |= d1() << 5;
        currentByte |= d0() << 4;
        
        #if not skipSteps: print(f'receive[3:0]: {d0()}{d1()}{d2()}{d3()}')
        
        sclk(0)
        
        skipSteps = step(skipSteps)
        
        sclk(1)
        currentByte |= d3() << 3;
        currentByte |= d2() << 2;
        currentByte |= d1() << 1;
        currentByte |= d0();
        
        #if not skipSteps: print(f'receive[3:0]: {d0()}{d1()}{d2()}{d3()}')
        if not skipSteps: print(f'read: {currentByte:#010b}')
        
        sclk(0)
        
        ret.append(currentByte)
        
    cs(1)
    
    return ret

