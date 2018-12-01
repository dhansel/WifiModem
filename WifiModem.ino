// -----------------------------------------------------------------------------
// WiFi Modem and Telnet Server
// Copyright (C) 2018 David Hansel
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
// -----------------------------------------------------------------------------

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>


//how many clients should be able to telnet to this ESP8266
#define MAX_SRV_CLIENTS 3

WiFiServer server(23);
WiFiClient serverClients[MAX_SRV_CLIENTS], modemClient;
ESP8266WebServer webserver(80);
unsigned long prevCharTime = 0;
uint8_t modemEscapeState = 0, modemExtCodes = 0, modemReg[255];
bool    modemCommandMode = true, modemEcho = true, modemQuiet = false, modemVerbose = true;


static int linespeeds[] = {0, 75, 110, 300, 600, 1200, 2400, 4800, 7200, 9600, 12000, 14400};
#define NSPEEDS (sizeof(linespeeds)/sizeof(int))

#define LED_PIN 2

#define REG_ESC            2
#define REG_CR             3
#define REG_LF             4
#define REG_BSP            5
#define REG_GUARDTIME     12
#define REG_LINESPEED     37
#define REG_CURLINESPEED  43

enum
  {
    E_OK = 0,
    E_CONNECT,
    E_RING,
    E_NOCARRIER,
    E_ERROR,
    E_CONNECT1200,
    E_NODIALTONE,
    E_BUSY,
    E_NOANSWER,
    E_CONNECT600,
    E_CONNECT2400,
    E_CONNECT4800,
    E_CONNECT9600,
    E_CONNECT14400,
    E_CONNECT19200
  };


struct TelnetStateStruct
{
  uint8_t cmdLen;
  uint8_t cmd[4];
  bool sendBinary;
  bool receiveBinary;
  bool receivedCR;
  bool subnegotiation;
} modemTelnetState, clientTelnetState[MAX_SRV_CLIENTS];


#define MAGICVAL 0xF0E1D2C3B4A59687
struct WiFiDataStruct
{
  uint64_t magic;
  char     ssid[256];
  char     key[256];
} WifiData;


struct SerialDataStruct
{
  uint64_t magic;
  uint32_t baud;
  byte     bits;
  byte     parity;
  byte     stopbits;
  byte     silent;
  byte     handleTelnetProtocol;
  char     telnetTerminalType[100];
} SerialData;


SerialConfig GetSerialConfig()
{
  if( SerialData.bits==5 && SerialData.parity==0 && SerialData.stopbits==1 )
    return SERIAL_5N1;
  else if( SerialData.bits==5 && SerialData.parity==0 && SerialData.stopbits==2 )
    return SERIAL_5N2;
  else if( SerialData.bits==5 && SerialData.parity==1 && SerialData.stopbits==1 )
    return SERIAL_5E1;
  else if( SerialData.bits==5 && SerialData.parity==1 && SerialData.stopbits==2 )
    return SERIAL_5E2;
  else if( SerialData.bits==5 && SerialData.parity==2 && SerialData.stopbits==1 )
    return SERIAL_5O1;
  else if( SerialData.bits==5 && SerialData.parity==2 && SerialData.stopbits==2 )
    return SERIAL_5O2;
  else if( SerialData.bits==6 && SerialData.parity==0 && SerialData.stopbits==1 )
    return SERIAL_6N1;
  else if( SerialData.bits==6 && SerialData.parity==0 && SerialData.stopbits==2 )
    return SERIAL_6N2;
  else if( SerialData.bits==6 && SerialData.parity==1 && SerialData.stopbits==1 )
    return SERIAL_6E1;
  else if( SerialData.bits==6 && SerialData.parity==1 && SerialData.stopbits==2 )
    return SERIAL_6E2;
  else if( SerialData.bits==6 && SerialData.parity==2 && SerialData.stopbits==1 )
    return SERIAL_6O1;
  else if( SerialData.bits==6 && SerialData.parity==2 && SerialData.stopbits==2 )
    return SERIAL_6O2;
  else if( SerialData.bits==7 && SerialData.parity==0 && SerialData.stopbits==1 )
    return SERIAL_7N1;
  else if( SerialData.bits==7 && SerialData.parity==0 && SerialData.stopbits==2 )
    return SERIAL_7N2;
  else if( SerialData.bits==7 && SerialData.parity==1 && SerialData.stopbits==1 )
    return SERIAL_7E1;
  else if( SerialData.bits==7 && SerialData.parity==1 && SerialData.stopbits==2 )
    return SERIAL_7E2;
  else if( SerialData.bits==7 && SerialData.parity==2 && SerialData.stopbits==1 )
    return SERIAL_7O1;
  else if( SerialData.bits==7 && SerialData.parity==2 && SerialData.stopbits==2 )
    return SERIAL_7O2;
  else if( SerialData.bits==8 && SerialData.parity==0 && SerialData.stopbits==1 )
    return SERIAL_8N1;
  else if( SerialData.bits==8 && SerialData.parity==0 && SerialData.stopbits==2 )
    return SERIAL_8N2;
  else if( SerialData.bits==8 && SerialData.parity==1 && SerialData.stopbits==1 )
    return SERIAL_8E1;
  else if( SerialData.bits==8 && SerialData.parity==1 && SerialData.stopbits==2 )
    return SERIAL_8E2;
  else if( SerialData.bits==8 && SerialData.parity==2 && SerialData.stopbits==1 )
    return SERIAL_8O1;
  else if( SerialData.bits==8 && SerialData.parity==2 && SerialData.stopbits==2 )
    return SERIAL_8O2;
  else
    return SERIAL_8N1;
}


void applySerialSettings()
{
  Serial.flush();
  Serial.end();
  delay(100);
  Serial.begin(SerialData.baud, GetSerialConfig());
}


const int   baud[]   = {110, 150, 300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 256000, 512000, 921600, 0};
const int   bits[]   = {5, 6, 7, 8, 0};
const char *parity[] = {"No parity", "Even parity", "Odd parity", NULL};
const char *stop[]   = {"One stop bit", "Two stop bits", NULL};


void handleRoot() 
{
  char buffer2[100];
  String s;
  int i;

  s = ("<html>\n"
       "<head>\n"
       "<title>ESP8266 Telnet-to-Serial Bridge</title>\n"
       "</head>\n"
       "<body>\n"
       "<h1>ESP8266 Telnet-to-Serial Bridge</h1>\n");

  s += "<h2>Baud rate</h1>\n<ul>\n";
  for(i=0; baud[i]; i++)
    {
      if( (i==0 || baud[i-1]<SerialData.baud) && baud[i] > SerialData.baud )
        {
          snprintf(buffer2, 100, "<li><b>%i baud</b></li>", SerialData.baud);
          s += String(buffer2);
        }
      
      if( baud[i] == SerialData.baud )
        snprintf(buffer2, 100, "<li><b>%i baud</b></li>", baud[i]);
      else 
        snprintf(buffer2, 100, "<li><a href=\"set?baud=%i\">%i baud</a></li>", baud[i], baud[i]);
      
      s += String(buffer2);
    }


  s += "</ul>\n<h2>Data bits</h2>\n<ul>\n";
  for(i=0; bits[i]; i++)
    {
      if( SerialData.bits == bits[i] )
        snprintf(buffer2, 100, "<li><b>%i bits</b></li>", bits[i]);
      else 
        snprintf(buffer2, 100, "<li><a href=\"set?bits=%i\">%i bits</a></li>", bits[i], bits[i]);
      
      s += String(buffer2);
    }


  s += "</ul>\n<h2>Parity</h2>\n<ul>\n";
  for(i=0; parity[i]; i++)
    {
      if( SerialData.parity == i )
        snprintf(buffer2, 100, "<li><b>%s</b></li>", parity[i]);
      else
        snprintf(buffer2, 100, "<li><a href=\"set?parity=%c\">%s</a></li>", parity[i][0], parity[i]);

      s += String(buffer2);
    }

  s += "</ul>\n<h2>Stop bits</h2>\n<ul>\n";
  for(i=0; stop[i]; i++)
    {
      if( SerialData.stopbits == i+1 )
        snprintf(buffer2, 100, "<li><b>%s</b></li>", stop[i]); 
      else 
        snprintf(buffer2, 100, "<li><a href=\"set?stopbits=%i\">%s</a></li>", i+1, stop[i]);

      s += String(buffer2);
    }

  s += "</ul>\n<h2>Show WiFi connection information</h2>\n<ul>\n";
  if( SerialData.silent )
    {
      s += "<li><a href=\"set?silent=no\">Print to serial port after connecting</a></li>\n";
      s += "<li><b>Do not show</b></li>\n";
    }
  else
    {
      s += "<li><b>Print to serial port after connecting</b></li>\n";
      s += "<li><a href=\"set?silent=yes\">Do not show</a></li>\n";
    }
  
  s += "</ul>\n<h2>Telnet protocol</h2>\n<ul>\n";
  if( SerialData.handleTelnetProtocol )
    {
      int i;
      const char *terminalTypes[] = {"none", "ansi", "teletype-33", "unknown", "vt100", "xterm", NULL};

      s += "<li><b>Handle</b> - Report terminal type: ";
      bool found = false;
      for(i=0; terminalTypes[i]!=NULL; i++)
        {
          if( i>0 ) s += ", ";
          if( strcmp(terminalTypes[i], SerialData.telnetTerminalType)==0 || (i==0 && SerialData.telnetTerminalType[0]==0) )
            { s += "<b>" + String(terminalTypes[i]) + "</b>"; found = true; }
          else
            {
              snprintf(buffer2, 100, "<a href=\"set?telnetTerminalType=%s\">%s</a>", terminalTypes[i], terminalTypes[i]);
              s += String(buffer2);
            }
        }

      if( !found ) s += ", <b>" + String(SerialData.telnetTerminalType) + "</b>";
      s += "</li>\n";
      s += "<li><a href=\"set?filterTelnet=no\">Pass through</a></li>\n";
    }
  else
    {
      s += "<li><a href=\"set?filterTelnet=yes\">Handle</a></li>\n";
      s += "<li><b>Pass through</b></li>\n";
    }
  
  s += "</ul>\n</body>\n</html>";
  webserver.send(200, "text/html", s);
}


void handleSet()
{
  bool ok = webserver.args()>0;
  for(int i=0; i<ok && webserver.args(); i++)
    {
      if( webserver.argName(i) == "baud" )
        SerialData.baud = atoi(webserver.arg(i).c_str());
      else if( webserver.argName(i) == "bits" )
        {
          byte bits = atoi(webserver.arg(i).c_str());
          if( bits>=5 && bits<=8 )
            SerialData.bits = bits;
          else
            ok = false;
        }
      else if( webserver.argName(i) == "parity" && webserver.arg(i)=="N" )
        SerialData.parity = 0;
      else if( webserver.argName(i) == "parity" && webserver.arg(i)=="E" )
        SerialData.parity = 1;
      else if( webserver.argName(i) == "parity" && webserver.arg(i)=="O" )
        SerialData.parity = 2;
      else if( webserver.argName(i) == "stopbits" && webserver.arg(i)=="1" )
        SerialData.stopbits = 1;
      else if( webserver.argName(i) == "stopbits" && webserver.arg(i)=="2" )
        SerialData.stopbits = 2;
      else if( webserver.argName(i) == "silent" && webserver.arg(i)=="yes" )
        SerialData.silent = true;
      else if( webserver.argName(i) == "silent" && webserver.arg(i)=="no" )
        SerialData.silent = false;
      else if( webserver.argName(i) == "filterTelnet" && webserver.arg(i)=="log" )
        SerialData.handleTelnetProtocol = 2;
      else if( webserver.argName(i) == "filterTelnet" && webserver.arg(i)=="yes" )
        SerialData.handleTelnetProtocol = 1;
      else if( webserver.argName(i) == "filterTelnet" && webserver.arg(i)=="no" )
        SerialData.handleTelnetProtocol = 0;
      else if( webserver.argName(i) == "telnetTerminalType" )
        {
          int j;
          String ts = webserver.arg(i);
          for(j=0; j<99 && j<ts.length() && ts[j]>=32 && ts[j]<127; j++)
            SerialData.telnetTerminalType[j] = ts[j];
          SerialData.telnetTerminalType[j] = 0;
        }
      else
        ok = false;
    }

  if( ok ) 
    {
      EEPROM.put(768, SerialData);
      EEPROM.commit();
      applySerialSettings();
    }

  handleRoot();
}


void handleNotFound() 
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += webserver.uri();
  message += "\nMethod: ";
  message += (webserver.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += webserver.args();
  message += "\n";

  for (uint8_t i = 0; i < webserver.args(); i++)
    message += " " + webserver.argName(i) + ": " + webserver.arg(i) + "\n";

  webserver.send(404, "text/plain", message);
}


void readString(char *buffer, int buflen, bool password)
{
  int len = 0;
  while( true )
  {
    if( Serial.available() )
      {
        char c = Serial.read();
        if( c==10 || c==13 )
          {
            buffer[len] = 0;
            return;
          }
        else if( (c==8 || c==127) && len>0 )
          {
            Serial.print(char(8)); Serial.print(' '); Serial.print(char(8));
            len--;
          }
        else if( c >= 32 && len<buflen-1 )
          {
            buffer[len++] = c;
            Serial.print(password ? '*' : c);
          }
      }
    else
      delay(10);
      
    yield();
  }
}


void clearSerialBuffer()
{
  // empty the serial buffer
  delay(100); 
  while( Serial.available()>0 ) { Serial.read(); delay(10); }
}


void GetWiFiData(const char *msg)
{
  clearSerialBuffer();

  bool go = true;
  while( go )
  {
     Serial.println(msg); 
     Serial.println("Press C to configure WiFi connection information.\n");
     
     unsigned long t = millis()+2000;
     while( millis()<t ) if( Serial.available()>0 && Serial.read()=='C' ) { go = false; break; }
     yield();
  }

  Serial.println("Scanning for networks...");

  WiFi.disconnect();
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("No networks found.");
  } else {
    Serial.print(n);
    Serial.println(" networks found:");
    for (int i = 0; i < n; ++i) {
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
      delay(10);
    }
  }
  Serial.println("\n");
  WiFi.disconnect();

  clearSerialBuffer();
  Serial.print("SSID: ");
  readString(WifiData.ssid, 256, false);
  Serial.println();

  clearSerialBuffer();
  Serial.print("Password: ");
  readString(WifiData.key, 256, true);
  Serial.println();

  WifiData.magic = MAGICVAL;
  SerialData.silent = false;
  EEPROM.put(0, WifiData);
  EEPROM.put(768, SerialData);
  EEPROM.commit();
}


void setup() 
{
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // start serial interface with setup parameters (9600 baud 8N1)
  Serial.begin(9600);

  // read serial info
  EEPROM.begin(1024);
  EEPROM.get(768, SerialData);
  if( SerialData.magic != MAGICVAL )
    {
      // use default settings
      SerialData.baud     = 9600;
      SerialData.bits     = 8;
      SerialData.parity   = 0;
      SerialData.stopbits = 1;
      SerialData.silent   = false;
      SerialData.handleTelnetProtocol = 1;
      SerialData.magic    = MAGICVAL;
      strcpy(SerialData.telnetTerminalType, "vt100");
      EEPROM.put(768, SerialData);
      EEPROM.commit();
    }
  else
    {
      // make sure terminal type string is ASCII and 0-terminated
      int i = 0;
      while(i<99 && SerialData.telnetTerminalType[i]>=32 && SerialData.telnetTerminalType[i]<127) i++;
      SerialData.telnetTerminalType[i]=0;
    }
  
  // read WiFi info
  WiFi.mode(WIFI_STA);
  EEPROM.get(0, WifiData);
  if( WifiData.magic != MAGICVAL ) 
    GetWiFiData("WiFi connection information not configured.");

  // start WiFi interface
  while( WiFi.status() != WL_CONNECTED )
  {
    WiFi.begin(WifiData.ssid, WifiData.key);
    uint8_t i = 0;

    // try to connect to WiFi
    while(WiFi.status() != WL_CONNECTED && i++ < 20 ) 
    {
      delay(250);
      digitalWrite(LED_PIN, HIGH); 
      delay(250);
      digitalWrite(LED_PIN, LOW); 
      if( Serial.available()>0 && Serial.read() == 27 ) break;
    }

    if( WiFi.status() != WL_CONNECTED )
    {
      char buffer[300];
      if( i == 21 )
        sprintf(buffer, "Could not connect to %s.", WifiData.ssid);
      else
        sprintf(buffer, "Received ESC during connect.");
        
      GetWiFiData(buffer);
    }
  }

  // if we get here then we're connected to WiFi
  digitalWrite(LED_PIN, HIGH); 

  // if normal operation is different from 9600 8N1 then print info now
  // (and again after switching)
  if( !SerialData.silent && (SerialData.baud!=9600 || GetSerialConfig()!=SERIAL_8N1) )
    {
      Serial.print("\nConnected to network "); Serial.println(WifiData.ssid);
      Serial.print("Listening on port 23 (telnet) at IP "); Serial.println(WiFi.localIP());
      Serial.flush();
    }

  // re-start serial interface with normal operation parameters
  Serial.end();
  Serial.begin(SerialData.baud, GetSerialConfig());

  if( !SerialData.silent )
  {
    Serial.println();
    Serial.print("\nConnected to network "); Serial.println(WifiData.ssid);
    Serial.print("Listening on port 23 (telnet) at IP "); Serial.println(WiFi.localIP());
    Serial.print("\nPress 's' to skip this information at future connects.");
    
    int n = 3;
    unsigned long t = millis()+1000;
    while( n>0 )
    {
      if( millis()>t ) { Serial.print('.'); n--; t = millis()+1000; }
      if( Serial.available()>0 && tolower(Serial.read())=='s' ) 
        {
          SerialData.silent = true;
          EEPROM.put(768, SerialData);
          EEPROM.commit();
          break; 
        }
    }

    Serial.println('\n');
  }

  MDNS.begin("esp8266");
  webserver.on("/", handleRoot);
  webserver.on("/set", handleSet);
  webserver.onNotFound(handleNotFound);
  webserver.begin();

  server.begin();
  server.setNoDelay(true);

  resetModemState();

  // flush serial input buffer
  while( Serial.available() ) Serial.read();
}


bool haveTelnetClient()
{
  for( int i=0; i<MAX_SRV_CLIENTS; i++ )
    if( serverClients[i] && serverClients[i].connected() )
      return true;

  return false;
}


void resetTelnetState(struct TelnetStateStruct &s)
{
  s.cmdLen = 0;
  s.sendBinary = false;
  s.receiveBinary = false;
  s.receivedCR = false;
  s.subnegotiation = false;
}


void resetModemState()
{
  const uint8_t regDefaults[] = {2, '+', 
                                 3, '\r', 
                                 4, '\n', 
                                 5, 8, 
                                 6, 2, 
                                 7, 50, 
                                 8, 2, 
                                 9, 6, 
                                 10, 14, 
                                 11, 95, 
                                 12, 50, 
                                 25, 5,
                                 38, 20};

  for(int i=0; i<256; i++) modemReg[i] = 0;
  for(int i=0; i<sizeof(regDefaults); i+=2) modemReg[regDefaults[i]] = regDefaults[i+1];

  modemEscapeState = 0;
  modemExtCodes = 0;
  modemCommandMode = true;
  modemEcho = true;
  modemQuiet = false;
  modemVerbose = true;
  
  if( modemClient.connected() ) modemClient.stop();
}


void printModemCR()
{
  Serial.print(char(modemReg[REG_CR]));
  if( modemVerbose ) Serial.print(char(modemReg[REG_LF]));
}


void printModemResult(byte code)
{
  if( !modemQuiet )
    {
      if( modemVerbose )
        {
          printModemCR();
          switch( code )
            {
            case E_OK            : Serial.print("OK");             break;
            case E_CONNECT       : Serial.print("CONNECT");        break;
            case E_RING          : Serial.print("RING");           break;
            case E_NOCARRIER     : Serial.print("NO CARRIER");     break;
            case E_ERROR         : Serial.print("ERROR");          break;
            case E_CONNECT1200   : Serial.print("CONNECT 1200");   break;
            case E_NODIALTONE    : Serial.print("NO DIALTONE");    break;
            case E_BUSY          : Serial.print("BUSY");           break;
            case E_NOANSWER      : Serial.print("NO ANSWER");      break;
            case E_CONNECT600    : Serial.print("CONNECT 600");    break;
            case E_CONNECT2400   : Serial.print("CONNECT 2400");   break;
            case E_CONNECT4800   : Serial.print("CONNECT 4800");   break;
            case E_CONNECT9600   : Serial.print("CONNECT 9600");   break;
            case E_CONNECT14400  : Serial.print("CONNECT 14400");  break;
            default:
              {
                char buf[20];
                sprintf(buf, "ERROR%i", code);
                Serial.print(buf);
                break;
              }
            }
        }
      else
        Serial.print(code);

      printModemCR();
    }
}


byte getCmdParam(char *cmd, int &ptr)
{
  byte res = 0;

  ptr++;
  if( isdigit(cmd[ptr]) )
    {
      int p = ptr;
      while( isdigit(cmd[ptr]) ) ptr++;
      char c = cmd[ptr];
      cmd[ptr] = 0;
      res = atoi(cmd+p);
      cmd[ptr] = c;
    }
  
  return res;
}


// returns TRUE if input byte "b" is part of a telnet protocol sequence and should
// NOT be passed through to the serial interface
bool handleTelnetProtocol(uint8_t b, WiFiClient &client, struct TelnetStateStruct &state)
{
#define T_SE      240 // 0xf0
#define T_NOP     241 // 0xf1
#define T_BREAK   243 // 0xf3
#define T_GOAHEAD 249 // 0xf9
#define T_SB      250 // 0xfa
#define T_WILL    251 // 0xfb
#define T_WONT    252 // 0xfc
#define T_DO      253 // 0xfd
#define T_DONT    254 // 0xfe
#define T_IAC     255 // 0xff

#define TO_SEND_BINARY        0
#define TO_ECHO               1
#define TO_SUPPRESS_GO_AHEAD  3
#define TO_TERMINAL_TYPE      24

  // if not handling telnet protocol then just return
  if( !SerialData.handleTelnetProtocol ) return false;

  // ---- handle incoming sub-negotiation sequences

  if( state.subnegotiation )
    {
      if( SerialData.handleTelnetProtocol>1 ) {Serial.print('<'); Serial.print(b, HEX);}

      if( state.cmdLen==0 && b == T_IAC )
        state.cmdLen = 1; 
      else if( state.cmdLen==1 && b == T_SE )
        {
          state.subnegotiation = false;
          state.cmdLen = 0;
        }
      else
        state.cmdLen = 0;

      return true;
    }

  // ---- handle CR-NUL sequences

  if( state.receivedCR )
    {
      state.receivedCR = false;
      if( b==0 )
        {
          // CR->NUL => CR (i.e. ignore the NUL)
          if( SerialData.handleTelnetProtocol>1 ) Serial.print("<0d<00");
          return true;
        }
    }
  else if( b == 0x0d && state.cmdLen==0 && !state.receiveBinary )
    {
      // received CR while not in binary mode => remember but pass through
      state.receivedCR = true;
      return false;
    }
  
  // ---- handle IAC sequences
  
  if( state.cmdLen==0 )
    {
      // waiting for IAC byte
      if( b == T_IAC )
        {
          state.cmdLen = 1;
          state.cmd[0] = T_IAC;
          if( SerialData.handleTelnetProtocol>1 ) {Serial.print('<'); Serial.print(b, HEX);}
          return true;
        }
    }
  else if( state.cmdLen==1 )
    {
      if( SerialData.handleTelnetProtocol>1 ) {Serial.print('<'); Serial.print(b, HEX);}
      // received second byte of IAC sequence
      if( b == T_IAC )
        {
          // IAC->IAC sequence means regular 0xff data value
          state.cmdLen = 0; 

          // we already skipped the first IAC (0xff), so just return 'false' to pass this one through
          return false; 
        }
      else if( b == T_NOP || b == T_BREAK || b == T_GOAHEAD )
        {
          // NOP, BREAK, GOAHEAD => do nothing and skip
          state.cmdLen = 0; 
          return true; 
        }
      else if( b == T_SB )
        {
          // start of sub-negotiation
          state.subnegotiation = true;
          state.cmdLen = 0;
          return true;
        }
      else
        {
          // record second byte of sequence
          state.cmdLen = 2;
          state.cmd[1] = b;
          return true;
        }
    }
  else if( state.cmdLen==2 )
    {
      // received third (i.e. last) byte of IAC sequence
      if( SerialData.handleTelnetProtocol>1 ) {Serial.print('<'); Serial.print(b, HEX);}
      state.cmd[2] = b;

      bool reply = true;
      if( state.cmd[1] == T_WILL )
        {
          switch( state.cmd[2] )
            {
            case TO_SEND_BINARY:        state.cmd[1] = T_DO; state.receiveBinary = true; break;
            case TO_ECHO:               state.cmd[1] = T_DO; break;
            case TO_SUPPRESS_GO_AHEAD:  state.cmd[1] = T_DO; break;
            default: state.cmd[1] = T_DONT; break;
            }
        }
      else if( state.cmd[1] == T_WONT )
        {
          switch( state.cmd[2] )
            {
            case TO_SEND_BINARY: state.cmd[1] = T_DO; state.receiveBinary = false; break;
            }
          state.cmd[1] = T_DONT; 
        }
      else if( state.cmd[1] == T_DO )
        {
          switch( state.cmd[2] )
            {
            case TO_SEND_BINARY:       state.cmd[1] = T_WILL; state.sendBinary = true; break;
            case TO_SUPPRESS_GO_AHEAD: state.cmd[1] = T_WILL; break;
            case TO_TERMINAL_TYPE:     state.cmd[1] = SerialData.telnetTerminalType[0]==0 ? T_WONT : T_WILL; break;
            default: state.cmd[1] = T_WONT; break;
            }
        }
      else if( state.cmd[1] == T_DONT )
        {
          switch( state.cmd[2] )
            {
            case TO_SEND_BINARY: state.cmd[1] = T_WILL; state.sendBinary = false; break;
            }
          state.cmd[1] = T_WONT;
        }
      else
        reply = false;

      // send reply if necessary
      if( reply ) 
        {
          if( SerialData.handleTelnetProtocol>1 )
            for(int k=0; k<3; k++) {Serial.print('>'); Serial.print(state.cmd[k], HEX);}

          client.write(state.cmd, 3);

          if( state.cmd[1] == T_WILL && state.cmd[2] == TO_TERMINAL_TYPE )
            {
              // send terminal-type subnegoatiation sequence
              uint8_t buf[110], i, n=0;
              buf[n++] = T_IAC;
              buf[n++] = T_SB;
              buf[n++] = TO_TERMINAL_TYPE;
              buf[n++] = 0; // IS
              for(i=0; i<100 && SerialData.telnetTerminalType[i]>=32 && SerialData.telnetTerminalType[i]<127; i++) 
                buf[n++] = SerialData.telnetTerminalType[i];
              buf[n++] = T_IAC;
              buf[n++] = T_SE;
              client.write(buf, n);
              if( SerialData.handleTelnetProtocol>1 )
                for(int k=0; k<n; k++) {Serial.print('>'); Serial.print(buf[k], HEX);}
            }
        }

      // start over
      state.cmdLen = 0;
      return true;
    }
  else
    {
      // invalid state (should never happen) => just reset
      state.cmdLen = 0;
    }

  return false;
}


void handleModemCommand()
{
  // check if a modem AT command was received
  static int cmdLen = 0, prevCmdLen = 0;
  static char cmd[81];
  char c = 0;

  if( Serial.available() )
    {
      c = (Serial.read() & 0x7f);
      if( cmdLen<80 && c>32 )
        cmd[cmdLen++] = toupper(c);
      else if( cmdLen>0 && c == modemReg[REG_BSP] )
        cmdLen--;
          
      if( modemEcho ) 
        {
          if( c==8 )
            { Serial.print(char(8)); Serial.print(' '); Serial.print(char(8)); }
          else
            Serial.print(c);
        }
          
      prevCharTime = millis();
    }

  if( cmdLen==2 && cmd[0]=='A' && cmd[1]=='/' )
    {
      c = modemReg[REG_CR];
      cmd[1] = 'T';
      cmdLen = prevCmdLen;
    }
      
  if( c == modemReg[REG_CR] )
    {
      prevCmdLen = cmdLen;
      if( cmdLen>=2 && cmd[0]=='A' && cmd[1]=='T' )
        {
          cmd[cmdLen]=0;
          int ptr = 2;
          bool connecting = false;
          int status = E_OK;
          while( status!=E_ERROR && ptr<cmdLen )
            {
              if( cmd[ptr]=='D' )
                {
                  unsigned long t = millis();

                  // if we are already connected then disconnect first
                  if( modemClient.connected() ) 
                    {
                      modemClient.stop();
                      modemReg[REG_CURLINESPEED] = 0;
                    }

                  ptr++; if( cmd[ptr]=='T' || cmd[ptr]=='P' ) ptr++;

                  bool haveAlpha = false;
                  int numSep = 0;
                  for(int j=ptr; j<cmdLen && !haveAlpha; j++)
                    {
                      if( isalpha(cmd[j]) )
                        haveAlpha = true;
                      else if( !isdigit(cmd[j]) )
                        {
                          numSep++;
                          while( j<cmdLen && !isdigit(cmd[j]) ) j++;
                        }
                    }

                  byte n[4];
                  IPAddress addr;
                  int port = 23;
                  if( haveAlpha )
                    {
                      int p = ptr;
                      while( cmd[p]!=':' && p<cmdLen ) p++;
                      char c = cmd[p];
                      cmd[p] = 0;
                      if( WiFi.hostByName(cmd+ptr, addr, 5000) )
                        {
                          if( p+1<cmdLen ) port = atoi(cmd+p+1);
                        }
                      else
                        status = E_NOCARRIER;
                      cmd[p] = c;
                    }
                  else if( numSep==0 )
                    {
                      if( (cmdLen-ptr==12) || (cmdLen-ptr==16) )
                        {
                          for(int j=0; j<12; j+=3)
                            {
                              char c = cmd[ptr+j+3];
                              cmd[ptr+j+3] = 0;
                              n[j/3] = atoi(cmd+ptr+j);
                              cmd[ptr+j+3] = c;
                            }
                              
                          addr = IPAddress(n);
                          if( cmdLen-ptr==16 ) port = atoi(cmd+ptr+12);
                        }
                      else
                        status = E_ERROR;

                      ptr = cmdLen;
                    }
                  else if( numSep==3 || numSep==4 )
                    {
                      for(int j=0; j<4; j++)
                        {
                          int p = ptr;
                          while( isdigit(cmd[ptr]) && ptr<cmdLen ) ptr++;
                          char c = cmd[ptr];
                          cmd[ptr] = 0;
                          n[j] = atoi(cmd+p);
                          cmd[ptr] = c;
                          while( !isdigit(cmd[ptr]) && ptr<cmdLen ) ptr++;
                        }
                      addr = IPAddress(n);
                      if( numSep==4 ) port = atoi(cmd+ptr);
                    }
                  else
                    status = E_ERROR;

                  if( status==E_OK )
                    {
                      if( modemClient.connect(addr, port) )
                        {
                          modemCommandMode = false;
                          modemEscapeState = 0;
                          resetTelnetState(modemTelnetState);
                                
                          if( modemReg[REG_LINESPEED]==0 )
                            {
                              int i = 0;
                              while( i<NSPEEDS && linespeeds[i]<SerialData.baud ) i++;
                              if( i==NSPEEDS )
                                modemReg[REG_CURLINESPEED] = 255;
                              else if( linespeeds[i]==SerialData.baud )
                                modemReg[REG_CURLINESPEED] = i;
                              else
                                modemReg[REG_CURLINESPEED] = i-1;
                            }
                          else if( modemReg[REG_LINESPEED] < NSPEEDS )
                            modemReg[REG_CURLINESPEED] = min(modemReg[REG_LINESPEED], byte(NSPEEDS-1));

                          if( modemExtCodes==0 )
                            {
                              status = E_CONNECT;
                              connecting = true;
                            }
                          else
                            {
                              switch( modemReg[REG_CURLINESPEED] )
                                {
                                case 3: status = E_CONNECT; break;
                                case 4: status = E_CONNECT600; break;
                                case 5: status = E_CONNECT1200; break;
                                case 6: status = E_CONNECT2400; break;
                                case 7: status = E_CONNECT4800; break;
                                case 9: status = E_CONNECT9600; break;
                                default: status = E_CONNECT; break;
                                }

                              connecting = true;
                            }
                        }
                      else if( modemExtCodes < 2 )
                        status = E_NOCARRIER;
                      else if( WiFi.status() != WL_CONNECTED )
                        status = E_NODIALTONE;
                      else
                        status = E_NOANSWER;

                      // force at least 1 second delay between receiving
                      // the dial command and responding to it
                      if( millis()-t<1000 ) delay(1000-(millis()-t));
                    }
                      
                  // ATD can not be followed by other commands
                  ptr = cmdLen;
                }
              else if( cmd[ptr]=='H' )
                {
                  if( cmdLen-ptr==1 || cmd[ptr+1]=='0' )
                    {
                      // hang up
                      if( modemClient.connected() ) 
                        {
                          modemClient.stop();
                          modemReg[REG_CURLINESPEED] = 0;
                        }
                    }

                  ptr += 2;
                }
              else if( cmd[ptr]=='O' )
                {
                  getCmdParam(cmd, ptr);
                  if( modemClient.connected() )
                    {
                      modemCommandMode = false;
                      modemEscapeState = 0;
                      break;
                    }
                }
              else if( cmd[ptr]=='E' )
                modemEcho = getCmdParam(cmd, ptr)!=0;
              else if( cmd[ptr]=='Q' )
                modemQuiet = getCmdParam(cmd, ptr)!=0;
              else if( cmd[ptr]=='V' )
                modemVerbose = getCmdParam(cmd, ptr)!=0;
              else if( cmd[ptr]=='X' )
                modemExtCodes = getCmdParam(cmd, ptr);
              else if( cmd[ptr]=='Z' )
                {
                  // reset serial settings to saved value
                  EEPROM.get(768, SerialData);
                  applySerialSettings();

                  // reset parameters (ignore command parameter)
                  getCmdParam(cmd, ptr);
                  resetModemState();

                  // ATZ can not be followed by other commands
                  ptr = cmdLen;
                }
              else if( cmd[ptr]=='S' )
                {
                  static uint8_t currentReg = 0;
                  int p = ptr;
                  int reg = getCmdParam(cmd, ptr);
                  if( ptr==p+1 ) reg = currentReg;
                  currentReg = reg;

                  if( cmd[ptr]=='?' )
                    {
                      ptr++;
                      byte v = modemReg[reg];
                      if( modemVerbose ) printModemCR();
                      if( v<100 ) Serial.print('0');
                      if( v<10  ) Serial.print('0');
                      Serial.print(v);
                      printModemCR();
                    }
                  else if( cmd[ptr]=='=' )
                    {
                      byte v = getCmdParam(cmd, ptr);
                      if( reg != REG_CURLINESPEED )
                        modemReg[reg] = v;
                    }
                }
              else if( cmd[ptr]=='I' )
                {
                  byte n = getCmdParam(cmd, ptr);
                }
              else if( cmd[ptr]=='M' || cmd[ptr]=='L' || cmd[ptr]=='A' || cmd[ptr]=='P' || cmd[ptr]=='T' )
                {
                  // ignore speaker settings, answer requests, pulse/tone dial settings
                  getCmdParam(cmd, ptr);
                }
              else if( cmd[ptr]=='#' )
                {
                  ptr++;
                  if( strncmp(cmd+ptr, "BDR", 3)==0 )
                    {
                      ptr += 3;
                      if( cmd[ptr]=='?' )
                        {
                          if( modemVerbose ) printModemCR();
                          Serial.print(SerialData.baud / 2400);
                          printModemCR();
                        }
                      else if( cmd[ptr]=='=' )
                        {
                          if( cmd[ptr+1]=='?' )
                            {
                              if( modemVerbose ) printModemCR();
                              Serial.print('0');
                              for(int i=1; i<48; i++) { Serial.print(','); Serial.print(i); }
                              printModemCR();
                            }
                          else
                            {
                              byte v = getCmdParam(cmd, ptr);
                              if( v <= 48 )
                                {
                                  if( v==0 )
                                    EEPROM.get(768, SerialData);
                                  else
                                    SerialData.baud = v * 2400;

                                  printModemResult(E_OK);
                                  applySerialSettings();
                                  status = -1;
                                }
                              else
                                status = E_ERROR;
                            }
                        }
                      else
                        status = E_ERROR;
                    }
                  else
                    status = E_ERROR;
                  ptr = cmdLen;
                }
              else
                status = E_ERROR;
            }

          if( status>=0 )
            printModemResult(status);

          // delay 1 second after a "CONNECT" message
          if( connecting ) delay(1000);
        }
      else if( cmdLen>0 )
        printModemResult(E_ERROR);
              
      cmdLen = 0;
    }
}


void relayModemData()
{
  if( modemClient && modemClient.connected() && modemClient.available() ) 
    {
      int baud = modemReg[REG_CURLINESPEED]==255 ? SerialData.baud : linespeeds[modemReg[REG_CURLINESPEED]];

      if( baud == SerialData.baud )
        {
          // use full modem<->computer data rate
          unsigned long t = millis();
          while( modemClient.available() && Serial.availableForWrite() && millis()-t < 100 )
            {
              uint8_t b = modemClient.read();
              if( !handleTelnetProtocol(b, modemClient, modemTelnetState) ) Serial.write(b);
            }
        }
      else if( modemClient.available() && Serial.availableForWrite() )
        {
          // limit data rate
          static unsigned long nextChar = 0;
          if( millis()>=nextChar )
            {
              uint8_t b = modemClient.read();
              if( !handleTelnetProtocol(b, modemClient, modemTelnetState) )
                {
                  Serial.write(b);
                  nextChar = millis() + 10000/baud;
                }
            }
        }
    }

  if( millis() > prevCharTime + 20*modemReg[REG_GUARDTIME] )
    {
      if( modemEscapeState==0 )
        modemEscapeState = 1;
      else if( modemEscapeState==4 )
        {
          // received [1 second pause] +++ [1 second pause]
          // => switch to command mode
          modemCommandMode = true;
          printModemResult(E_OK);
        }
    }

  if( Serial.available() )
    {
      uint8_t buf[256];
      int n = 0, millisPerChar = 1000 / (SerialData.baud / (1+SerialData.bits+SerialData.stopbits)) + 1;
      unsigned long startTime = millis();
              
      if( millisPerChar<5 ) millisPerChar = 5;
      while( Serial.available() && n<256 && millis()-startTime < 100 )
        {
          uint8_t b = Serial.read();

          if( SerialData.handleTelnetProtocol )
            {
              // Telnet protocol handling is enabled

              // must duplicate IAC tokens
              if( b==T_IAC ) buf[n++] = b;

              // if not sending in binary mode then a stand-alone CR (without LF) must be followd by NUL
              if( !modemTelnetState.sendBinary && n>0 && buf[n-1] == 0x0d && b != 0x0a ) buf[n++] = 0;
            }

          buf[n++] = b;
          prevCharTime = millis();
                  
          if( modemEscapeState>=1 && modemEscapeState<=3 && b==modemReg[REG_ESC] )
            modemEscapeState++;
          else
            modemEscapeState=0;

          // wait a short time to see if another character is coming in so we
          // can send multi-character (escape) sequences in the same packet
          // some BBSs don't recognize the sequence if there is too much delay
          while( !Serial.available() && millis()-prevCharTime < millisPerChar );
        }
              
      // if not sending in binary mode then a stand-alone CR (without LF) must be followd by NUL
      if( SerialData.handleTelnetProtocol && !modemTelnetState.sendBinary && buf[n-1] == 0x0d && !Serial.available() ) buf[n++] = 0;

      modemClient.write(buf, n);
    }
}


void relayTelnetData()
{
  int i;

  //check clients for data
  for (i = 0; i < MAX_SRV_CLIENTS; i++) {
    if (serverClients[i] && serverClients[i].connected()) {
      if (serverClients[i].available()) {
        //get data from the telnet client and push it to the UART
        unsigned long t = millis();
        while(serverClients[i].available() && Serial.availableForWrite() && millis()-t < 100)
          {
            uint8_t b = serverClients[i].read();
            if( !handleTelnetProtocol(b, serverClients[i], clientTelnetState[i]) ) Serial.write(b);
          }
      }
    }
  }
          
  //check UART for data
  if( Serial.available() )
    {
      uint8_t buf[256];
      int n = 0, millisPerChar = 1000 / (SerialData.baud / (1+SerialData.bits+SerialData.stopbits))+1;
      unsigned long t, startTime = millis();
          
      if( millisPerChar<5 ) millisPerChar = 5;
      while( Serial.available() && n<256 && millis()-startTime < 100 )
        {
          uint8_t b = Serial.read();
          buf[n++] = b;
              
          // if Telnet protocol handling is enabled then we need to duplicate IAC tokens
          // if they occur in the general data stream
          if( b==T_IAC && SerialData.handleTelnetProtocol ) buf[n++] = b;
              
          // wait a short time to see if another character is coming in so we
          // can send multi-character (escape) sequences in the same packet
          t = millis();
          while( !Serial.available() && millis()-t < millisPerChar );
        }

      // push UART data to all connected telnet clients
      for (i = 0; i < MAX_SRV_CLIENTS; i++) 
        if( serverClients[i] && serverClients[i].connected() )
          {
            if( !SerialData.handleTelnetProtocol || clientTelnetState[i].sendBinary )
              serverClients[i].write(buf, n);
            else
              {
                // if sending in telnet non-binary mode then a stand-alone CR (without LF) must be followd by NUL
                uint8_t buf2[512];
                int j, m = 0;
                for(j=0; j<n; j++)
                  {
                    buf2[m++] = buf[j];
                    if( buf[j]==0x0d && (j>=n-1 || buf[j+1]!=0x0a) ) buf2[m++] = 0;
                  }
                serverClients[i].write(buf2, m);
              }
          }
    }
}


void loop() 
{
  uint8_t i;

  if( modemClient && modemClient.connected() )
    {
      // modem is connected. if telnet server has new client then reject
      if( server.hasClient() ) server.available().stop();

      // only relay data if not in command mode
      if( !modemCommandMode ) relayModemData();
    }
  else
    {
      // check whether connection to modem client was lost
      if( !modemCommandMode )
        {
          modemClient.stop();
          modemCommandMode = true;
          modemReg[REG_CURLINESPEED] = 0;
          printModemResult(E_NOCARRIER);
        }

      // check if there are any new telnet clients
      if( server.hasClient() ) 
        {
          // find free/disconnected spot
          for( i=0; i<MAX_SRV_CLIENTS; i++ )
            {
              if( !serverClients[i] || !serverClients[i].connected() ) 
                {
                  serverClients[i] = server.available();
                  resetTelnetState(clientTelnetState[i]);
                  break;
                }
            }
          
          //no free/disconnected spot so reject
          if( i == MAX_SRV_CLIENTS )
            {
              WiFiClient serverClient = server.available();
              serverClient.stop();
            }
        }
    }

  if( haveTelnetClient() )
    relayTelnetData();
  else if( modemCommandMode )
    handleModemCommand();

  webserver.handleClient();
}
