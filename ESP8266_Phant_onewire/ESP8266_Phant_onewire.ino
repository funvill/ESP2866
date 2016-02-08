/**
 * Created: 5 Feb 2016 
 * Created by: Steven Smethurst
 * 
 * More information: https://github.com/funvill/ESP2866
 * 
 * Libraries used
 * https://github.com/esp8266/Arduino 
 * https://github.com/tzapu/WiFiManager
 * https://github.com/milesburton/Arduino-Temperature-Control-Library 
 * 
 * Data Store:
 * https://data.sparkfun.com/ 
 */

#include <ESP8266WiFi.h>        // Allowes the ESP to connect to wifi access points 
#include <ESP8266HTTPClient.h>  // Allowes the ESP to send HTTP Requests. 
#include <ESP8266WebServer.h>   // Allowes the ESP to recv HTTP Requests. 
#include <EEPROM.h>             // Allowes for write to the EPROM

// WiFiManager library
// https://github.com/tzapu/WiFiManager
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          

// One Wire Temp Sensor 
// https://github.com/milesburton/Arduino-Temperature-Control-Library 
#include <OneWire.h>
#include <DallasTemperature.h>

//////////////////
// EPROM MEMORY //
//////////////////
// Phant 
char EEPROM_PhantHost[64]  = "data.sparkfun.com" ; 
char EEPROM_PublicKey[21]  = "VGXNn1WangTMQg6G8Qr2"; // Your public key 
char EEPROM_PrivateKey[21] = "9Ygo8kqj8ZswRbYemRz5"; // Your private key 

//////////////
// One Wire //
//////////////
/** 
 * Setup a oneWire instance to communicate with any OneWire devices (not 
 * just Maxim/Dallas temperature ICs). A 4.7K resistor connected between 
 * data and 5v is nessary. 
 * 
 * More information: https://datasheets.maximintegrated.com/en/ds/DS18B20.pdf
 */
OneWire oneWire(D6); 
DallasTemperature sensors(&oneWire); 

/////////////////
// Post Timing //
/////////////////
/** 
 * How much data can I push?
 * Each stream has a maximum of 50mb. After you hit the limit, the oldest data 
 * will be erased. (These limitations can be removed if installed on your own 
 * server). Logging is limited to 100 pushes in a 15 minute window. This allows 
 * you to push data in bursts, or spread them out over the 15 minute window.
 * Source: https://data.sparkfun.com/
 * 
 * There are 900 seconds in 15 mins. The MAX frequency that we can update the 
 * server is once every 9 secs 
 * 
 * The time is mesured in milliseconds. 
 *     1000 =  1 second
 *    30000 = 30 seconds. 
 */
const unsigned long postRate = 30 * 1000;

////////////////////////////
// HTTP Client and server //
////////////////////////////
WiFiClient        client; 
ESP8266WebServer  server(80);

// Other variables. 
char deviceName[32] ; 
byte ledStatus = LOW;     


void setup() {
  // Generate the device name 
  sprintf( deviceName, "esp%d", ESP.getChipId() ); 

  // Set up the status LED 
  pinMode(BUILTIN_LED, OUTPUT);

  // Set up the serial port 
  Serial.begin(115200);
  delay(10); // The serial port takes a little bit of time to fully start up. 
  Serial.println(""); Serial.println(""); Serial.println("");
  Serial.println("[Info] VHS - ESP + DS1820B");
  Serial.println("[Info] Chip ID: " + String( ESP.getChipId()) );  

  // Get the MAC addres 
  byte mac[6]; 
  WiFi.macAddress(mac);  
  Serial.print("[Info] MAC: "); Serial.print(mac[0],HEX); Serial.print(":"); Serial.print(mac[1],HEX); Serial.print(":"); 
  Serial.print(mac[2],HEX); Serial.print(":"); Serial.print(mac[3],HEX); Serial.print(":"); Serial.print(mac[4],HEX); 
  Serial.print(":"); Serial.println(mac[5],HEX);
  
  // WiFiManager
  // Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  wifiManager.autoConnect(deviceName);
  
  // Print the wifi connection details. 
  Serial.println("[WiFi] WiFi connected");
  
  // Read the presistent memory off the chip 
  ReadEEPROM(); 

  // Set up the webserver
  server.on("/", handleRoot);
  server.on("/config", handleConfig);
  server.begin();

  // Start up the temp sensor libary 
  sensors.begin();
}


void loop()
{  
  // If we are not connected to the network then we should to connect.  
  if (WiFi.status() != WL_CONNECTED) { 
    WiFiManager wifiManager;
    wifiManager.autoConnect(deviceName);
    return ; 
  }

  // Check for incoming HTTP requests and proccess them if needed. 
  server.handleClient();  

  // Check the tempature and update the server if needed. 
  static unsigned long previousMillisTemp = 0 ; 
  unsigned long currentMillisTemp = millis();
  if(currentMillisTemp - previousMillisTemp < postRate ) {
    // Not enught time has passed. Nothing to do....  
    delay(100);
    return ; 
  }

  // The timer has expired. Update the server with the current value. 
  previousMillisTemp = currentMillisTemp;  
  
  // Flip the LED 
  ledStatus = (ledStatus == HIGH) ? LOW : HIGH;
  digitalWrite(BUILTIN_LED, ledStatus);

  // Remind the user how to update the devices configuration. 
  Serial.print("[Info] Goto http://"); 
  Serial.print(WiFi.localIP());
  Serial.println(" to configure the device. "); 

  // Check to see if we have a device connected. 
  if( sensors.getDeviceCount() <= 0  ) {  
     Serial.println("[Info] Error: No OneWire devices detected. Nothing to do..."); 
     return ; 
  }

  // Send the command to get temperatures
  sensors.requestTemperatures() ; 
  float celsius = sensors.getTempCByIndex(0); 

  // Generate the URL based saved settings and the current tempature. 
  // http://data.sparkfun.com/input/VGXNn1WangTMQg6G8Qr2.txt?private_key=9Ygo8kqj8ZswRbYemRz5&celsius=0.0
  String url = "http://" ;
  url += String( EEPROM_PhantHost ) ;
  url += "/input/" ;
  url += String( EEPROM_PublicKey ) ; 
  url += ".txt?private_key=" ;
  url += String( EEPROM_PrivateKey ) ; 
  url += "&celsius=" ;
  url += String( celsius ) ; 
  
  // Send the HTTP REST request to the server 
  Serial.println("[HTTP.Client] GET " + String(url) );
        
  // Start connection and send HTTP header
  HTTPClient http;
  http.begin(url);

  // Wait on the response. 
  // Once we recive a response or timeout the httpCode will be updated
  int httpCode = http.GET();        
  if(httpCode > 0) {
      Serial.printf("[HTTP.Client] http.code: %d ", httpCode);
      if( httpCode == 200 ) { 
        // HTTP header has been send and Server response header has been handled
        Serial.println("OK - Everything is good" );  
      } else {
        Serial.println("???? - Something strange happened. Check the error code https://en.wikipedia.org/wiki/List_of_HTTP_status_codes");
      }
      Serial.println(http.getString());
      Serial.println(String( "View results here: https://") + String( EEPROM_PhantHost ) + String( "/streams/") + String( EEPROM_PublicKey) );
  } else {
      Serial.printf("[HTTP.Client] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

void handleConfig() {
  Serial.println("[HTTP.Server] HTTP.Requst GET /config");
  String message = "<!DOCTYPE HTML>\r\n<html><h1>VHS: ESP + DS1820 - Configuration page</h1>";

  // Check to see if the values have been changed. 
  String temp_PhantHost = server.arg("EEPROM_PhantHost");
  String temp_PublicKey = server.arg("EEPROM_PublicKey");
  String temp_PrivateKey = server.arg("EEPROM_PrivateKey");
  if( temp_PhantHost.length() > 0 && temp_PublicKey.length() > 0 && temp_PrivateKey.length() > 0 ) 
  {
    message += String( "<h3>UPDATED THE EEPROM</h3>");    
    message += String( "<table>");
    message += String( "<tr><th>PhantHost</th><td>")+ temp_PhantHost + "</td></tr>";
    message += String( "<tr><th>PublicKey</th><td>")+ temp_PublicKey + "</td></tr>";
    message += String( "<tr><th>PrivateKey</th><td>")+ temp_PrivateKey + "</td></tr>";
    message += String( "</table>");

    temp_PhantHost.toCharArray(EEPROM_PhantHost, 64 ); 
    temp_PublicKey.toCharArray(EEPROM_PublicKey, 21 ); 
    temp_PrivateKey.toCharArray(EEPROM_PrivateKey, 21 ); 

    // Write the values to the chip
    WriteEEPROM();

    message += String( "Done... Go back <a href='/'>Home</a><br />");
    server.send(200, "text/html", message);
    return ; 
  }

  // Let the user configure the variables. 
  message += String( "<form action='/config' method='get' >");
  
  message += String( "<h2>Phant configuration</h2>");
  message += String( "<table>" );
  message += String( "<tr><th><label>PhantHost: </th><td><input name='EEPROM_PhantHost' type='text' value='") + String( EEPROM_PhantHost ) + String( "'/></label></td></tr>");
  message += String( "<tr><th><label>PublicKey: </th><td><input name='EEPROM_PublicKey' type='text' value='") + String( EEPROM_PublicKey ) + String( "'/></label></td></tr>");
  message += String( "<tr><th><label>PrivateKey: </th><td><input name='EEPROM_PrivateKey' type='text' value='") + String( EEPROM_PrivateKey ) + String( "'/></label></td></tr>");
  message += String( "</table>" );

  message += String( "<br /><br />");
  message += String( "<input type='submit' />");
  message += String( "</form>");

  message += String( "<p><a href='/'>Go back home...</a></p></html>");
  server.send(200, "text/html", message);
}


void handleRoot() {
  Serial.println("[HTTP.Server] HTTP.Requst GET /");
  String message = "<!DOCTYPE HTML>\r\n<html><h1>VHS: ESP + DS1820</h1>";

  // Check to see if we can get the current Temperature   
  if( sensors.getDeviceCount() > 0 ){
    sensors.requestTemperatures() ; 
    message += String( "<p>Celsius=") + String( sensors.getTempCByIndex(0)  ) + String("</p>");    
    message += String( "<p>View full history <a href='https://") + String(EEPROM_PhantHost) + String("/streams/") + String(EEPROM_PublicKey) + String("'>full history</a> here</p>") ;
  } else {
    message += String( "<p>Error: Could not get the current Temperature</p>");
  }

  message += String( "<p><a href='/config'>Configure this device...</a></p></html>");
  server.send(200, "text/html", message );
}


void WriteEEPROM() {
  Serial.println("[EEPROM] Writing variables to EEPROM");
  EEPROM.begin(512);
  delay(10);

  for (int i = 0; i < 64; ++i) {    
    EEPROM.write(i, EEPROM_PhantHost[i] ); 
  }
  for (int i = 64; i < 64+21; ++i) {
    EEPROM.write(i, EEPROM_PublicKey[i-(64)] );
  }
  for (int i = 64+21; i < 64+21+21; ++i) {
    EEPROM.write(i, EEPROM_PrivateKey[i-(64+21)] );
  }
  EEPROM.commit();
  Serial.println("[EEPROM] Done...");
}

void ReadEEPROM() {
  Serial.println("[EEPROM] Reading variables from EEPROM");
  EEPROM.begin(512);
  delay(10);

  Serial.print("[EEPROM] Reading PhantHost");
  for (int i = 0; i < 64; ++i) {
    EEPROM_PhantHost[i] = char(EEPROM.read(i));
  }
  Serial.println(" = " + String( EEPROM_PhantHost ) ); 
  
  Serial.print("[EEPROM] Reading PublicKey");
  for (int i = 64; i < 64+21; ++i) {
    EEPROM_PublicKey[i-(64)] = char(EEPROM.read(i));
  }
  Serial.println(" = " + String( EEPROM_PublicKey ) );    
  
  Serial.print("[EEPROM] Reading PrivateKey");
  for (int i = 64+21; i < 64+21+21; ++i) {
    EEPROM_PrivateKey[i-(64+21)] = char(EEPROM.read(i));
  }
  Serial.println(" = " + String( EEPROM_PrivateKey ) );    
  Serial.println("[EEPROM] Done...");
}
