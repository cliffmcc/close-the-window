/*
 * Close The Window - copyright 2021 Cliff McCollum
 *
 * 1. kSERVER_URL: You'll need to request an API key from "api.weatherapi.com" that matches your local area
 * 2. kNOTIFICATION_URL: You'll need a notification URL key from IFTTT
 * 3. kWIFI_SSID: Your WiFi SSID
 * 4. kWIFI_PASS: Your WiFi password
 *
 */
#include <algorithm>
#include <Arduino.h>
#include <TimeLib.h>

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include "main.h"

#include "keys.h" // Create this file and place local copy of the four keys below

//const String kWIFI_SSID = "<wifiSID>";
//const String kWIFI_PASS = "<wifipassword>";
//const String kSERVER_URL = "http://api.weatherapi.com/v1/forecast.json?key=<api-key>&q=<location>&days=1&aqi=no&alerts=no";
//const String kNOTIFICATION_URL = "http://maker.ifttt.com/trigger/ConservatoryWindowOpen/with/key/<IFTT-key>";
const String kNTP_POOL = "europe.pool.ntp.org";
const int kWINDOW_PIN = 12; // also known as D6
const int kLED_PIN = 2; // also known as D4
const int kLOWEST_TEMP = 15;
const int kTIME_CUTOFF = 18;
const int kOpen = 0;
const int kMessagesPerDay = 3;
#ifdef _DEBUG_
const int kDelayTime = 5*1000; // 5 seconds
#else
const int kDelayTime = 15*60*1000; // 15 minutes
#endif

int gSwitch_value = kOpen;
int gMessages_today = 0;
bool gHaveTodayTemp = false;
int gTodayTemp = 0;

WiFiUDP ntpUDP;
NTPClient gNTPClient(ntpUDP, kNTP_POOL.c_str());

/*
 *  Get everything setup
 */
void setup() {
    setupHardware();
}

/*
 * Main Loop
 */
void loop() {
    connectNetwork();
    windowSwitchIsOpen(); // this will update the onboard LED to show window state each loop
    checkWindow();
    delay(kDelayTime);
}

/*
 *  Setup the hardware, including serial and GPIO
 */
void setupHardware() {
    pinMode(kWINDOW_PIN, INPUT_PULLUP);
    pinMode(kLED_PIN, OUTPUT);

    kOUTPUT_DEVICE.begin(115200); // Init serial comms

    // Clear Serial buffer
    DEBUG_WRAP(kOUTPUT_DEVICE.println();)
    DEBUG_WRAP(kOUTPUT_DEVICE.println("Clearing Serial buffer...");)
    DEBUG_WRAP(kOUTPUT_DEVICE.flush();)
}

/*
 *  Setup the network. Wifi using a static IP address
 */
void connectNetwork() {
    IPAddress staticIP(192,168,50,16);
    IPAddress gateway(192,168,50,1);
    IPAddress subnet(255,255,255,0);
    IPAddress dns1(208,67,220,220);
    IPAddress dns2(208,67,222,222);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(10);
    WiFi.config(staticIP, gateway, subnet, dns1, dns2);
    WiFi.begin(kWIFI_SSID, kWIFI_PASS);

    DEBUG_WRAP(kOUTPUT_DEVICE.setDebugOutput(true);)
    DEBUG_WRAP(WiFi.printDiag(kOUTPUT_DEVICE);)

    kOUTPUT_DEVICE.println();
    kOUTPUT_DEVICE.println("Connecting");

    DEBUG_WRAP(uint8_t macAddr[6];)
    DEBUG_WRAP(WiFi.macAddress(macAddr);)
    DEBUG_WRAP(kOUTPUT_DEVICE.printf("Mac address: %02x:%02x:%02x:%02x:%02x:%02x\n", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);)

    int wait_count = 0;
    while (WiFi.status() != WL_CONNECTED) {
        DEBUG_WRAP(kOUTPUT_DEVICE.printf(".%d.", WiFi.status());)
        wait_count++;
        if (wait_count % 10 == 0) {
            yield();
        }
    }
    wifi_station_set_auto_connect(true);

    DEBUG_WRAP(kOUTPUT_DEVICE.println("Connect success!");)
    DEBUG_WRAP(kOUTPUT_DEVICE.print("IP Address is: ");)
    DEBUG_WRAP(kOUTPUT_DEVICE.println(WiFi.localIP());)

    // Setup network time
    gNTPClient.begin();
    gNTPClient.setTimeOffset(0); // No offset is GMT
}


/*
 *  Read the state of the Window switch
 */
bool windowSwitchIsOpen() {
    int switchVal = digitalRead(kWINDOW_PIN);
    DEBUG_WRAP( kOUTPUT_DEVICE.printf("Window switch is: %d", switchVal); )
    if (switchVal == kOpen) {
        digitalWrite(kLED_PIN, LOW);
    }
    else {
        digitalWrite(kLED_PIN, HIGH);
    }

    return (switchVal == kOpen);
}

/*
 *  Main loop. Check if it's time to inspect our logic again, then take action.
 */
void checkWindow() {
    gNTPClient.update();
    int cur_hour = gNTPClient.getHours();

    DEBUG_WRAP(kOUTPUT_DEVICE.printf("Check hour: %d", cur_hour);)

    // Only do work if it's after cut-off time
    if (cur_hour >= kTIME_CUTOFF) {
        if (windowSwitchIsOpen()) {
            if (gMessages_today < kMessagesPerDay) {
                if (!gHaveTodayTemp) {
                    gTodayTemp = get_temperature_tonight();
                    DEBUG_WRAP(kOUTPUT_DEVICE.printf("Low temperature for today is %d\n", gTodayTemp);)
                }

                if (gTodayTemp < kLOWEST_TEMP) {
                    bool success = SendOpenNotification(gTodayTemp);
                    if (success) {
                        gMessages_today++;
                    }
                }
            }
            else {
                DEBUG_WRAP(kOUTPUT_DEVICE.println("Max messages already sent.");)
            }
        }
        else {
            DEBUG_WRAP(kOUTPUT_DEVICE.println("Window closed. Resetting everything.");)
            // If the window has been closed, reset everything - so we can operate more than once per day
            gMessages_today = 0;
            gHaveTodayTemp = false;
        }
    }
    else {
        // nothing to do right now
    }
}

/*
 *  Call the Weather API and parse out the minimal temperature for tonight.
 */
int get_temperature_tonight() {
    int temperature = -1;

    // configure and initiate connection with target server and url
    WiFiClient wifi_client;
    HTTPClient http_client;
    DEBUG_WRAP(kOUTPUT_DEVICE.print("[HTTP] starting...\n");)
    http_client.begin(wifi_client, kSERVER_URL);

    DEBUG_WRAP(kOUTPUT_DEVICE.print("[HTTP] GET...");)
    // start connection and send HTTP header
    int httpCode = http_client.GET();

    // httpCode will be negative on error. Success http code is 200
    if (httpCode == HTTP_CODE_OK) {
        // get tcp stream
        WiFiClient *stream = http_client.getStreamPtr();

        // On successful connection
        DEBUG_WRAP(kOUTPUT_DEVICE.print("[HTTP] Received HTML...\n");)

        // read all data from server
        char temperature_buffer[8];
        bool found_it = false;
        unsigned int temp_count = 0;

        temperature_buffer[0] = 0;
        found_it = stream->findUntil("\"mintemp_c\":", "}]}]}}");
        if (found_it) {
            temp_count = stream->readBytesUntil(',', temperature_buffer, sizeof(temperature_buffer));
            temperature_buffer[temp_count] = 0;
        } else {
            DEBUG_WRAP(kOUTPUT_DEVICE.print("No temperature found");)
        }

        DEBUG_WRAP(kOUTPUT_DEVICE.printf("temp chars %s\n", temperature_buffer);)

        if (temp_count > 0) {
            char *temp_end = nullptr;
            temperature = strtol(temperature_buffer, &temp_end, 10);
            DEBUG_WRAP(kOUTPUT_DEVICE.printf("temperature %d\n", temperature);)
        }

        DEBUG_WRAP(kOUTPUT_DEVICE.flush();)
    }
    else {
        DEBUG_WRAP(kOUTPUT_DEVICE.printf("[HTTP] failed, error: %s\n", http_client.errorToString(httpCode).c_str());)
    }
    http_client.end();

    return temperature;
}

/*
 *  Send a notification to my phone
 */
bool SendOpenNotification(int temperature) {
    // requests.post(iftt_url, params={"value1":"{}".format(temperature)})
    // configure and initiate connection with target server and url
    bool result = false;

    WiFiClient wifi_client;
    HTTPClient http_client;
    DEBUG_WRAP(kOUTPUT_DEVICE.print("[HTTP Send] starting...\n");)

    String post_URL = kNOTIFICATION_URL + "?value1=" + String(temperature);
    DEBUG_WRAP(kOUTPUT_DEVICE.printf("[HTTP Send] request: %s", post_URL.c_str()); )

    http_client.begin(wifi_client, post_URL);
    http_client.addHeader("Content-Type", "application/x-www-form-urlencoded");

    int httpCode = http_client.POST("");

    if (httpCode == HTTP_CODE_OK) {
        result = true;
    }
    else {
        DEBUG_WRAP(kOUTPUT_DEVICE.printf("[HTTP Send] failed, error: %d\n", httpCode);)
    }
    http_client.end();

    return result;
}
