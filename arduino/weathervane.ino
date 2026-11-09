/*
Display the weather as provided by an HTTP service.

Ideally, this will draw directly from the NWS, but it's all very iterative, since holy
hell is Arduino top notch at dependency hell.



Here is the 6 colors you can display:
1.TFT_WHITE
2.TFT_BLACK
3.TFT_YELLOW
4.TFT_GREEN
5.TFT_BLUE
6.TFT_RED
*/

#include "arduino_secrets.h"
#include "TFT_eSPI.h"
#include "esp_wifi.h"

#include <Adafruit_GFX.h>
#include "font/arial8.h"

#include <Arduino.h>
#include <Arduino_JSON.h>
#include <EthernetClient.h>

#include <WiFi.h>

#ifdef NTP
//#include <WiFiUdp.h>
#endif

#include <time.h>
// #include <WiFiMulti.h>

// HTTP client doesn't connect.
// #include <HTTPClient.h>

// WiFiMulti wifiMulti;

/*
 # Fun Failures
 or why I swear like I do and use a python based "helper" app.

 ## Serial Failures
 ArduinoIDE fails to read the serial port correctly, regardless of what
 baud is specified, resulting in garbage. This means coding blind. whee.

 ## HTTP Failures
 HTTPClient will get built, but for whatever reason either cannot 
 connect to the server, or does not send any data through. Either way,
 I'm doing things via raw connections because of that.

 Using one of the alternate libraries gets me into dependency hell.

 ## Time Failures
 While I can (technically) set up UDP, the UDP request to the NTP
 server times out and nothing gets returned. Since I'm flying blind
 I have no idea what the fail is, but it's there.
 As an added bonus, `strptime` returns a broken down `tm` struct.
 There's nothing that can convert a `tm` struct to a `time_t` numeric.
 `mktime()` is undefined on Arduino, and most of the time libraries
 are internally broken. I really, really don't want to do that by 
 hand because both Leap years and Leap seconds are things.
 Because of that, I'm passing the time via the JSON structure, 
 since that appears to still work.

*/
const char *ssid = S_SSID;
const char *password = S_PASS;

// if we got a report, do a full 4 hour sleep
const long full_sleep_secs = 2*60*60;

// Otherwise, just sleep 5 minutes and retry.
const long quick_nap_secs = 5*60;

#define BAT_MON_PIN 21
#define BAT_READ_PIN 1

#ifdef EPAPER_ENABLE // Only compile this code if the EPAPER_ENABLE is defined in User_Setup.h
EPaper epaper;
#endif

void setup()
{
    char error[100];
    Serial.begin(115200);

    Serial.println();

    Serial.println("\nOK");
    Serial.flush();

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        // Serial seems to dump garbage
        Serial.print(".");
    }

    // Set the battery monitoring pin modes.
    // (You would think that BAT_MON_PIN, which is written to, 
    //  would be INPUT. You would be wrong.)
    pinMode(BAT_MON_PIN, OUTPUT);
    pinMode(BAT_READ_PIN, OUTPUT);

}

int len(char *str)
{
    int l = 0;
    while (*str + l != 0)
    {
        l++;
    }
    return l;
}

String response(NetworkClient *client)
{
    String result;
    while (client->available() == 0)
    {
        delay(100);
    }

    while (client->available())
    {
        result += (client->readStringUntil('\n')) + "\n";
    }
    delay(200);
    client->stop();
    return result;
}

int drawWeather(EPaper *epaper, JSONVar report, int r_num, int nl) {
    epaper->setTextSize(3);
    epaper->setTextColor(TFT_BLUE);
    nl += 33;
    //sprintf(line, "%s %s %d°F",(const char *)report["forecast"][r_num]["name"], (const char *)report["forecast"][r_num]["short"], (int)report["forecast"][r_num]["temp"]);
    //epaper.drawString(line, 10, nl);                
    epaper->drawString((const char *)report["forecast"][r_num]["name"], 10, nl);                
    epaper->setTextSize(2);
    epaper->setTextColor(TFT_BLACK);
    for (int i=0; i < report["forecast"][r_num]["long"].length(); i++){
        epaper->drawString((const char *)report["forecast"][r_num]["long"][i], 10, nl += 20);
    }
    return nl;
}

#ifdef HTTP_TIME
// Broken string time parser
time_t parseTimeString(char * timeString) {
    struct tm tm = {0};
    time_t result;
    strptime(timeString, "%A, %d %B %Y %H:%M:%S %Z", &tm);
    result = maketm(&tm);
    return localtime(&result);  
}
#endif

void loop()
{
    int err = 0;
    NetworkClient client;
    char line[200];
    int buff_len = 0;
    String buffer("");
    char error[100];
    int nl = 0;

    time_t req_time = 0;

    long sleep_duration = quick_nap_secs;

    // Why am I doing this manually instead of using some fancy HTTPs library?
    // Because dependency hell.
    if (!client.connect("10.10.1.198", 12345))
    {
        sprintf(error, "Cannot connect\n");
        buffer += error;
    }
    else
    {
        client.print("GET / HTTP/1.1\r\nConnection: close\r\n\r\n");
        buffer += response(&client);
        // parse_response(buffer);
    }

    Serial.flush();

#ifdef EPAPER_ENABLE
    epaper.begin();
    epaper.fillScreen(TFT_WHITE);

    epaper.setTextSize(2);
    epaper.setTextColor(TFT_YELLOW);
    epaper.drawString(WiFi.localIP().toString(), 350, nl+=0);
    epaper.setTextColor(TFT_BLACK);
    epaper.setTextSize(2);

    
    char *token = strtok(buffer.begin(), "\n");
    while (token != NULL)
    {
        if (nl > 480)
        {
            break;
        }
#ifdef HTTP_DATE
        if (String(token).startsWith("Date: ")) {
            
            char * dateString = token.substr(token.indexOf(' '), len(token));
            t_now = parseTime(dateString);

        }
#endif
        if (token[0] == '{')
        {
            int r_num = 0;
            JSONVar report = JSON.parse(token);
            // epaper.drawString("Parsed report", 10, nl+=20);
            long refresh = (long)report["refresh"];
            epaper.setTextSize(5);
            epaper.setTextColor(TFT_BLACK);
            epaper.drawString((const char *)report["today"], 10, nl += 5);
            epaper.setTextSize(1);
            epaper.setTextColor(TFT_BLACK);
            sprintf(line, "%s", (const char *)report["date"]);
            epaper.drawString(line, 650, 18);
            sprintf(line, "seconds: %d", refresh);
            sleep_duration = full_sleep_secs;
            nl += 20;

            // The following are pretty much exactly what is in `drawWeather` but the app crashes when
            // I call them in that funciton. Zero idea why, so doing an inplace for now.
            for (int r_num=0; r_num < 3; r_num++)
            {
                epaper.setTextSize(3);
                epaper.setTextColor(TFT_BLUE);
                nl += 35;
                //sprintf(line, "%s %s %d°F",(const char *)report["forecast"][r_num]["name"], (const char *)report["forecast"][r_num]["short"], (int)report["forecast"][r_num]["temp"]);
                //epaper.drawString(line, 10, nl);                
                epaper.drawString((const char *)report["forecast"][r_num]["name"], 10, nl);
                nl += 2;
                epaper.setTextSize(2);
                epaper.setTextColor(TFT_BLACK);
                for (int i=0; i < report["forecast"][r_num]["long"].length(); i++){
                    epaper.drawString((const char *)report["forecast"][r_num]["long"][i], 10, nl += 21);
                }
            }

            // Show the sunrise / sunset footer
            nl = 450;
            {
                epaper.fillRect(0,nl-5,800,480,TFT_WHITE);
                epaper.setTextColor(TFT_GREEN);
                epaper.setTextSize(3);
                epaper.drawString((const char *)report["sunrise"], 10, nl);
                epaper.drawString((const char *)report["transit"], 300, nl);
                epaper.drawString((const char *)report["sunset"], 630, nl);
            }

            // Got a report, we can sleep for a while.
            // sleep_duration = full_sleep_secs;
            break;
        }
        // epaper.drawString(token, 10, nl+=20);
        token = strtok(NULL, "\n");
    }

    // Display the battery level.
    // Right, so, Arduino treats GPIO as pin numbers. Even though they distinguish
    // Analog and Digital pins.
    // The following code is how you read the battery for e100* displays. 
    // This is a very low voltage read process, to preserve battery life. 
    // (See https://www.hackster.io/melastmohican/driving-the-reterminal-e1002-7-3-color-e-paper-with-arduino-21070f#toc-zero-drain-battery-measurement-15)
    {
        float val = 0;
        // Set the battery monitor pin (GPIO21) to high to enable the voltage read
        digitalWrite(BAT_MON_PIN, HIGH);
        // wait a few ticks for the value to populate.
        delay(10);
        // Read the voltage from GPIO1
        int rawVolt = analogRead(BAT_READ_PIN);
        // Reset the BAT_MON_PIN to low to reduce voltage use.
        digitalWrite(BAT_MON_PIN, LOW);
        
        // Funky fun math to convert the raw voltage read number to something useful.
        // this divides the raw read by the battery capacity, times the voltage provided, 
        // times 2 (because of the buck circuit(?))
        float power = (rawVolt / 4095.0f) * 3.3f * 2.0f;
        epaper.setTextColor(TFT_RED);
        epaper.setTextSize(2);
        // Get the percentage of battery available.
        float level = (power / 4.15) * 100;
        sprintf(line, "%02.2f%% (%02.2f)", level, power);
        epaper.drawString(line, 645, 0);
    }
    epaper.update(); // update the display

#endif

    // TODO: Get the real clock time from either the HTTP request
    // or some other source
    // Sleep for 6 hours
    esp_sleep_enable_timer_wakeup(sleep_duration*1000000ULL);
    esp_deep_sleep_start();
}
