# WifiModem

This is a firmware for the ESP8266 module that connects a serial port to Wifi.
It does so in two ways:

1) A computer attached to the serial port can "talk" to the module as if it
   were a Hayes-compatible modem (AT commands).  It can "dial" an IP address
   or hostname and the module will connect to that host via the Telnet protocol.
   In effect this allows vintage computers to dial in to telnet BBSs.
   
2) A computer on the WiFi network can connect to the module (port 23) via Telnet
   and the module will pass all communication through between the serial connection
   and the Telnet connection. This allows connecting to a (vintage) computer via WiFi.

Note that the two functions are exclusive: if an outside computer has connected
to the Telnet server (function 2) then the modem emulation is not available as 
all AT commands will be passed through to the connected computer. If a modem
connection is active then the Telnet server is disabled.

## Wiring the ESP8266

I made this for a ESP-01 module (with the 8-pin header) but it should not be hard
to adapt the wiring for other versions.

![ESP8266](/images/ESP8266-Pinout.png)

Pin | Name  | Wire to
---------------------------------------
1   | GND   | Ground
2   | TX    | RX (serial input) of connected device
3   | GPIO2 | --> LED --> 150+ Ohm Resistor --> Ground
4   | CH_EN | --> 10k Resistor -> +3.3V
5   | GPIO0 | --> 10k Resistor -> +3.3V
6   | RESET | --> 10k Resistor -> +3.3V
7   | RX    | TX (serial output) of connected device
8   | VCC   | +3.3V

## Initial setup

Initial setup (i.e. connecting to the WiFi network) is done via the serial connection.
Connect to the module's serial port at 9600 baud 8N1. After powering on, the module
will show a list of visible WiFi networks and ask to enter the SSID and password
to connect.

After the information is entered, the module will attempt to connect to the network
and display its IP address if successful. The module is now fully active, using
9600 baud 8N1 on the serial port. To change the serial parameters, connect to
the module's web server (see below).
