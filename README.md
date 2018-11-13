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

![ESP8266](/images/ESP8266-Pinout.png)


## Initial setup

Initial setup (i.e. connecting to the WiFi network) is done via the serial connection.
Connect to the module's serial port at 9600 baud 8N1. After powering on, the module
will show a list of visible WiFi networks and ask to enter the SSID and password
to connect.

After the information is entered, the module will attempt to connect to the network
and display its IP address if successful. The module is now fully active, using
9600 baud 8N1 on the serial port. To change the serial parameters, connect to
the module's web server (see below).
