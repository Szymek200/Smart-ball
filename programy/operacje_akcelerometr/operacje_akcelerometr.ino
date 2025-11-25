

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
RingBuffer<Sample, RINGSIZE> circleBuff;

//kolejka eventow do zapisu 
//std::deque< std::deque<Sample>> eventQueue;
QueueHandle_t eventQueue = xQueueCreate(32, sizeof(Sample));
QueueHandle_t backgroundQueue = xQueueCreate(128, sizeof(Sample));

//do pojedynczych pomiarow podczas lotu
//std::deque<Sample> backgroundQueue;

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
  Serial.print("booting");

Serial.printf("Total heap: %u\n", ESP.getHeapSize());
Serial.printf("Free heap: %u\n", ESP.getFreeHeap());
//Serial.printf("Max free block: %u\n", ESP.getMaxFreeBlockSize());
Serial.print("Total heap: ");
Serial.println(ESP.getHeapSize());      // total heap (DRAM)
Serial.print("Free heap: ");
Serial.println(ESP.getFreeHeap());      // free heap
Serial.print("Largest free block: ");
Serial.println(ESP.getMaxAllocHeap());  // largest contiguous allocatable block

  //3 argument - liczba slow stosu przydzielonego dla zadania.
  //4 - argumenty przydzielane przy starcie 
  //5 - piorytet zadania
  //6 - Wskaźnik, do którego FreeRTOS zapisze uchwyt do utworzonego zadania
  xTaskCreate(sensorTask, "Sensor", 2048, NULL, 3, NULL);//4096
    xTaskCreate(analyzeTask, "Analyze", 4096, NULL, 2, NULL);//8192
    xTaskCreate(writerTask, "Writer", 4096, NULL, 1, NULL);

    

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
                //eventQueue.push_back(hitEvent);           // send full hit event
                 xQueueSend(eventQueue, &s, 0);
            }
            else if (analyzer.backgroundReady()) {
                //backgroundQueue.push_back(s);             // send reduced-frequency background sample
                xQueueSend(backgroundQueue, &s, 0);
            }
        }
        vTaskDelay(1); // yield CPU to other tasks
    }
}

void writerTask(void *pvParameters) {
    storage *writer = (storage*) pvParameters;
    Sample s;                         // for background samples
    std::deque<Sample> evt;           // for hit events


     while (true) {
        // 1. Full hit events
        if (xQueueReceive(eventQueue, &evt, 0) == pdTRUE) {  // non-blocking
            if (!evt.empty()) {
                unsigned long duration = millis() - evt.front().timestamp; // or store actual
               // writer->saveEvent(evt, duration);
                writer->sendEventWifi(evt, duration);
                evt.clear();
            }
        }

        // 2. Occasional background samples
        if (xQueueReceive(backgroundQueue, &s, 0) == pdTRUE) {  // non-blocking
            //writer->saveSample(s);
            writer->sendSampleWifi(s);
        }

        vTaskDelay(5); // yield CPU
    }
}



void loop() {

}


