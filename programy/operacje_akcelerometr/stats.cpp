#include "stats.h"





TripleF stats::mulvs( TripleF data, float scalar)
{
  TripleF result;
    result.x = data.x * scalar;
    result.y = data.y * scalar;
    result.z = data.z * scalar;

    return result;
}

float stats::vectorLenght(TripleF r)
{
    return sqrt(r.x*r.x + r.y*r.y + r.z*r.z );
}


float stats::magnitude(TripleF data, float duration)
{
    return vectorLenght(data);
}

float stats::curSpeed(TripleF data, float duration)
{
    return vectorLenght(mulvs(data, duration));
}

//how much force was used to kick the ball
float  stats::force(TripleF data)
{
    return vectorLenght(mulvs(data, ballMass));
}



//checkes if this sample is a hit     
//  wykrywamy poczatek uderzenia
//i zapisujemy okreslana liczbe probek przed i po 
bool stats::isHit(TripleF accW)
{
        // remove gravity
    accW.z -= 1.0f;

    // compute overall motion
    float motion = sqrt(accW.x*accW.x + accW.y*accW.y + accW.z*accW.z);

 
        if(motion >HIT_START_THRESHOLD)
        {
          
            return 1;
        }


}


void stats::startHitEvent() {

    event.clear();
    hitActive = true;
    postRemaining = POST_SAMPLES;
    hitStartTime = millis(); // or micros()

    // Copy PRE_SAMPLES from history
    for (int i = PRE_SAMPLES; i > 0; i--) {
        event.push_back(ring->getFromEnd(i));
    }

    // Debug/log
    Serial.println("Impact detected → Starting event capture");
}

void stats::endHitEvent() {
    hitActive = false;

        // Compute duration
        unsigned long hitEndTime = millis();
        hitDuration = hitEndTime - hitStartTime;
        
       

        
     //zapisanie dokladnych danych + czasu
    // Now currentEvent contains: [pre-hit ... hit ... post-hit]

    //teraz tego nie wykorzystujemy
    //saver->saveEvent(event, hitDuration);      // file, SD card, wifi, flash, etc.
    event.clear();         // reset for next hit

    Serial.println("Event completed and saved");
}

/*
void stats::processSample(Sample& s) {



    float mag = sqrt(s.first.x*s.first.x + s.first.y*s.first.y + s.first.z*s.first.z);
    float dynAcc = fabs(mag - 1.0f); // gravity removed

    if (!hitActive && dynAcc > HIT_START_THRESHOLD) {
        startHitEvent();
        event.push_back(s);
    }
    else if (hitActive) {
        event.push_back(s);

        // Check dynamic end condition
        if (dynAcc < HIT_END_THRESHOLD && postRemaining <= 0) {
            endHitEvent();   // cleanup and save
        } else {
            postRemaining--; // optional minimum post-hit samples
        }
    }
    else {
        // Normal background data
        normalCounter++;
        if (normalCounter >= NORMAL_SAMPLE_SKIP) {
            normalCounter = 0;
            saver->save(s);  // save every N-th sample
        }
    }
}*/

void stats::processSample(Sample& s) {
    float mag = sqrt(s.accel.x*s.accel.x + s.accel.y*s.accel.y + s.accel.z*s.accel.z);
    float dynAcc = fabs(mag - 1.0f);

    if (!hitActive && dynAcc > HIT_START_THRESHOLD) {
        startHitEvent();
        event.push_back(s);
    } 
    else if (hitActive) {
        event.push_back(s);

        if (dynAcc < HIT_END_THRESHOLD && postRemaining <= 0) {
            endHitEvent();   // sets hasEventReady
        } else {
            postRemaining--;
        }
    }
    else {
        // background sample
        normalCounter++;
        if (normalCounter >= NORMAL_SAMPLE_SKIP) {
            normalCounter = 0;
            // The caller can now check backgroundReady() and send sample
            
        }
    }
}

void stats::loop() {
    while (ring->available()) {
        Sample s = ring->pop();  // read next sample
        processSample(s);    // detect hits here
    }
}

