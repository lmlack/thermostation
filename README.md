## High level TODO
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
