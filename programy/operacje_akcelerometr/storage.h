#ifndef STORAGE
#define STORAGE

#include "circularBuffer.h"
#include <fstream>
#include <deque>



class storage
{
     std::ofstream file;

    //we are saving everything to flash
    //during hit
    bool allSave = 0;

    int precision;

    public:

    // Constructor opens the file
    storage(int precision = 49) 
        : file("pomiary.bin", std::ios::binary | std::ios::app), 
          precision(precision)
    {
        if (!file.is_open()) {
            // handle error
            throw std::runtime_error("Cannot open file pomiary.bin");
        }
    }

    
    ~storage()
    {
        if (file.is_open())
            file.close();
    }



    void saveFlash();

void sendWifi();

void saveEvent(std::deque<Sample> &event, unsigned long duration);

void saveSample(const Sample &s);

};

#endif