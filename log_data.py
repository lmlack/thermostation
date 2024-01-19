import serial
import re
import matplotlib.pyplot as plt

# Connect to serial port and discard the first line, which may contain partial
# data and is maybe invalid.
ser = serial.Serial()
ser.port = '/dev/ttyACM0'
ser.baudrate = 115200
#ser.timeout = 5
ser.open()
print(ser.is_open)
_ = ser.readline() 

# Set up lists to collect samples.
ext_data = [[],[],[],[]]
int_data = {
        'fsr': [],
        'at': [],
        'pt': [],
}

while ser.is_open == True:
    try:
        data = ser.readline().decode('utf-8')
        print(data)
        vals = data.split(',')
        sample_type = vals[0]
        if sample_type == 'int':
            int_data['fsr'].append(float(vals[1]))
            int_data['at'].append(float(vals[2]))
            int_data['pt'].append(float(vals[3]))
        elif sample_type == 'ext':
            channel = int(vals[1])
            sample = int(vals[2])
            ext_data[channel].append(sample)
    except KeyboardInterrupt:
        fig, axs = plt.subplots(7,1)
        for ch in range(4):
            axs[ch].plot(ext_data[ch])
        axs[4].plot(int_data['fsr'])
        axs[5].plot(int_data['at'])
        axs[6].plot(int_data['pt'])
        plt.show()
        break
