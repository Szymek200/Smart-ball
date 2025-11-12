#include "SparkFun_LIS331.h"
#include <L3G4200D.h>
#include <Wire.h>
#include <math.h>




L3G4200D gyroscope;
// Pitch, Roll and Yaw values
float pitch = 0;
float roll = 0;
float yaw = 0;
//led
boolean Blink = false;
int LED = 13;
// Timers
unsigned long timer = 0;
float timeStep = 0.01;


//general data
float ballMass = 0.250; //in kg

bool duringFLight = 0;

//accelerometr data
LIS331 xl;

#define AC_SCALE 6


// for timer - accelerometer

volatile bool hitStartFlag = false;
volatile bool hitEndFlag = false;
volatile unsigned long hitStart = 0;
volatile unsigned long hitEnd = 0;


//flight duration
volatile unsigned long flightStart = 0;
volatile unsigned long flightEnd = 0;

#define INT1_PIN 34  // your actual GPIO for INT1
#define INT2_PIN 35  // choose a free GPIO for INT2

  uint8_t thresholdHigh = 40;  // ~0.64 g
uint8_t thresholdLow = 20;   // ~0.32 g (optional, smaller than high)

int16_t data[3];

void IRAM_ATTR int1ISR();
void IRAM_ATTR int2ISR();


void setup() {
  Serial.begin(115200);


  Wire.begin(21, 22);     // specify SDA, SCL for ESP32

// Initialize L3G4200D - gyroscope
  // Set scale 2000 dps and 400HZ Output data rate (cut-off 50)
  while (!gyroscope.begin(L3G4200D_SCALE_2000DPS, L3G4200D_DATARATE_400HZ_50))
  {
    // Waiting for initialization

    if (Blink)
    {
      digitalWrite(LED, HIGH);
    } else
    {
      digitalWrite(LED, LOW);
    }

    Blink = !Blink;

    delay(500);
  }

  digitalWrite(LED, HIGH);

  // Calibrate gyroscope. The calibration must be at rest.
  // If you don't want calibrate, comment this line.
  gyroscope.calibrate(100);

  digitalWrite(LED, LOW);

//setting accelerometer

  xl.setI2CAddr(0x19);    // must come before .begin()
  xl.begin(LIS331::USE_I2C);  // selects I2C mode, no return value

  // This next section configures an interrupt. It will cause pin
  //  INT1 on the accelerometer to go high when the absolute value
  //  of the reading on the Z-axis exceeds a certain level for a
  //  certain number of samples.
  xl.intSrcConfig(LIS331::INT_SRC, 1); // Select the source of the
                          //  signal which appears on pin INT1. In
                          //  this case, we want the corresponding
                          //  interrupt's status to appear. 
  xl.setIntDuration(50, 1); // Number of samples a value must meet
                          //  the interrupt condition before an
                          //  interrupt signal is issued. At the
                          //  default rate of 50Hz, this is one sec.
  xl.setIntThreshold(50, 1);

  xl.setIntThreshold(thresholdHigh, 1);  // INT1
  xl.setIntThreshold(thresholdLow, 2);   // INT2
                          // Threshold for an interrupt. This is
                          //  not actual counts, but rather, actual
                          //  counts divided by 16.
    // High-g -> start timer
    xl.enableInterrupt(LIS331::X_AXIS, LIS331::TRIG_ON_HIGH, 1, true);
    xl.enableInterrupt(LIS331::Y_AXIS, LIS331::TRIG_ON_HIGH, 1, true);
    xl.enableInterrupt(LIS331::Z_AXIS, LIS331::TRIG_ON_HIGH, 1, true);

    // Low-g -> stop timer
    xl.enableInterrupt(LIS331::X_AXIS, LIS331::TRIG_ON_LOW, 2, true);
    xl.enableInterrupt(LIS331::Y_AXIS, LIS331::TRIG_ON_LOW, 2, true);
    xl.enableInterrupt(LIS331::Z_AXIS, LIS331::TRIG_ON_LOW, 2, true);
 
                          // Enable the interrupt. Parameters indicate
                          //  which axis to sample, when to trigger
                          //  (in this case, when the absolute mag
                          //  of the signal exceeds the threshold),
                          //  which interrupt source we're configuring,
                          //  and whether to enable (true) or disable
                          //  (false) the interrupt.

  //setting interrupts for esp32
  attachInterrupt(digitalPinToInterrupt(INT1_PIN), int1ISR, RISING);
  attachInterrupt(digitalPinToInterrupt(INT2_PIN), int2ISR, RISING);

  Serial.begin(115200);


  Serial.println("LIS331 initialized (I2C mode).");
}

//calculation methods
float * Mulvs( float data[3], float scalar)
{
  float result[3];
    result[0] = data[0] * scalar;
    result[1] = data[1] * scalar;
    result[2] = data[2] * scalar;

    return result;
}

float vectorLenght(float r[3])
{
    return sqrt(r[0]*r[0] + r[1]*r[1] + r[2]*r[2] );
}


float maxAcceleration(float data[3], float duration)
{
    return vectorLenght(data);
}

float maxSpeed(float data[3], float duration)
{
    return vectorLenght(Mulvs(data, duration));
}

//how much force was used to kick the ball
float  maxForce(float data[3])
{
    return vectorLenght(Mulvs(data, ballMass));
}

//vector times scalar
//doesn't return - array is a pointer


//gyroscope functions

float * gyroMeasures()
{
  // Read normalized values
  Vector norm = gyroscope.readNormalize();
  float gyroData[3];
  gyroData[0] = norm.XAxis;
    gyroData[1] = norm.YAxis;
      gyroData[2] = norm.ZAxis;

  // Calculate Pitch, Roll and Yaw
  pitch = pitch + norm.YAxis * timeStep;
  roll = roll + norm.XAxis * timeStep;
  yaw = yaw + norm.ZAxis * timeStep;

    return gyroData;

}

//second hit - how long flight took
void  Landing(unsigned long Start, unsigned long Stop)
{
  float duration = static_cast<float>(Stop - Start) / static_cast<float>(pow(10, 6));

  Serial.print("Czas lotu: ");
  Serial.println(duration);
}





//interrupts==========================================pts=====================pts=====================

void IRAM_ATTR int1ISR() { // High-g interrupt
    if (!hitStartFlag) {
        hitStart = micros();
        hitStartFlag = true;

         xl.readAxes(data[0], data[1], data[2]);

        if(flightStart != 0)//necessary only in first flight
        {
          flightEnd = hitStart;
          //copying data, because next kick is going to happen


          Landing(flightStart, flightEnd);
        }

    }
}

void IRAM_ATTR int2ISR() { // Low-g interrupt
    if (hitStartFlag && !hitEndFlag) {
        hitEnd = micros();
        hitEndFlag = true;
       
       flightStart = hitEnd;
        afterKick();
    }
}


//function for calculating all things which we can caluculate after kich is ended
void afterKick()
{
  //reseting flags
        hitStartFlag = false;
        hitEndFlag = false;
       unsigned long kick_time = hitEnd - hitStart;

       double kick_time_Seconds = static_cast<float>(kick_time) / 1000000;
       
       //converted to g
       float conData[3];
      
      //read comes from first interrupt
       conData[0] = xl.convertToG(AC_SCALE,data[0]);
       conData[1] = xl.convertToG(AC_SCALE,data[1]);
       conData[2] = xl.convertToG(AC_SCALE,data[2]);

      float max_speed = maxSpeed(conData, kick_time_Seconds);
      float kick_force = maxForce(conData);
      float maxx_acceleration = maxAcceleration(conData, kick_time_Seconds);

       
}


//-----------------------------

void loop() {
    // put your main code here, to run repeatedly:

    gyroMeasures();

    Serial.println("Zyroskop ");
    Serial.print("Pitch: ");
  Serial.print(pitch);
  Serial.print(" Roll: ");
  Serial.print(roll);
  Serial.print(" Yaw: ");
  Serial.println(yaw);

}

