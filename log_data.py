import collections
import matplotlib.pyplot as plt
import numpy as np
import re
import serial
import time
import multiprocessing
import queue

# Hardcoded list of metric names
METRIC_NAMES = [
    'EXT ADC 0',
    'EXT ADC 1',
    'EXT ADC 2',
    'EXT ADC 3',
    'ACCEL X',
    'ACCEL Y',
    'ACCEL Z',
    'GYRO X',
    'GYRO Y',
    'GYRO Z',
    'ACTIVE THERM',
    'PASSIVE THERM',
    'FSR',
]

# Simple wrapper around collections.deque to maintain a sliding window of the
# last `size` samples (a time and value tuple) and plot these samples onto a
# matplotlib axis.
class MetricStream:
    def __init__(self, name, size):
        self.name = name
        self.size = size
        self.buffer = collections.deque(maxlen=size)

    def write(self, timestamp, value):
        self.buffer.append((timestamp, value))

    def plot(self, ax):
        a = np.array(self.buffer)

        # When starting up we may not have yet received samples for all
        # metrics, so just ignore plotting requests if we are empty.
        if len(a.shape) == 1:
            return

        ax.clear()
        ax.set_title(self.name)
        ax.plot(a[:,1])

def decode_event_str(metrics, event_str):
    # Events are serialized as a simple comma separated string.
    elements = event_str.split(',')

    # If there aren't enough values to decode, this event is malformed.
    if len(elements) < 3:
        print(f'Couldnt decode event str {event_str}')
        return

    # Extract the type and timestamp, the first two elements of any event.
    event_type = int(elements[0])
    timestamp_s = int(elements[1])/1000000

    # The fields of the event data are the remaining elements, these have
    # variable type and length depending on the event type.
    fields = elements[2:]

    if event_type == 0:
        if len(fields) != 2:
            return

        # EXT_ADC event, log to corresponding channel's stream
        channel = int(fields[0])
        metric_name = f'EXT ADC {channel}'
        metrics[metric_name].write(timestamp_s, int(fields[1]))
    elif event_type == 1:
        # IMU event, log to all IMU data streams
        #
        # All fields are floats, preconvert to floats and unpack into
        # components.
        if len(fields) != 6:
            return

        fields = map(float, fields)
        ax, ay, az, gx, gy, gz = fields

        # Manually log to all streams, there's definitely a cleaner way to do
        # this but this is easily understandable and simple.
        metrics['ACCEL X'].write(timestamp_s, ax)
        metrics['ACCEL Y'].write(timestamp_s, ay)
        metrics['ACCEL Z'].write(timestamp_s, az)
        metrics['GYRO X'].write(timestamp_s, gx)
        metrics['GYRO Y'].write(timestamp_s, gy)
        metrics['GYRO Z'].write(timestamp_s, gz)
    elif event_type == 2:
        if len(fields) != 3:
            return

        # Resistive sensors event, same as above, unpack floats fields.
        fields = map(float, fields)
        at, pt, fsr = fields

        # Manually log to all streams, there's definitely a cleaner way to do
        # this but this is easily understandable and simple.
        metrics['ACTIVE THERM'].write(timestamp_s, at)
        metrics['PASSIVE THERM'].write(timestamp_s, pt)
        metrics['FSR'].write(timestamp_s, fsr)

# Creates a dict of metrics that map from the given name to a MetricStream of
# the same name. This dict is a nice way to access a collection of name metric
# streams, addressing them by name.
#
# Also returns a mapping of metric names to indexes used for plotting - since
# dict iteration order is not guaranteed, this helps maintain stable plotting
# order.
def init_metrics(names, size):
    metrics = {}
    metric_idxs = {}
    for idx, name in enumerate(names):
        metrics[name] = MetricStream(name, size)
        metric_idxs[name] = idx
    return metrics, metric_idxs


# It is important to not drop data, and the serial rx buffer is small, and the
# plotting is slow... What we can do is read from the serial port in a separate
# process and just stash the raw messages in a really large queue, then process
# them in bulk in the main loop. This will help ensure that even if the main
# loop is busy drawing plots, the other process can service the serial
# connection and just buffer the received data.
def serial_process(port, msg_q):
    print(f'Opening serial port {port}')
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    #ser.timeout = 5
    ser.open()
    if not ser.is_open:
        print(f'ERROR - Failed to open {port}')
        return

    # Discard the first line, which may be partially complete.
    _ = ser.readline() 

    # Buffer messages until the end of time. Or the process ends. Whichever
    # comes first.
    while True:
        try:
            data = ser.readline().decode('utf-8')
            # print(data)
            msg_q.put(data)
        except KeyboardInterrupt:
            break


# Takes all available items from the queue.
def drain_queue(q):
    items = []
    while True:
        try:
            items.append(q.get(block=False))
        except queue.Empty:
            break
    return items


# Get port from args
#
# TODO: argparse is way nicer
import sys
port = sys.argv[1]

# Connect to serial port and discard the first line, which may contain partial
# data and is maybe invalid.
# Initialize metric streams
metrics, metric_idxs = init_metrics(METRIC_NAMES, 2*500)
num_metrics = len(metrics)

# Create plots for live plotting
fig, axs = plt.subplots(num_metrics,1)
plt.show(block=False)

# Create a message queue and start the background serial reader process
event_q = multiprocessing.Queue()
p = multiprocessing.Process(target=lambda: serial_process(port, event_q), daemon=True)
p.start()

# Now drain the queue and draw as fast as possible
while True:
    try:
        items = drain_queue(event_q)
        if len(items) == 0:
            time.sleep(0.001)
            continue

        print(f'Read {len(items)} from queue!')
        for msg in items:
            decode_event_str(metrics, msg)
        for name, stream in metrics.items():
            stream.plot(axs[metric_idxs[name]])
        fig.canvas.draw()
        fig.canvas.flush_events()

    except KeyboardInterrupt:
        break
