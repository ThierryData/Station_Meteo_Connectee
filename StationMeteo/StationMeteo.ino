/*************************************************************************************
  Nom
        StationMeteo.ino
  Role
        Station météo connectée basée sur module NodeMCU ESP8266 Wifi
        Fonction :
          - Acquérir la température et l'hygrométrie avec un DHT11
          - Acquérir la pression atmosphérique (a venir)
          - Mesurer la pluviométrie (a venir)
          - Mettre à disposition ces informations par l'intermédiare d'un serveur web
          - Transmettre ces informations sur un serveur distant (type thingspeak)

  Auteur
        Laurent Macron
        Thierry Dezormeaux

  Version / Date / Commentaire / Auteur
        0.01 / 03-01-2017 / Version initial
*************************************************************************************/

/***********  Ajout de bibliothèque  ************************************************/
#include <dht.h> //Librairie pour le capteur DHT http://arduino.cc/playground/Main/DHTLib
#include <ESP8266WiFi.h>  //ESP8266 Core WiFi Library https://github.com/esp8266/Arduino

#include <DNSServer.h> //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h> //Local WebServer used to serve the configuration portal
#include <WiFiManager.h> //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#include <Wire.h> 
//#include <LiquidCrystal_I2C.h> //https://github.com/marcoschwartz/LiquidCrystal_I2C

#include <Adafruit_BMP085.h> // Library for BMP180 "Adafruit_BMP085 library v1.0.0" 

#include <WiFiUdp.h>

//for LED status
#include <Ticker.h>
Ticker ticker;

#include "ThingSpeak.h"

/***********  Déclaration des CONSTANTES COMPILATION *******************************************/
#define TEST true 

/***********  Déclaration des CONSTANTES  *******************************************/
#define DHT11_PIN 0 //The data I/O pin connected to the DHT11 sensor : GPIO0 = D3 (for memory GPIO5 = D1) of NodeMCU ESP8266 Board

/***********  Déclaration des variables globales  ***********************************/
dht DHT;  //Creation de l'objet DHT
//LiquidCrystal_I2C lcd(0x3F,16,2);  // set the LCD address to 0x3F for a 16 chars and 2 line display

WiFiServer server(80); // Create an instance of the server, specify the port to listen on as an argument

// ThingSpeak
WiFiClient  client;
if (TEST){
  unsigned long myChannelNumber = 73956; //Thermomètre 
  const char * myWriteAPIKey = "50S0WGBDUG294NK5";
}else
{
  unsigned long myChannelNumber = 290841; //Station Météo
  const char * myWriteAPIKey = "QHAVPLJB3C55CSCD";
}


unsigned long lastWriteThingSpeak = 0 ;
unsigned long delaySendThingSpeak = 300000; // 300s - ThingSpeak will only accept updates every 15 seconds.

// variable pour stocker valeur lue par sensor
int humidity_DHT;
int Last_humidity_DHT;
int temperature_DHT;
float currentTemperature;

//Variable pluviomètre
unsigned long lastDetectionRainSensor = 0 ; // Variable antirebond 
boolean detection_Pluie = false;
float pluie_mm = 0; 

//Variable MIN MAX
float MaxTemperature = -40;
float MinTemperature = +80;

// BMP180 SENSOR
// Connect VCC of the BMP085 sensor to 3.3V (NOT 5.0V!)
// Connect GND to Ground
// Connect SCL to i2c clock - D1 on NodeMCU
// Connect SDA to i2c data - D2 on NodeMCU

Adafruit_BMP085 bmp180_sensor;

//Variable pour récupération de l'heure par serveur basé sur l'exemple Udp NTP Client
unsigned int localPort = 2390;      // local port to listen for UDP packets

IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "time.nist.gov";

const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

unsigned long lastUDPRequest = 0 ;
unsigned long delayUDPRequest = 60000; // 60s = 1min - délai de requête UDP

unsigned long lastHours = 0; //Save last hours

// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;


/***********  setup, to run once  ***************************************************/
void setup() {
  //On ouvre un connexion série pour le terminal
  Serial.begin(115200);
  Serial.println("Programme Station Meteo initialisée");
  Serial.println();

/*  //LCD I2C Initialisation
  Wire.begin(0,2); // LCD: SDA = D3(GPIO0) ; SCL = D4(GPIO2)
  lcd.init();                      // initialize the lcd 
  lcd.backlight();
  lcd.print("Station meteo");
  lcd.setCursor(2,1);
  lcd.print("demarrage ...");
*/
  //set led pin as output
  pinMode(BUILTIN_LED, OUTPUT);
  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);

  //WiFiManager
  WiFiManager wifiManager;
  
  wifiManager.setAPCallback(configModeCallback); //Use this if you need to do something when your device enters configuration mode on failed WiFi connection attempt.
  
  //fetches ssid and pass from eeprom and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnect_StationMeteo"
  //and goes into a blocking loop awaiting configuration
  wifiManager.autoConnect("AutoConnect_StationMeteo");
  //or use this for auto generated name ESP + ChipID
  //wifiManager.autoConnect();
  
  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");
  ticker.detach();
  //keep LED on
  digitalWrite(BUILTIN_LED, LOW);
  
  // Start the server
  server.begin();
  Serial.println("Server started");
  Serial.println();

  if (!bmp180_sensor.begin()) {
    Serial.println("Could not find a valid BMP180 sensor, check wiring!");
  }

  Serial.print("DHTLib LIBRARY VERSION: ");
  Serial.println(DHT_LIB_VERSION);
  Serial.println();
  Serial.println("Type,\tstatus,\tHumidity (%),\tTemperature (C)");

  ThingSpeak.begin(client);
  attachInterrupt(2,interruption_Pluie,RISING); //Input D4

  //Pour récupérer date et heure
  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());
}

/***********  main, run repeatedly **************************************************/
void loop() {

  // On récupère l'heure
  if( (millis()- lastUDPRequest) > delayUDPRequest) 
  {
    lastUDPRequest = millis();
    //get a random server from the pool
    WiFi.hostByName(ntpServerName, timeServerIP); 
  
    sendNTPpacket(timeServerIP); // send an NTP packet to a time server
    // wait to see if a reply is available
    delay(1000);

    int cb = udp.parsePacket();
    if (!cb) {
      Serial.println("no packet yet");
    }
    else {
      Serial.print("packet received, length=");
      Serial.println(cb);
      // We've received a packet, read the data from it
      udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
  
      //the timestamp starts at byte 40 of the received packet and is four bytes,
      // or two words, long. First, esxtract the two words:
  
      unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
      unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
      // combine the four bytes (two words) into a long integer
      // this is NTP time (seconds since Jan 1 1900):
      unsigned long secsSince1900 = highWord << 16 | lowWord;
      Serial.print("Seconds since Jan 1 1900 = " );
      Serial.println(secsSince1900);
  
      // now convert NTP time into everyday time:
      Serial.print("Unix time = ");
      // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
      const unsigned long seventyYears = 2208988800UL;
      // subtract seventy years:
      unsigned long epoch = secsSince1900 - seventyYears;
      // print Unix time:
      Serial.println(epoch);
  
  
      // print the hour, minute and second:
      Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
      Serial.print((epoch  % 86400L) / 3600); // print the hour (86400 equals secs per day)
      Serial.print(':');
      if ( ((epoch % 3600) / 60) < 10 ) {
        // In the first 10 minutes of each hour, we'll want a leading '0'
        Serial.print('0');
      }
      Serial.print((epoch  % 3600) / 60); // print the minute (3600 equals secs per minute)
      Serial.print(':');
      if ( (epoch % 60) < 10 ) {
        // In the first 10 seconds of each minute, we'll want a leading '0'
        Serial.print('0');
      }
      Serial.println(epoch % 60); // print the second

      //Reset Max temperature at 24H00
      if((((epoch  % 86400L) / 3600) == 0) && (lastHours == 23))
      {
        ThingSpeak.setField(4,MaxTemperature);
        ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);  
        resetMax();
        // reset pluie
        pluie_mm = 0;
      }
      //Reset Min temperature at 12H00
      if((((epoch  % 86400L) / 3600) == 12) && (lastHours == 11))
      {
        ThingSpeak.setField(3,MinTemperature);
        ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);  
        resetMin();
      }
      lastHours = (epoch  % 86400L) / 3600;
    }   
  }

  //READ DATA
  Serial.print("DHT11, \t");
  int chk = DHT.read11(DHT11_PIN);
  switch (chk)
  {
    case DHTLIB_OK:
      Serial.print("OK,\t");
      break;
    case DHTLIB_ERROR_CHECKSUM:
      Serial.print("Checksum error,\t");
      break;
    case DHTLIB_ERROR_TIMEOUT:
      Serial.print("Time out error,\t");
      break;
    default:
      Serial.print("Unknown error,\t");
      break;
  }

  //filtre les valeurs erronées
  int read_humidity = DHT.humidity;
  if (Last_humidity_DHT == read_humidity)
  {
    humidity_DHT = read_humidity;
  }
  Last_humidity_DHT = read_humidity;
  
  temperature_DHT = DHT.temperature;
  currentTemperature = bmp180_sensor.readTemperature();

  //Check if temperature MIN or MAX
  if(MaxTemperature < currentTemperature)
  {
    MaxTemperature = currentTemperature;
  }
  if(MinTemperature > currentTemperature)
  {
    MinTemperature = currentTemperature;
  }

  // DISPLAY DATA
  Serial.print(humidity_DHT, 1);
  Serial.print("%,\t");
  Serial.print(temperature_DHT, 1);
  Serial.print("*C,\t bmp180: ");
  Serial.print(currentTemperature, 1);
  Serial.print("*C, Min: ");
  Serial.print(MinTemperature, 1);
  Serial.print("*C, Max: ");
  Serial.print(MaxTemperature, 1);
  Serial.println("*C");

  
/*  //Display on LCD I2C
  lcd.setCursor(2,1);
  lcd.print(humidity_DHT);
  lcd.print("% ");
  lcd.print(currentTemperature);
  lcd.print("C       ");
  */
  fct_bmp180(); // Display bmp180 information
  
  // Check if a client has connected
  /*WiFiClient*/ client = server.available();
  if (client) {
    // Wait until the client sends some data
    Serial.println("new client");
    while (!client.available()) {
      delay(1);
    }
    // Read the first line of the request
    String req = client.readStringUntil('\r');
    Serial.println(req);
    client.flush();

    // Match the request
    if (req.indexOf("/on") != -1) {
      digitalWrite(LED_BUILTIN, LOW);
    }
    else if (req.indexOf("/off") != -1) {
      digitalWrite(LED_BUILTIN, HIGH);
    }
    else if (req.indexOf("/reset") != -1) { //Reset request
      MaxTemperature = currentTemperature;
      MinTemperature = currentTemperature;
    }

    client.flush();

    // Prepare the response
    String s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
    s += "<head>";
    s += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    s += "<script src=\"https://code.jquery.com/jquery-2.1.3.min.js\"></script>";
    s += "<link rel=\"stylesheet\" href=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.4/css/bootstrap.min.css\">";
    s += "</head>";
    s += "<div class=\"container\">";
    s += "<h1>Lamp Control</h1>";
    s += "<p><br/>Capteur temperature DHT 11:<br/> ";
    s += humidity_DHT;
    s += " %  ";
    s += temperature_DHT;
    s += " C ";
    s += "MIN = ";
    s += MinTemperature;
    s += "MAX = ";
    s += MaxTemperature;
    s += "</p><br/>";
    s += "<div class=\"row\">";
    s += "<div class=\"row\">";
    s += "<div class=\"row\">";
    s += "<div class=\"col-xs-6\"><input class=\"btn btn-block btn-lg btn-primary\" type=\"button\" value=\"On\" onclick=\"on()\"></div>";
    s += "<div class=\"col-xs-6\"><input class=\"btn btn-block btn-lg btn-danger\" type=\"button\" value=\"Off\" onclick=\"off()\"></div>";
    s += "<div class=\"col-xs-6\"><input class=\"btn btn-block btn-lg btn-danger\" type=\"button\" value=\"Reset\" onclick=\"reset()\"></div>";
    s += "</div>";
    s += "<div class=\"row\">";
    s += "</div></div>";
    s += "<script>function Refresh() {$.get(\"/refresh\");setTimeout(reloadpage, 500);}</script>";
    s += "<script>function on() {$.get(\"/on\");}</script>";
    s += "<script>function off() {$.get(\"/off\");}</script>";
    s += "<script>function reset() {$.get(\"/reset\");}</script>";

    // Send the response to the client
    client.print(s);
    delay(1);
    Serial.println("Client disconnected");

    // The client will actually be disconnected
    // when the function returns and 'client' object is detroyed
  }

  // Write to ThingSpeak
  if( (millis()- lastWriteThingSpeak) > delaySendThingSpeak) // ThingSpeak will only accept updates every 15 seconds.
  {
    lastWriteThingSpeak = millis();
    ThingSpeak.setField(1,currentTemperature);
    ThingSpeak.setField(2,humidity_DHT);

    // Write the fields that you've set all at once.
    ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);  
  }

  delay(10000); // Wait 10 secondes before new loop

  //L'interruption de détection de pluie a été enclencé
  if(detection_Pluie)
  {
    Serial.println("####################################################");
    Serial.println("#      Detection entrée D4: 0,2794 mm de pluie     #");
    Serial.println("####################################################");
    pluie_mm = pluie_mm + 0,2794;
    ThingSpeak.setField(5,pluie_mm);
    ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);  
  }
}

/***********  for LED status  **************************************************/
void tick()
{
  //toggle state
  int state = digitalRead(BUILTIN_LED);  // get the current state of GPIO1 pin
  digitalWrite(BUILTIN_LED, !state);     // set pin to the opposite state
}


/***********  Display data of BMP180 sensor  **************************************************/
void fct_bmp180()
{
    Serial.print("Temperature = ");
    Serial.print(bmp180_sensor.readTemperature());
    Serial.println(" *C");

    Serial.print("Pressure = ");
    Serial.print(bmp180_sensor.readPressure());
    Serial.println(" Pa");
    
    // Calculate altitude assuming 'standard' barometric
    // pressure of 1013.25 millibar = 101325 Pascal
    Serial.print("Altitude = ");
    Serial.print(bmp180_sensor.readAltitude());
    Serial.println(" meters");

    Serial.print("Pressure at sealevel (calculated) = ");
    Serial.print(bmp180_sensor.readSealevelPressure());
    Serial.println(" Pa");

  // you can get a more precise measurement of altitude
  // if you know the current sea level pressure which will
  // vary with weather and such. If it is 1015 millibars
  // that is equal to 101500 Pascals.
    Serial.print("Real altitude = ");
    Serial.print(bmp180_sensor.readAltitude(101500));
    Serial.println(" meters");
    
    Serial.println();
}

/***********  test Interrup **************************************************/
void interruption_Pluie()
{
  if( (millis()- lastDetectionRainSensor) > 200) // Filtre antirebond: accept une detection uniquement toutes les 200ms
  {
    lastDetectionRainSensor = millis();
    detection_Pluie = false;
  }
}

/***********  Executed if device enters configuration mode on failed WiFi connection attempt **************************************************/
void configModeCallback (WiFiManager *myWiFiManager) {
  ticker.attach(0.1, tick);

  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());

  Serial.println(myWiFiManager->getConfigPortalSSID());
}

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

/***********  Reset temperature Min Max **************************************************/
void resetMax()
{
  MaxTemperature = currentTemperature;
}
void resetMin()
{
  MinTemperature = currentTemperature;
}

