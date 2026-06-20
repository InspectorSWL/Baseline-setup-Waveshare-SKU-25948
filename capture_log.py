import serial
import time

ser = serial.Serial('COM3', 115200)
print('Capturing boot log from COM3...')
time.sleep(1)
output = ''

for i in range(200):  # Try 200 iterations of 50ms = 10 seconds
    if ser.in_waiting:
        chunk = ser.read(ser.in_waiting)
        output += chunk.decode('utf-8', errors='ignore')
    time.sleep(0.05)

ser.close()
print(output)
