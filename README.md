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

Pin | Name  | Connect to
----|-------|--------------------------
1   | GND   | Ground
2   | TX    | RX (serial input) of connected device
3   | GPIO2 | --> LED --> 150+ Ohm Resistor --> Ground
4   | CH_EN | --> 10k Resistor --> +3.3V
5   | GPIO0 | --> 10k Resistor --> +3.3V
6   | RESET | --> 10k Resistor --> +3.3V
7   | RX    | TX (serial output) of connected device
8   | VCC   | +3.3V

The LED on pin 3 (GPIO2) is not required but helpful since it displays the WiFi
connection status: Off means "not connected", Blinking means "connecting", On means "connected".

If you would like to be able to reset the module without turning off power then
connect pin 6 (RESET) via a pushbutton to ground (keep the resistor though).

## Initial setup

Initial setup (i.e. connecting to the WiFi network) is done via the serial connection.
Connect to the module's serial port at 9600 baud 8N1. After powering on, the module
will show a list of visible WiFi networks and ask to enter the SSID and password
to connect.

After the information is entered, the module will attempt to connect to the network
and display its IP address if successful. The module is now fully active, using
9600 baud 8N1 on the serial port. To change the serial parameters, connect to
the module's web server (see below).

## Changing the WiFi network connection

Once the WiFi setup is completed, the module will automatically attempt to re-connect
to the network when it boots up. The LED will blink while connecting. If a connection
can not be established, the module automatically goes into network configuration mode
(same as during initial setup).

If you wish to re-configure the WiFi network (i.e. select a different network), send
an ESC (27) character over the serial port while the module is attempting to connect.
Doing so will cause the module to go into network configuration mode.

Note that network configuration is ALWAYS done at 9600 baud, 8N1 regardless of what
the WiFi parameters were set to before.

## Configuring the serial connection

By default the module uses 9600 baud 8N1 on its serial connection. To modify the serial
parameter, the module provides a (minimal) web server on port 80. After the module
connects to WiFi it will display its IP address on the serial port. Using a web
browser, connect to that address (port 80).

The following options can be configured:

- Baud rate: a number of pre-defined baud rates are given. You can set a custom
  rate by going to the following URL: http://[module-ip-address]/set?baud=[rate]
  where "rate" is the desired baud rate.
- Data bits (5-8)
- Parity (none, even, odd)
- Stop bits (1 or 2)
- Show WiFi connection information: enable or disable whether or not the module
  will send out it's IP address on the serial connection after booting up.
- Telnet negotiation: the Telnet protocol includes a negotiation protocol to
  agree on some terminal options. The module can either handle this for you
  in a very basic way (will basically say "no" to any requested option) or pass
  the negotiation through to the device connected on the serial port.
  Not handling the Telnet negotiation may show some weird characters when connecting
  some BBSs. Some may also stop communicating at all after connecting.
  The "Ask client to disable local echo" will, when enabled, send a "will echo" message to
  a telnet client after it connects. This is necessary to turn off local echo for some
  telnet clients (local echo enabled causes each character typed to be printed twice).

## Telnet server function

After the module has connected to WiFi you can use any Telnet client to connect
to the module at port 23 (remember that the module will print it's IP address
on the serial port after connecting).

Up to 3 telnet clients can connect. Input from each client will appear at the module's
TX serial pin and all data coming in on the module's RX serial pin will be forwarded
to all telnet clients.

## Modem emulation function

The module emulates a Hayes-compatible modem with a [basic command set](https://en.wikipedia.org/wiki/Hayes_command_set#The_basic_Hayes_command_set). 

By default the module will send and receive its information as fast as the serial
connection baud rate allows. However, if you want the true nostalgic feel of a slow
modem connection, you can limit the speed by setting the "Desired Telco Line Speed"
register (S37). **Before** dialing, issue the command "AT S37=N" where N is an index defining the desired baud rate:
0=auto (default), 1=75, 2=110, 3=300, 4=600, 5=1200, 6=2400, 7=4800, 8=7200, 9=9600, 10=12000, 11=14400

If the baud rate set in S37 is set to "auto" or is higher than the serial port then the 
serial port speed is used. 

The serial baud rate can be temporarily changed by sending "AT#BDR=N" where N is an index
defining the new baud rate: baud=2400 * N (N=0 selects the baud rate configured via the web server). 
The module will respond with OK at the previous baud rate and then switch to the new baud rate.

Use ATD, ATDT or ATDP to "dial" a number. The module understands the following formats for
the number following the ATD command:

- ATDT hostname.com[:port]. Connect to hostname.com. 
- ATDT n.n.n.n[:port]. Connect to IP addres n.n.n.n. 
  Any non-digit can be used as the separator instead of "." or ":".
- ATDT aaabbbcccddd[pppp]. Connect to IP address aaa.bbb.ccc.ddd[:pppp]. This is useful for some
  vintage terminal programs that expect a phone number to only contain digits.

If the port argument is omitted in either of these then it defaults to 23.

Since there are only RX/TX pins on the ESP8266, there are no DSR/DTR or CTS/RTS 
hardware control signals. However, the module does understand the "+++" sequence to switch
into command mode during a call.
