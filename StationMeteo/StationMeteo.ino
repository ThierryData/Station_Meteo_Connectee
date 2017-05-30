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

//for LED status
#include <Ticker.h>
Ticker ticker;

#include "ThingSpeak.h"

/***********  Déclaration des CONSTANTES  *******************************************/
#define DHT11_PIN 0 //The data I/O pin connected to the DHT11 sensor : GPIO0 = D3 (for memory GPIO5 = D1) of NodeMCU ESP8266 Board

/***********  Déclaration des variables globales  ***********************************/
dht DHT;  //Creation de l'objet DHT
//LiquidCrystal_I2C lcd(0x3F,16,2);  // set the LCD address to 0x3F for a 16 chars and 2 line display

WiFiServer server(80); // Create an instance of the server, specify the port to listen on as an argument

// ThingSpeak
WiFiClient  client;
unsigned long myChannelNumber = 73956;
const char * myWriteAPIKey = "50S0WGBDUG294NK5";
unsigned long lastWriteThingSpeak = 0 ;

// variable pour stocker valeur lue par sensor
int humidity_DHT;
int temperature_DHT;

// BMP180 SENSOR
// Connect VCC of the BMP085 sensor to 3.3V (NOT 5.0V!)
// Connect GND to Ground
// Connect SCL to i2c clock - D1 on NodeMCU
// Connect SDA to i2c data - D2 on NodeMCU

Adafruit_BMP085 bmp180_sensor;

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
}

/***********  main, run repeatedly **************************************************/
void loop() {

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
  // DISPLAY DATA
  humidity_DHT = DHT.humidity;
  temperature_DHT = DHT.temperature;
  Serial.print(humidity_DHT, 1);
  Serial.print(",\t");
  Serial.println(temperature_DHT, 1);

/*  //Display on LCD I2C
  lcd.setCursor(2,1);
  lcd.print(humidity_DHT);
  lcd.print("% ");
  lcd.print(temperature_DHT);
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
    s += " C";
    s += "</p><br/>";
    s += "<div class=\"row\">";
    s += "<div class=\"row\">";
    s += "<div class=\"row\">";
    s += "<div class=\"col-xs-6\"><input class=\"btn btn-block btn-lg btn-primary\" type=\"button\" value=\"On\" onclick=\"on()\"></div>";
    s += "<div class=\"col-xs-6\"><input class=\"btn btn-block btn-lg btn-danger\" type=\"button\" value=\"Off\" onclick=\"off()\"></div>";
    s += "</div>";
    s += "<div class=\"row\">";
    s += "</div></div>";
    s += "<script>function Refresh() {$.get(\"/refresh\");setTimeout(reloadpage, 500);}</script>";
    s += "<script>function on() {$.get(\"/on\");}</script>";
    s += "<script>function off() {$.get(\"/off\");}</script>";

    // Send the response to the client
    client.print(s);
    delay(1);
    Serial.println("Client disconnected");

    // The client will actually be disconnected
    // when the function returns and 'client' object is detroyed
  }

  // Write to ThingSpeak
  if( (millis()- lastWriteThingSpeak) > 20000) // ThingSpeak will only accept updates every 15 seconds.
  {
    lastWriteThingSpeak = millis();
    ThingSpeak.setField(1,temperature_DHT);
    ThingSpeak.setField(2,humidity_DHT);

    // Write the fields that you've set all at once.
    ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);  
  }

  delay(2000); // Wait 2 secondes before new loop
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

