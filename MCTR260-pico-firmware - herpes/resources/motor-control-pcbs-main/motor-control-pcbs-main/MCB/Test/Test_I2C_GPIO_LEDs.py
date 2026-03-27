from time import sleep
import machine

i2c = machine.I2C(0, scl=machine.Pin(5), sda=machine.Pin(4))
i2cdevices = i2c.scan()
print(i2cdevices)

#write bank A make outputs
i2c.writeto_mem(0x20,0x00,b'\x00')
#write bank B make outputs
i2c.writeto_mem(0x20,0x01,b'\x00')

i2c.writeto_mem(0x20,0x13,b'\x00') # 0x55 to turn yellow
i2c.writeto_mem(0x20,0x12,b'\x01') # 0x05 to turn yellow and LED bar 1 on

#write bank A make outputs
i2c.writeto_mem(0x21,0x00,b'\x00')
#write bank B make outputs
i2c.writeto_mem(0x21,0x01,b'\x00')

while(1):
  #write bank B upper bits to 1
  i2c.writeto_mem(0x21,0x13,b'\xF0')
  i2c.writeto_mem(0x21,0x12,b'\xF0')
  sleep(1.5)
  i2c.writeto_mem(0x21,0x13,b'\x00')
  i2c.writeto_mem(0x21,0x12,b'\x00')
  sleep(1.5)


