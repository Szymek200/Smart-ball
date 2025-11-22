

#include <Wire.h>
#include <math.h>
#include <queue>
#include <fstream>
#include <utility>

#include "circularBuffer.h"
#include "normalize.h"
#include "stats.h"
#include "measurements.h"


//general info about ball
struct parameters
{
  TripleF speed;
  float height;
  bool offGround;
  
};


//current stats about the ball
std::queue<parameters> speedo;

//zapisywanie znormalizowanych danych
RingBuffer<Sample, 4096> circleBuff;

//kolejka eventow do zapisu 
std::deque< std::deque<Sample>> eventQueue;

//do pojedynczych pomiarow podczas lotu
std::deque<Sample> backgroundQueue;

float ballMass = 0.27;



  

//skala z jaka mierzy akcelerometr
#define AC_SCALE 6

storage  writer;
  measurements miernik;
  stats analyzer( &circleBuff, ballMass);

void setup() {

  //komunikacja I2C
  Wire.begin(21, 22);     // specify SDA, SCL for ESP32


  Serial.begin(115200);


  //3 argument - liczba slow stosu przydzielonego dla zadania.
  //4 - argumenty przydzielane przy starcie 
  //5 - piorytet zadania
  //6 - Wskaźnik, do którego FreeRTOS zapisze uchwyt do utworzonego zadania
  xTaskCreate(sensorTask, "Sensor", 4096, NULL, 3, NULL);
    xTaskCreate(analyzeTask, "Analyze", 8192, NULL, 2, NULL);
    xTaskCreate(writerTask, "Writer", 8192, NULL, 1, NULL);

}




//rownoleglosc
void sensorTask(void *pvParameters) {
    while(true) {
        Sample s = miernik.realMeasure();      // odczyt IMU
        circleBuff.push(s);
        vTaskDelay(1);                // mała pauza, np. 1 ms
    }
}

void analyzeTask(void *pvParameters) {
   while(true) {
        while(circleBuff.available() > 0) {
            Sample s = circleBuff.pop();

            analyzer.processSample(s);  // detect hits, update internal state

            // --- Enqueue events and background samples for writer ---
            if (analyzer.eventReady()) {
                 std::deque<Sample> hitEvent = analyzer.getEvent();      // deque<Sample>
                eventQueue.push_back(hitEvent);           // send full hit event
            }
            else if (analyzer.backgroundReady()) {
                backgroundQueue.push_back(s);             // send reduced-frequency background sample
            }
        }
        vTaskDelay(1); // yield CPU to other tasks
    }
}

void writerTask(void *pvParameters) {
    storage *writer = (storage*) pvParameters;

    while (true) {
        // 1. Full hit events (deque<Sample>)
        if (!eventQueue.empty()) {

            //caly event
            std::deque<Sample> evt = eventQueue.front();   // deque<Sample>
            eventQueue.pop_front();

            unsigned long duration = millis() - evt.front().timestamp; // or store actual duration
            writer->saveEvent(evt, duration);
            evt.clear(); // free memory
        }

        // 2. Occasional background samples
        if (!backgroundQueue.empty()) {
            auto s = backgroundQueue.front(); // single Sample
            backgroundQueue.pop_front();

            writer->saveSample(s);
        }

        vTaskDelay(5); // yield CPU
    }
}



void loop() {

}


