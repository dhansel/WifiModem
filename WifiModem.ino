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
uint32_t filterTelnetState[MAX_SRV_CLIENTS];
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
  byte     filterTelnetNegotiation;
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
  
  s += "</ul>\n<h2>Telnet negotiation</h2>\n<ul>\n";
  if( SerialData.filterTelnetNegotiation )
    {
      s += "<li><b>Filter out</b></li>\n";
      s += "<li><a href=\"set?filterTelnet=no\">Pass through</a></li>\n";
    }
  else
    {
      s += "<li><a href=\"set?filterTelnet=yes\">Filter out</a></li>\n";
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
      else if( webserver.argName(i) == "filterTelnet" && webserver.arg(i)=="yes" )
        SerialData.filterTelnetNegotiation = true;
      else if( webserver.argName(i) == "filterTelnet" && webserver.arg(i)=="no" )
        SerialData.filterTelnetNegotiation = false;
      else
        ok = false;
    }

  if( ok ) 
    {
      EEPROM.put(768, SerialData);
      EEPROM.commit();
      Serial.end();
      delay(100);
      Serial.begin(SerialData.baud, GetSerialConfig());
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
      SerialData.filterTelnetNegotiation = true;
      SerialData.magic    = MAGICVAL;
      EEPROM.put(768, SerialData);
      EEPROM.commit();
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

  modemReset();

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


void modemReset()
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


bool handleTelnetNegotiation(uint8_t b, WiFiClient &client, uint32_t *state)
{
  // returns "true" if parameter "b" is part of a telnet negotiation
  // sequence (sequence starts with 0xff followed by two more bytes)
  uint8_t *stateA = (uint8_t *) state;

  if( !SerialData.filterTelnetNegotiation )
    return false;
  else if( b == 0xff )
    stateA[0] = 1;

  if( stateA[0]==0 )
    return false;
  else
    {
      stateA[stateA[0]] = b;
      stateA[0]++;
      if( stateA[0]>=4 )
        {
          bool reply = true;

          if( stateA[2] == 251 && stateA[3] == 1 )
            stateA[2] = 253; // reply to WILL ECHO with DO ECHO
          else if( stateA[2] == 251 || stateA[2] == 252 )
            stateA[2] = 254; // reply to WILL/WONT * with DONT *
          else if( stateA[2] == 253 || stateA[2] == 254 )
            stateA[2] = 252; // reply to DO/DONT * with WONT *
          else
            reply = false;

          if( reply ) client.write(stateA+1, 3);
          stateA[0] = 0;
        }
      
      return true;
    }
}


void loop() 
{
  uint8_t i;

  if( modemClient && modemClient.connected() )
    {
      // if telnet server has new client then reject
      if( server.hasClient() ) server.available().stop();

      if( !modemCommandMode )
        {
          if( modemClient && modemClient.connected() && modemClient.available() ) 
            {
              int baud = modemReg[REG_CURLINESPEED]==255 ? SerialData.baud : linespeeds[modemReg[REG_CURLINESPEED]];

              if( baud == SerialData.baud )
                {
                  // use full modem<->computer data rate
                  while( modemClient.available() && Serial.availableForWrite() )
                    {
                      uint8_t b = modemClient.read();
                      if( !handleTelnetNegotiation(b, modemClient, filterTelnetState) ) Serial.write(b);
                    }
                }
              else if( modemClient.available() && Serial.availableForWrite() )
                {
                  // limit data rate
                  static unsigned long nextChar = 0;
                  if( millis()>=nextChar )
                    {
                      uint8_t b = modemClient.read();
                      if( !handleTelnetNegotiation(b, modemClient, filterTelnetState) )
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
              char buf[256];
              int n = 0, millisPerChar = 1000 / (SerialData.baud / (1+SerialData.bits+SerialData.stopbits))+1;
              unsigned long startTime = millis();
              
              if( millisPerChar<5 ) millisPerChar = 5;
              while( Serial.available() && n<256 && millis()-startTime < 100 )
                {
                  char c = Serial.read();
                  buf[n++] = c;
                  prevCharTime = millis();
                  
                  if( modemEscapeState>=1 && modemEscapeState<=3 && c==modemReg[REG_ESC] )
                    modemEscapeState++;
                  else
                    modemEscapeState=0;

                  // wait a short time to see if another character is coming in so we
                  // can send multi-character (escape) sequences in the same packet
                  // some BBSs don't recognize the sequence if there is too much delay
                  while( !Serial.available() && millis()-prevCharTime < millisPerChar );
                }

              modemClient.write(buf, n);
            }
        }
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
                  filterTelnetState[i] = 0;
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
    {
      //check clients for data
      for (i = 0; i < MAX_SRV_CLIENTS; i++) {
        if (serverClients[i] && serverClients[i].connected()) {
          if (serverClients[i].available()) {
            //get data from the telnet client and push it to the UART
            while(serverClients[i].available() && Serial.availableForWrite() ) {
              uint8_t b = serverClients[i].read();
              if( !handleTelnetNegotiation(b, serverClients[i], filterTelnetState+i) ) Serial.write(b);
            }
          }
        }
      }
          
      //check UART for data
      if (Serial.available()) 
        {
          size_t len = Serial.available();
          uint8_t sbuf[len];
          Serial.readBytes(sbuf, len);
          //push UART data to all connected telnet clients
          for (i = 0; i < MAX_SRV_CLIENTS; i++) {
            if (serverClients[i] && serverClients[i].connected()) {
              serverClients[i].write(sbuf, len);
              delay(1);
            }
          }
        }
    }
  else if( modemCommandMode )
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
              uint8_t status = E_OK;
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
                          //Serial.print("connecting to: "); Serial.print(n[0]); Serial.print('.'); Serial.print(n[1]); Serial.print('.'); Serial.print(n[2]); Serial.print('.'); Serial.print(n[3]); Serial.print(':'); Serial.println(port);
                          if( modemClient.connect(addr, port) )
                            {
                              // force at least 1 second delay between receiving
                              // the dial command and responding to it
                              if( millis()-t<1000 ) delay(1000-(millis()-t));
                              
                              modemCommandMode = false;
                              modemEscapeState = 0;
                              filterTelnetState[0] = 0;

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
                                status = E_CONNECT;
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
                      // reset parameters (ignore command parameter)
                      getCmdParam(cmd, ptr);
                      modemReset();

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
                  else
                    status = E_ERROR;
                }
              
              printModemResult(status);
              if( connecting ) delay(1000);
            }
          else if( cmdLen>0 )
            printModemResult(E_ERROR);
              
          cmdLen = 0;
        }
    }

  webserver.handleClient();
}
