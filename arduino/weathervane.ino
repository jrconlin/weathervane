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
const int power_pin = BAT_VOLT_PIN;

// if we got a report, do a full 4 hour sleep
const long full_sleep_secs = 4*60*60;

// Otherwise, just sleep 5 minutes and retry.
const long quick_nap_secs = 5*60;

// WiFiUDP Udp;

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

    pinMode(power_pin, OUTPUT);
    // Udp.begin(localPort);
    // t_now = getNtpTime();
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

int drawWeather(EPaper epaper, JSONVar report, int r_num, int nl) {
    epaper.setTextSize(3);
    epaper.setTextColor(TFT_BLUE);
    epaper.drawString((const char *)report["forecast"][r_num]["name"], 10, nl += 35);
    epaper.setTextSize(2);
    epaper.setTextColor(TFT_BLACK);
    for (int i=0; i < report["forecast"][r_num]["long"].length(); i++){  
        epaper.drawString((const char *)report["forecast"][r_num]["long"][i], 10, nl += 25);
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
    epaper.drawString(WiFi.localIP().toString(), 10, nl+=0);
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
            epaper.drawString((const char *)report["today"], 10, nl += 20);
            epaper.setTextSize(2);
            epaper.setTextColor(TFT_YELLOW);
            sprintf(line, "Updated: %s", (const char *)report["date"]);
            epaper.drawString(line, 350, nl+10);
            // sprintf(line, "seconds: %d", refresh);
            sleep_duration = refresh;
            nl += 20;

            nl = drawWeather(epaper, report, 0, nl);
            
            // The following are pretty much exactly what is in `drawWeather` but the app crashes when
            // I call them in that funciton. Zero idea why, so doing an inplace for now.
            //*
            {
                r_num = 1;
                epaper.setTextSize(3);
                epaper.setTextColor(TFT_BLUE);
                nl += 35;
                //sprintf(line, "%s %s %d°F",(const char *)report["forecast"][r_num]["name"], (const char *)report["forecast"][r_num]["short"], (int)report["forecast"][r_num]["temp"]);
                //epaper.drawString(line, 10, nl);                
                epaper.drawString((const char *)report["forecast"][r_num]["name"], 10, nl);                
                epaper.setTextSize(2);
                epaper.setTextColor(TFT_BLACK);
                for (int i=0; i < report["forecast"][r_num]["long"].length(); i++){
                    epaper.drawString((const char *)report["forecast"][r_num]["long"][i], 10, nl += 25);
                }
            }
            // */
            //*
            {
                r_num = 2;
                epaper.setTextSize(3);
                epaper.setTextColor(TFT_BLUE);
                nl += 35;
                epaper.drawString((const char *)report["forecast"][r_num]["name"], 10, nl);
                epaper.setTextSize(2);
                epaper.setTextColor(TFT_BLACK);
                for (int i=0; i < report["forecast"][r_num]["long"].length(); i++){
                    epaper.drawString((const char *)report["forecast"][r_num]["long"][i], 10, nl += 25);
                }
            }
            // */

            nl = 400;
            {
                epaper.setTextColor(TFT_GREEN);
                epaper.setTextSize(3);
                sprintf(line, "Sunrise: %s", (const char *)report["sunrise"]);
                epaper.drawString(line, 10, nl);
                sprintf(line, "Sunset: %s", (const char *)report["sunset"]);
                epaper.drawString(line, 400, nl);
                sprintf(line, "Noon: %s", (const char *)report["transit"]);
                epaper.drawString(line, 10, nl += 35);
            }

            // Got a report, we can sleep for a while.
            // sleep_duration = full_sleep_secs;
            break;
        }
        // epaper.drawString(token, 10, nl+=20);
        token = strtok(NULL, "\n");
    }

    // Display the battery voltage
    // TODO: convert to percentage.
    // AFAIK: 8158 is the full value / 302 is the empty value. 
    float val = 0;
    val = analogRead(power_pin) * 2;
    {
        epaper.setTextColor(TFT_RED);
        epaper.setTextSize(2);
        sprintf(line, "Battery: %02.2f %02.2f%%", val, (val/8158 * 100));
        epaper.drawString(line, 500, 0);
    }
    epaper.update(); // update the display

#endif

    // TODO: Get the real clock time from either the HTTP request
    // or some other source
    // Sleep for 6 hours
    esp_sleep_enable_timer_wakeup(sleep_duration*1000000ULL);
    esp_deep_sleep_start();
}
