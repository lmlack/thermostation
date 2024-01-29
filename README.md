## Setup
### Repo setup
This repo uses a git submodule for the RP2040 SD SPI library. When cloning the
repo, make sure to clone with `--recurse-submodules` like:
```shell
$ git clone --recurse-submodules -j8 git@github.com:lmlack/thermostation.git
```

or if you have already cloned and forgot about the submodules, just run
```shell
$ git submodule update --init --recursive
```

### Build
This project, like all rpi pico C sdk projects, uses cmake to build. To build
with cmake, create a directory for all the cmake outputs (usually called build)
and run cmake while passing it the directory containing the CMakeLists.txt.
Then make can be run on the generate makefile to build the project:
```shell
$ mkdir build && cd build
$ cmake ../
$ make
```

### Flash
To flash on the firmware, there are many options. There are plenty of rpi pico
flashing tutorials out there, but the simplest options are to reboot the pico
while holding the boot button to put it into bootloader mode, and then copy the
uf2 file created in build/ by the build process to the USB mass storage device
for the pico that appears in bootloader mode, or install and use picotool:
```shell
$ make && picotool load hp_test.uf2 -f
```

### Run python data streamer
The repo includes a python program that deserializes and plots the data in
real-ish time (TODO: optimize matplotlib drawing by caching background and
blitting), using multiple processes to help ensure no dropped samples.

This serves as a good example of how to read and deserialize the data stream.

To run the plotting program, you'll need to install the following python
package prerequisites (TODO: add requirements.txt or Pipfile or something):
* pyserial
* numpy
* matplotlib

Once that's done, run the program like any other python program, passing one
positional command line arg, the path to the serial port (TODO: add better args
with argparse):
```shell
$ python3 log_data.py /dev/ttyACM0
```

## High level TODO
### SD card logging
Although the SD card hardware works, the SD logger isn't quite done yet, but it
should be easy to wrap up. We just have to be careful to do the initialization
and set up the DMA interrupts on the core that we want to be doing the file IO.

### Per board calibration
Each board + sensor combo will probably need calibration for the
most accurate results. We can write a calibration routine of some
sort and store the calibration constants into a file on the board's
SD card. Then we can make backups of the calibration iof the
procedure is time consuming, and load the file on startup each time
to ensure the boards can make accurate measurements.

### Spurious connection handling
It's possible that without locking connectors the connectors will
shake around and transiently break connection during operation.  We
can dynamically detect IMU I2C connectivity problems and
discontinuities in the resistive sensor values and try to deal with
these errors by discarding bad samples and re-trying failed I2C
transactions, etc

### Improved host control and timestamping
Right now the host<->device serial communication is one-way, the device spews
events as fast as they are generated. There could easily be some kind of simple
serial console where the host can send commands to do things like, just as a
few examples:
* start and stop the event stream being written to the serial port
* start and stop the event stream being written to the SD card
* set SD data log file names
* disable logging of unused external ADC channels
* set UTC timestamp

The last one would be cool, currently since there is no RTC module the device
only timestamps events with microseconds since boot, but if the host could set
the UTC time before starting the logging, the device could use UTC timestamps
for event timestamps and logfile names, etc.
