#include <FS.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>

// project settings
String prName = "Cordel";
String prId = "cordel";

// wifi setup
bool modeAp = false;
String apSsid = prId;
String apPassword = "a9b8c7d6e5";
String ssid = "";
String password = "";

// preparing webserver
ESP8266WebServer server(80);
const int apLed = D5;
const int staLed = D4;
const int resetBt = D2;
int busyLed;
int modeLed;
bool buttonState = false;

// led display
const int rgbLed = D6;
const int totalLed = 10;
int counter = 0;
StaticJsonDocument<800> ledInfo;
Adafruit_NeoPixel pixels(totalLed, rgbLed, NEO_GRB + NEO_KHZ800);

// sketch initialize
void setup() 
{
  // initializing board
  Serial.begin(9600);
  Serial.println("");
  Serial.println(prName + " is starting...");
  pinMode(apLed, OUTPUT);
  digitalWrite(apLed, LOW);
  pinMode(staLed, OUTPUT);
  digitalWrite(staLed, LOW);
  pinMode(resetBt, INPUT);
  pinMode(rgbLed, OUTPUT);
  Serial.println("");
  
  // initializing file system
  SPIFFS.begin();

  // loading saved configuration
  Serial.print("Loading configuration... ");
  getWifiMode();
  getLedInfo();
  Serial.println("");

  // initializing wifi
  if (modeAp) {
    // set led output
    busyLed = staLed;
    modeLed = apLed;
    // start as an access point
    Serial.println("Starting access point...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSsid, apPassword);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("Access point ");
    Serial.print(apSsid);
    Serial.print(" started with password ");
    Serial.println(apPassword);
    Serial.print("Host address: ");
    Serial.println(IP);
    Serial.println("");
    digitalWrite(modeLed, HIGH);
  } else {
    // set led output
    busyLed = apLed;
    modeLed = staLed;
    // connect to an existing wifi network
    Serial.print("Starting wifi");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.println("");
    digitalWrite(modeLed, HIGH);
  }

  // initializing mDNS
  Serial.println("Starting mDNS");
  if (MDNS.begin(prId)) {
    Serial.println("mDNS started at http://" + prId + ".local - no Android support, sorry :-(");
  } else {
    Serial.println("failed to start mDNS - sorry :-(");
  }
  Serial.println("");

  // initilizing leds
  Serial.println("Starting led display...");
  Serial.print("Mode: ");
  String dmode = ledInfo["mode"];
  Serial.println(dmode);
  pixels.begin();
  showLed();
  Serial.println("");

  /* DEFAULT ROUTING: NO NEED TO CHANGE */

  // basic index route
  server.on("/", onIndex);
  server.on("/index.html", onIndex);

  // wifi setup route
  server.on("/wifi.html", onWifi);

  // no assigned route found: try to send the corresponding file to client
  server.onNotFound(onNoRoute);

  // starting server
  Serial.println("Starting webserver...");
  server.begin();
  Serial.println("I´M READY!!!");
}

// reset board function
void(* resetFunc) (void) = 0; //declare reset function @ address 0

// sketch running
void loop() 
{
  // check out server requests
  server.handleClient();
  MDNS.update();
  // checking the reset button
  buttonState = digitalRead(resetBt);
  if (buttonState == LOW) {
    // wait for 1 second and check again
    delay(1000);
    // still pressed?
    buttonState = digitalRead(resetBt);
    if (buttonState == LOW) {
      // wait for another 1 second
      delay(1000);
      // still pressed?
      buttonState = digitalRead(resetBt);
      if (buttonState == LOW) {
        // user really wants to reset configuration
        //Serial.println("RESETING CONFIGURATION!");
        //digitalWrite(modeLed, LOW);
        //setWifiModeAp();
      }
    }
  }
}

/** COMMON ROUTE ACTIONS: NO NEED TO CHANGE FROM HERE **/

// main index route
void onIndex()
{
  // receiving post info?
  if (server.arg("ac") == "setcolor") {
     Serial.println("Receiving led setup.");
    // checking colors
    int red = server.arg("red").toInt();
    int green = server.arg("green").toInt();
    int blue = server.arg("blue").toInt();
    // setting each led
    for (int i=0; i<totalLed; i++) {
      if (server.arg("led" + String(i)) == "1") {
        ledInfo["led"][i]["r"] = red;
        ledInfo["led"][i]["g"] = green;
        ledInfo["led"][i]["b"] = blue;
      }
    }
    // saving led setup
    String jsonText;
    serializeJson(ledInfo, jsonText);
    File fl = SPIFFS.open("/ledconf/colors", "w+");
    fl.println(jsonText);
    fl.close();
    Serial.println("New led setup saved. Applying now!");
    // showing new led setup
    showLed();
  }
  // resetting board?
  if (server.arg("ac") == "reset") {
    resetFunc();
  }
  // finally, send the index file
  sendFile("/index.html");
}

// wifi setup
void onWifi()
{
  // action set?
  if (server.arg("ac") == "ap") {
    // change wifi mode to access point
    setWifiModeAp();
  } else if (server.arg("ac") == "sta") {
    // connection to an existing wifi network
    String wfSsid = server.arg("ssid");
    String wfPass = server.arg("password");
    if ((wfSsid != "") && (wfPass != "")) {
      setWifiModeSta(wfSsid, wfPass);
    }
  }
  // show wifi interface
  sendFile("/wifi.html");
}

// no defined route: just send the corresponding file
void onNoRoute()
{
  // just try to send the requested file
  sendFile(server.uri());
}

// sending a file to the connected client
void sendFile(String path)
{
  // only lower case
  path.toLowerCase();
  // visual output
  digitalWrite(busyLed, HIGH);
  // does the route file exist?
  if(SPIFFS.exists(path) || SPIFFS.exists(path + ".gz")) {
    // check mime type
    String mime = "text/html";
    bool textfile = true;
    if (path.endsWith(".css")) mime = "text/css";
    if (path.endsWith(".js")) mime = "application/javascript";
    if (path.endsWith(".txt")) mime = "text/plain";
    if (path.endsWith(".jpg")) {
      mime = "image/jpeg";
      textfile = false;
    }
    if (path.endsWith(".jpeg")) {
      mime = "image/jpeg";
      textfile = false;
    }
    if (path.endsWith(".png")) {
      mime = "image/png";
      textfile = false;
    }
    if (path.endsWith(".gif")) {
      mime = "image/gif";
      textfile = false;
    }
    // sending file to client
    if (textfile) {
      if (SPIFFS.exists(path + ".gz")) {
        // sending compressed text file
        File txt = SPIFFS.open((path + ".gz"),"r");
        server.streamFile(txt, mime);
        txt.close();
      } else {
        // sending plain text file
        File txt = SPIFFS.open(path,"r");
        server.streamFile(txt, mime);
        txt.close();
      }
    } else {
      // send binary file
      File fl = SPIFFS.open(server.uri(), "r");
      server.streamFile(fl, mime);
      fl.close();
    }
  } else {
    // file not found
    String message = "ROUTE ERROR\n\n";
    message += "route: ";
    message += path;
    message += "\nmethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\narguments: ";
    message += server.args();
    message += "\n";
    for (uint8_t i = 0; i < server.args(); i++) {
      message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    server.send(404, "text/plain", message);
  }
  // visual output
  digitalWrite(busyLed, LOW);
}

/** DATA HANDLING **/

// get current wifi mode settings
void getWifiMode()
{
  // do ssid and password files exist?
  if (SPIFFS.exists("/wifimode/ssid") && SPIFFS.exists("/wifimode/pass")) {
    // get info to connect to an existing wifi network
    Serial.print("wifi configuration files found... ");
    String wfSsid = "";
    String wfPass = "";
    File fl = SPIFFS.open("/wifimode/ssid", "r");
    wfSsid = fl.readStringUntil('\r');
    fl.close();
    fl = SPIFFS.open("/wifimode/pass", "r");
    wfPass = fl.readStringUntil('\r');
    fl.close();
    // information really available?
    if ((wfSsid != "") && (wfPass != "")) {
      // connect to an wifi network
      ssid = wfSsid;
      password = wfPass;
      Serial.println("connect to wifi " + ssid + "!");
      modeAp = false;
    } else {
      // no information: start as an access point
      Serial.println("failed to load configuration files.");
      modeAp = true;
    }
  } else {
    // mode set to access point
    Serial.println("no wifi configuration files found.");
    modeAp = true;
  }
}

// get current led settings
void getLedInfo()
{
  // does the led color file exist?
  if (SPIFFS.exists("/ledconf/colors")) {
    // get saved led configuration
    Serial.println("Led configuration file found... ");
    File fl = SPIFFS.open("/ledconf/colors", "r");
    String ledLoaded = fl.readString();
    fl.close();
    // try to parse loaded string
    DeserializationError error = deserializeJson(ledInfo, ledLoaded);
    if (error) {
      Serial.println("Error on led configuration file... all white! ");
      initLed();
    } else {
      Serial.println("Led configuration file sucessfully loaded!");
    }
  } else {
    // leds all white
    Serial.println("No led configuration file found... ");
    initLed();
  }
}

// set the connection mode to an access point
void setWifiModeAp()
{
  if (SPIFFS.exists("/wifimode/ssid")) SPIFFS.remove("/wifimode/ssid");
  if (SPIFFS.exists("/wifimode/pass")) SPIFFS.remove("/wifimode/pass");
  Serial.println("Wifi mode changed to access point.");
  resetFunc();
}

// set the connection mode to wifi network client
void setWifiModeSta(String wfSsid, String wfPass)
{
  File fl = SPIFFS.open("/wifimode/ssid", "w+");
  if (!fl) {
    // can't set wifi mode: nothing to do...
    Serial.println("Can't change wifi settings. Using access point.");
  } else {
    fl.println(wfSsid);
    fl.close();
    File fl = SPIFFS.open("/wifimode/pass", "w+");
    if (!fl) {
      // can't set wifi mode: nothing to do...
      Serial.println("Can't change wifi settings. Using access point.");
    } else {
      fl.println(wfPass);
      fl.close();
      Serial.println("Wifi mode changed: connect to " + wfSsid + ".");
    }
  }
  resetFunc();
}

/** LED DISPLAY HANDLING **/

// initialize leds withh all-white display
void initLed()
{
  String ledLoaded = "{\"mode\":\"ind\",\"led\": [{\"r\": 255,\"g\":255,\"b\":255},{\"r\": 255,\"g\":255,\"b\":255},{\"r\": 255,\"g\":255,\"b\":255},{\"r\": 255,\"g\":255,\"b\":255},{\"r\": 255,\"g\":255,\"b\":255},{\"r\": 255,\"g\":255,\"b\":255},{\"r\": 255,\"g\":255,\"b\":255},{\"r\": 255,\"g\":255,\"b\":255},{\"r\": 255,\"g\":255,\"b\":255},{\"r\": 255,\"g\":255,\"b\":255}]}";
  deserializeJson(ledInfo, ledLoaded);
  File fl = SPIFFS.open("/ledconf/colors", "w+");
  fl.println(ledLoaded);
  fl.close();
  Serial.println("Led configuration file reset!");
}

// adjust led display
void showLed()
{
  // clear previous display setup
  pixels.clear();
  // get each led setup
  Serial.println("Changing led colors:");
  String ledset = "";
  int lcolor;
  for (counter = 0; counter < totalLed; counter++) {
    ledset = "";
    pixels.setPixelColor(counter, pixels.Color(ledInfo["led"][counter]["r"], ledInfo["led"][counter]["g"], ledInfo["led"][counter]["b"]));
    ledset.concat(counter);
    ledset.concat(" => ");
    lcolor = ledInfo["led"][counter]["r"];
    ledset.concat(lcolor);
    ledset.concat(", ");
    lcolor = ledInfo["led"][counter]["g"];
    ledset.concat(lcolor);
    ledset.concat(", ");
    lcolor = ledInfo["led"][counter]["b"];
    ledset.concat(lcolor);
    Serial.println(ledset);
  }
  // apply defined setup
  pixels.show();
  Serial.println("Led display updated!");
}
