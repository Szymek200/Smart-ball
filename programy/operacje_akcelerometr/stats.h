#ifndef STATS
#define STATS

#include "circularBuffer.h"
#include "storage.h"
#include "queue"
#include "math.h"
#include <Arduino.h>




class stats
{
    float ballMass = 0.250; //in kg
    float maxAcceleration = 0;
    float maxRotation = 0;
 

    RingBuffer<Sample, RINGSIZE> * ring;

    //copy of samples when hit detected
    std::deque<Sample> event;
    bool hasEventReady = false; // flag

    const int PRE_SAMPLES  = 1000;
    const int POST_SAMPLES = 1500;
    const int   MIN_POST_SAMPLES    = 50;     // optional minimum duration
   int postRemaining = 0;


    //zapisywanie, gdy nie ma uderzenia
    int normalCounter = 0;
const int NORMAL_SAMPLE_SKIP = 100;  // save 1 sample every 100

    //if we are currently during hit
    bool hitActive = 0; 
    const float HIT_START_THRESHOLD = 12.0;
    const float HIT_END_THRESHOLD = 3.0;

    unsigned long hitStartTime;
      unsigned long hitDuration;

    bool flightIs = 0;

    void startHitEvent();

void endHitEvent();

//calculation methods
TripleF mulvs( TripleF data, float scalar);

float vectorLenght(TripleF r);

bool isHit(TripleF accW);

  public:

    stats(RingBuffer<Sample, RINGSIZE> * ring, float ballMass)
    {
      this->ballMass = ballMass;
  
      this->ring = ring;
      
    }

    //wszystkie poboczne uslugi
void generalStats();

float magnitude(TripleF data, float duration);

float curSpeed(TripleF data, float duration);

float  force(TripleF data);

unsigned long  flightDuration(unsigned long time);

  std::deque<Sample> getEvent() {
        hasEventReady = false;  // reset flag
        return event;           // return reference to current event
    }



      bool backgroundReady() const { return normalCounter == 0; }

  bool eventReady()
  {
    return hasEventReady;
  }

void processSample(Sample& s); 

void loop();

};


#endif