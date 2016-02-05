/**
 * Created: 5 Feb 2016 
 * Created by: Steven Smethurst
 * 
 * More information: https://github.com/funvill/ESP2866
 */

#include <ESP8266WiFi.h>  // Include the ESP8266 WiFi library. (Works a lot like the Arduino WiFi library.)
#include <ESP8266HTTPClient.h> // Include the ESP8266 wifi http client. 
#include <ESP8266WebServer.h>
#include <OneWire.h>
#include <Phant.h>

//////////////////////
// WiFi Definitions //
//////////////////////
const char WiFiSSID[] = "xxx";   // YOUR SSID 
const char WiFiPSK[]  = "xxx";              // YOUR PASSWORD 

///////////
// Phant // 
///////////
const char PhantHost[]  = "data.sparkfun.com";
const char PublicKey[]  = "xxx";  // Your public key 
const char PrivateKey[] = "xxx";  // Your private key 

Phant phant("data.sparkfun.com", PublicKey, PrivateKey);

//////////////
// One Wire //
//////////////
OneWire  ds(D6);  // on pin 10 (a 4.7K resistor is necessary)

/////////////////
// Post Timing //
/////////////////

/* 
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


WiFiClient        client; // Create an ESP8266 WiFiClient class to connect 
ESP8266WebServer  server(80);

byte ledStatus = LOW;     

void setup() {

  // Set up the status LED 
  pinMode(BUILTIN_LED, OUTPUT);

  // Set up the serial port 
  Serial.begin(115200);
  delay(10); // The serial port takes a little bit of time to fully start up. 
  Serial.println(""); Serial.println(""); Serial.println("");
  Serial.println("[info] VHS - ESP + DS1820");

  // Set up the webserver
  server.on("/", handleRoot);
  server.on("/config", handleConfig);
  server.begin();
}


void loop()
{
  // If we are not connected to the network then we should to connect.  
  if (WiFi.status() != WL_CONNECTED) { 
    connectWiFi();
    return ; 
  }

  // Check for incoming HTTP requests and proccess them if needed. 
  server.handleClient();


  // Check the tempature and update the server if needed. 
  static unsigned long previousMillisTemp = 0 ; 
  unsigned long currentMillisTemp = millis();
  if(currentMillisTemp - previousMillisTemp >= postRate ) 
  {   
    // Flip the LED 
    ledStatus = (ledStatus == HIGH) ? LOW : HIGH;
    digitalWrite(BUILTIN_LED, ledStatus);

    // Get the temp 
    float celsius = 0.0f;
    if( GetTemp (&celsius ) ){
      phant.add("celsius", celsius);

      HTTPClient http;
      http.begin(phant.url()); //HTTP
      Serial.println("[HTTP.Client] GET " +String(phant.url()) );
      // start connection and send HTTP header
      int httpCode = http.GET();

      // httpCode will be negative on error
      if(httpCode > 0) {
          // HTTP header has been send and Server response header has been handled
          Serial.printf("[HTTP.Client] http.code: %d\n", httpCode);

          // file found at server
          if(httpCode == HTTP_CODE_OK) {
              String payload = http.getString();
              Serial.println(payload);
          }
      } else {
          Serial.printf("[HTTP.Client] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
      }
      http.end();

      // Only do this at the end. If there is an error and we don't sent to the server 
      // We want to try again ASAP. 
      previousMillisTemp = currentMillisTemp;         
    }
  }

  delay(100);
}


bool GetTemp (float * celsius ) {
  byte i;
  byte present = 0;
  byte type_s;
  byte data[12];
  byte addr[8];

  if( celsius == NULL ) {
    return false; 
  }
  
  // There is an issue here that if there are more then one sensor we want to be able to get the current temp from each one. 
  // ToDo: what do we do if there is more then one temp sensor? 
  // Teh following command will reset the search so only the first sensor will ever be found. 
  ds.reset_search();

  // Search the OneWire bus for a device. 
  if ( !ds.search(addr)) {
    Serial.println("[OneWire] FYI: No more oneWire Sensors found " );
    ds.reset_search(); // Reset the search back to the start. 
    return false; 
  }
  
  Serial.print("[OneWire] Address: ");
  for( i = 0; i < 8; i++) {
    if( addr[i] < 10 ) { 
      Serial.print("0");
    }
    Serial.print(addr[i], HEX);
  }
  Serial.print(", ");

  if (OneWire::crc8(addr, 7) != addr[7]) {
      Serial.println("CRC is not valid!");
      return false; 
  }
  // Serial.println();
 
  // the first ROM byte indicates which chip
  switch (addr[0]) {
    case 0x10:
      Serial.print("Chip = DS18S20, ");  // or old DS1820
      type_s = 1;
      break;
    case 0x28: // We have this one 
      Serial.print("Chip = DS18B20, ");
      type_s = 0;
      break;
    case 0x22:
      Serial.print("Chip = DS1822, ");
      type_s = 0;
      break;
    default:
      Serial.println("[OneWire] Device is not a DS18x20 family device.");
      return false; 
  } 

  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);        // start conversion, with parasite power on at the end
  
  // delay(1000);     // maybe 750ms is enough, maybe not
  // we might do a ds.depower() here, but the reset will take care of it.
  
  present = ds.reset();
  ds.select(addr);    
  ds.write(0xBE);         // Read Scratchpad

  // Serial.print("  Data = ");
  // Serial.print(present, HEX);
  // Serial.print(" ");
  for ( i = 0; i < 9; i++) {           // we need 9 bytes
    data[i] = ds.read();
    // Serial.print(data[i], HEX);
    // Serial.print(" ");
  }
  // Serial.print(" CRC=");
  // Serial.print(OneWire::crc8(data, 8), HEX);
  // Serial.println();

  // Convert the data to actual temperature
  // because the result is a 16 bit signed integer, it should
  // be stored to an "int16_t" type, which is always 16 bits
  // even when compiled on a 32 bit processor.
  int16_t raw = (data[1] << 8) | data[0];
  if (type_s) {
    raw = raw << 3; // 9 bit resolution default
    if (data[7] == 0x10) {
      // "count remain" gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  } else {
    byte cfg = (data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
    //// default is 12 bit resolution, 750 ms conversion time
  }
  *celsius = (float)raw / 16.0;
  Serial.print("Temperature = ");
  Serial.print(*celsius);
  Serial.println(" Celsius");
  return true; 
}

void handleConfig() {
  Serial.println("[HTTP.Server] HTTP.Requst GET /config");
  String message = "<h1>VHS: ESP + DS1820 - Configuration page</h1>";
  message += String( "ToDo: Do the SoftAP configuration here. Set up the WiFi and the public and private key for data.sparkfun.com");

  message += String( "<h2>WiFi configuration</h2>");
  message += String( "<label>SSID: <select><option>ONE ONE ONE</option><option>TWO TWO TWO</option></select></label><br />");
  message += String( "<label>Password: <input type='text' /></label><br />");

  message += String( "<h2>Phant configuration</h2>");
  message += String( "<label>PhantHost: <input type='text' value='data.sparkfun.com' /></label><br />");
  message += String( "<label>PublicKey: <input type='text' value=''/></label><br />");
  message += String( "<label>PrivateKey: <input type='text' value=''/></label><br />");

  message += String( "<br /><br />");
  message += String( "<input type='submit' />");

  message += String( "<p><a href='/'>Go back home...</a></p>");
  server.send(200, "text/html", message);
}


void handleRoot() {
  Serial.println("[HTTP.Server] HTTP.Requst GET /");

  String message = "<h1>VHS: ESP + DS1820</h1>";

  float celsius = 0.0f;
  // Check to see if we can get the current Temperature 
  if( GetTemp (&celsius ) ){
    message += String( "<p>Celsius=") + String( celsius ) + String("</p>");    
  } else {
    message += String( "<p>Error: Could not get the current Temperature</p>");
  }

  message += String( "<p><a href='/config'>Configure this device...</a></p>");
  server.send(200, "text/html", message );
}

void connectWiFi()
{
  Serial.println(""); 
  Serial.print("[Wifi] Connecting to ");
  Serial.print(WiFiSSID);
    
  // Set WiFi mode to station (as opposed to AP or AP_STA)
  WiFi.mode(WIFI_STA);
  // WiFI.begin([ssid], [passkey]) initiates a WiFI connection
  // to the stated [ssid], using the [passkey] as a WPA, WPA2,
  // or WEP passphrase.
  WiFi.begin(WiFiSSID, WiFiPSK);
  
  // Use the WiFi.status() function to check if the ESP8266
  // is connected to a WiFi network.
  while (WiFi.status() != WL_CONNECTED)
  {
    // Blink the LED
    digitalWrite(BUILTIN_LED, ledStatus); // Write LED high/low
    ledStatus = (ledStatus == HIGH) ? LOW : HIGH;
    
    // Delays allow the ESP8266 to perform critical tasks
    // defined outside of the sketch. These tasks include
    // setting up, and maintaining, a WiFi connection.
    delay(100);
    // Potentially infinite loops are generally dangerous.
    // Add delays -- allowing the processor to perform other
    // tasks -- wherever possible.
    Serial.print(".");
  }
  Serial.println("");
  
  // Print the wifi connection details. 
  Serial.println("[Wifi] WiFi connected");
  Serial.print("[Wifi] IP address: "); 
  Serial.println(WiFi.localIP());  
}


