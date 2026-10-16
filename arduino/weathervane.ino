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
// #include <WiFiMulti.h>
// HTTP client doesn't connect.
// #include <HTTPClient.h>

// WiFiMulti wifiMulti;
const char *ssid = S_SSID;
const char *password = S_PASS;
const int power_pin = BAT_VOLT_PIN;

// if we got a report, do a full 6 hour sleep
const long full_sleep_secs = 6*60*60;

// Otherwise, just sleep 5 minutes and retry.
const long quick_nap_secs = 5*60;

#ifdef EPAPER_ENABLE // Only compile this code if the EPAPER_ENABLE is defined in User_Setup.h
EPaper epaper;
#endif

void setup()
{
    char error[100];
    Serial.begin(9600);

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
    client->stop();
    return result;
}

void loop()
{
    int err = 0;
    NetworkClient client;
    int buff_len = 0;
    String buffer("");
    char error[100];
    int nl = 0;

    long sleep_duration = quick_nap_secs;

    // Why am I doing this manually instead of using some fancy HTTPs library?
    // Because dependency hell.
    if (!client.connect("10.10.1.123", 12345))
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
    epaper.setTextColor(TFT_GREEN);
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
        if (token[0] == '{')
        {
            JSONVar report = JSON.parse(token);
            // epaper.drawString("Parsed report", 10, nl+=20);
            epaper.setTextSize(5);
            epaper.setTextColor(TFT_BLACK);
            epaper.drawString((const char *)report["today"], 10, nl += 20);
            epaper.setTextSize(2);
            epaper.drawString((const char *)report["date"], 10, nl += 55);
            nl += 25;
            epaper.setTextSize(3);
            epaper.setTextColor(TFT_BLUE);
            epaper.drawString((const char *)report["forecast"][0]["name"], 10, nl += 35);
            epaper.setTextSize(2);
            epaper.setTextColor(TFT_BLACK);
            epaper.drawString((const char *)report["forecast"][0]["long"], 10, nl += 25);
            epaper.setTextSize(3);
            epaper.setTextColor(TFT_BLUE);
            epaper.drawString((const char *)report["forecast"][1]["name"], 10, nl += 35);
            epaper.setTextSize(2);
            epaper.setTextColor(TFT_BLACK);
            epaper.drawString((const char *)report["forecast"][1]["long"], 10, nl += 25);
            epaper.setTextSize(3);
            epaper.setTextColor(TFT_BLUE);
            epaper.drawString((const char *)report["forecast"][2]["name"], 10, nl += 35);
            epaper.setTextSize(2);
            epaper.setTextColor(TFT_BLACK);
            epaper.drawString((const char *)report["forecast"][2]["long"], 10, nl += 25);

            nl += 50;
            {
                char line[200];
                epaper.setTextColor(TFT_GREEN);
                epaper.setTextSize(3);
                sprintf(line, "Sunrise: %s", (const char *)report["sunrise"]);
                epaper.drawString(line, 10, nl += 35);
                sprintf(line, "Noon: %s", (const char *)report["transit"]);
                epaper.drawString(line, 10, nl += 35);
                sprintf(line, "Sunset: %s", (const char *)report["sunset"]);
                epaper.drawString(line, 10, nl += 35);
            }

            // Got a report, we can sleep for a while.
            sleep_duration = full_sleep_secs;
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
        char line[32];
        epaper.setTextColor(TFT_RED);
        epaper.setTextSize(2);
        sprintf(line, "Battery: %02.2f %02.2f%%", val, (val/8158 * 100));
        epaper.drawString(line, 500, 10);
    }

    /*epaper.fillCircle(25, 25, 15, TFT_RED);
    epaper.fillRect(epaper.width() - 40,  10, 30, 30, TFT_GREEN);

    for (int i = 0; i < epaper.height() / 80; i++)
    {
        epaper.setTextColor(TFT_BLUE);
        epaper.setTextSize(i + 1);
        epaper.drawLine(10, 70 + 60 * i, epaper.width() - 10, 70 + 60 * i, TFT_BLACK);
        epaper.drawString("Hello ePaper", 10, 80 + 60 * i);
    }
    */

    epaper.update(); // update the display

#endif

    // TODO: Get the real clock time from either the HTTP request
    // or some other source
    // Sleep for 6 hours
    esp_sleep_enable_timer_wakeup(sleep_duration*1000000ULL);
    esp_deep_sleep_start();
}
