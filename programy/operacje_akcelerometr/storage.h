#ifndef STORAGE
#define STORAGE

#include "circularBuffer.h"
#include <fstream>
#include <deque>
#include <WiFi.h>



class storage
{
     std::ofstream file;

    //we are saving everything to flash
    //during hit
    bool allSave = 0;

    int precision;

    const char *apSSID = "SmartBall";
    const char *apPassword = "12345678";  // or empty for open AP

    WiFiServer wifiServer;  // any port you like
    WiFiClient wifiClient;

    void setupWiFiAP();

    public:

    // Constructor opens the file
    storage(int precision = 49) 
        : file("pomiary.bin", std::ios::binary | std::ios::app), 
          precision(precision),  wifiServer(4567)
    {
        if (!file.is_open()) {
            // handle error
            throw std::runtime_error("Cannot open file pomiary.bin");
        }

      
         setupWiFiAP();
    }

    
    ~storage()
    {
        if (file.is_open())
            file.close();
    }



    void saveFlash();



void saveEvent(std::deque<Sample> &event, unsigned long duration);

void saveSample(const Sample &s);

void sendEventWifi(const std::deque<Sample> &event, unsigned long duration);
void sendSampleWifi(const Sample &s);
void sendWifi(); // check connection loop

};

#endif