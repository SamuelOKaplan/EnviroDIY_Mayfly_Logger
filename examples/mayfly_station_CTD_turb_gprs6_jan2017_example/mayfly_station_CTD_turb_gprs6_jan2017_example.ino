/**************************************************************************
  mayfly_station_CTD_turb_gprs6_jan2017_example.ino

  Written By:  Shannon Hicks <shicks@stroudcenter.org>
  Documentation By: Anthony Aufdenkampe <aaufdenkampe@limno.com>
  Creation Date: 2016
  Development Environment: Arduino IDE 1.6.x
  Hardware Platform: Stroud Center, EnviroDIY Mayfly Arduino Datalogger
  Radio Module:  GPRSbee rev.6 cell wireless
  Sensors:
    Decagon Devices CTD-10 sensor
    Campbell Scientific OBS-3+ sensor

  This sketch is an example sketch for a solar-powered cell-radio wireless
  stream sensing station based on those recently deployed at Delaware’s First
  State National Historic Park (FSNHP), as described in our
  [EnviroDIY Mayfly logger stations deployed in PA, DE and MN!]
  (http://envirodiy.org/envirodiy-mayfly-logger-stations-deployed-in-pa-de-and-mn/)
  blog post. For this deployment, the data are posted to a data system at
  http://swrcsensors.dreamhosters.com, which can only be accessed by Stroud
  Center staff.

**WARNING:** This sketch will not work in it's entirity as it is currently
  written, because the data are posted to a dummy RESTful endpoint URL
  (http://somewebsite.com/somescript.php?) using the syntax
  that is expected at by our system at http://swrcsensors.dreamhosters.com.
  However, this sketch could be modified to post sensor measurements from
  any sensor to any receiving data system that has been configured to accept them.

**************************************************************************/

#include <Wire.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <SPI.h>
#include <SD.h>
#include <SDI12_Mod.h>
#include <GPRSbee.h>
#include <Adafruit_ADS1015.h>

//SODAQ  libraries
#include <RTCTimer.h>
#include <Sodaq_DS3231.h>
#include <Sodaq_PcInt_Mod.h>

String targetURL;
#define APN "apn.konekt.io"

#define READ_DELAY 1

//RTC Timer
RTCTimer timer;

String dataRec = "";
int currentminute;
long currentepochtime = 0;
float boardtemp = 0.0;
int testtimer = 0;
int testminute = 2;

int batteryPin = A6;    // select the input pin for the potentiometer
int batterysenseValue = 0;  // variable to store the value coming from the sensor
float batteryvoltage;
Adafruit_ADS1115 ads;     /* Use this for the 16-bit version */

char CTDaddress = '1';      //for one sensor on channel '1'
float CTDtempC, CTDdepthmm, CTDcond;
float lowturbidity, highturbidity;   //variables to hold the calculated NTU values

#define DATAPIN 7         // change to the proper pin for sdi-12 data pin, pin 7 on shield 3.0
int SwitchedPower = 22;    // sensor power is pin 22 on Mayfly
SDI12 mySDI12(DATAPIN);

#define BEE_DTR_PIN  23  //DTR
#define BEE_CTS_PIN     19   //CTS

// RTC Interrupt pin
#define RTC_PIN A7
#define RTC_INT_PERIOD EveryMinute

#define SD_SS_PIN 12

// The data log file
#define FILE_NAME "SL0xxLog.txt"

// Data header
#define LOGGERNAME "SL0xx - Mayfly CTD & Turbidity Logger"
#define DATA_HEADER "DateTime_EST,TZ-Offset,Loggertime,BoardTemp,Battery_V,CTD_Depth_mm,CTD_temp_DegC,CTD_cond_dS/m,Turb_low_NTU,Turb_high_NTU"


/*
   setup() - this function runs once when you turn your Mayfly on
*/
void setup()
{
  // Start the primary serial connection
  Serial.begin(57600);
  // Start the serial connection with the *bee
  Serial1.begin(57600);

  // Start the Real Time Clock
  rtc.begin();
  delay(100);

  // Start the SDI-12 Library
  mySDI12.begin();
  delay(100);

  // Set up pins for the LED's (Mayfly pins 8 & 9)
  pinMode(8, OUTPUT);
  pinMode(9, OUTPUT);

  // Set up pins for the switched power to the sensors (Mayfly pin 22, defined above)
  pinMode(SwitchedPower, OUTPUT);
  digitalWrite(SwitchedPower, LOW);

  // Set up pins for the *bee
  pinMode(23, OUTPUT);    // Bee socket DTR pin
  digitalWrite(23, LOW);   // on GPRSbee v6, setting this high turns on the GPRSbee.  leave it high to keep GPRSbee on.
  gprsbee.init(Serial1, BEE_CTS_PIN, BEE_DTR_PIN);
  // Comment out the next line when used with GPRSbee Rev.4
  gprsbee.setPowerSwitchedOnOff(true);

  // Start the adafruit ADS1015
  ads.begin();

  // Blink the LEDs to show the board is on and starting up
  greenred4flash();

  // Set up the log file
  setupLogFile();

  // Setup timer events
  setupTimer();

  // Setup sleep mode
  setupSleep();

  // Print a start-up note to the first serial port
  Serial.println("Power On, running: SL081_mayfly_CTD_turb_gprs_1.ino");
  //showTime(getNow());

}


/*
   void() - this function runs over and over as long as your Mayfly is on.
*/
void loop()
{
  // Update the timer
  timer.update();

  // Check of the current time is an even interval of the test minute
  if (currentminute % testminute == 0)
  { //Serial.println("Multiple of x!!!   Initiating sensor reading and logging data to SDcard....");

    digitalWrite(8, HIGH);
    dataRec = createDataRecord();

    // Turn on power to the sensors
    digitalWrite(SwitchedPower, HIGH);
    delay(1000);

    // Take a CTD Measurement via SDI-12
    // This also appends the value to dataRec
    CTDMeasurement(CTDaddress);

    // Take an analog Turbidity Measurement
    // This also appends the value to dataRec
    analogturbidity();

    // Cut power to the sensors
    digitalWrite(SwitchedPower, LOW);

    //Save the data record to the log file
    logData(dataRec);

    //Echo the data to the serial connection
    Serial.println();
    Serial.print("Data Record: ");
    Serial.println(dataRec);

    assembleURL();

    delay(100);
    digitalWrite(23, HIGH);
    delay(1000);

    sendviaGPRS();

    delay(1000);
    digitalWrite(23, LOW);
    delay(500);

    String dataRec = "";

    digitalWrite(8, LOW);
    delay(100);
    testtimer++;
  }

  // Check if the time is an interval of 5 minutes
  if (testtimer >= 5)
  { testminute = 5;
  }

  delay(200);
  //Sleep
  systemSleep();
}


// Helper function to retrieve and display the current time
void showTime(uint32_t ts)
{
  // Retrieve and display the current date/time
  String dateTime = getDateTime();
  //Serial.println(dateTime);
}


// Set-up the RTC Timer events
void setupTimer()
{
  // Schedule the wakeup every minute
  timer.every(READ_DELAY, showTime);

  // Instruct the RTCTimer how to get the current time reading
  timer.setNowCallback(getNow);
}


void wakeISR()
{
  //Leave this blank
}


// Sets up the sleep mode (used on device wake-up)
void setupSleep()
{
  pinMode(RTC_PIN, INPUT_PULLUP);
  PcInt::attachInterrupt(RTC_PIN, wakeISR);

  //Setup the RTC in interrupt mode
  rtc.enableInterrupts(RTC_INT_PERIOD);

  //Set the sleep mode
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
}


// Puts the system to sleep to conserve battery life.
void systemSleep()
{
  // This method handles any sensor specific sleep setup
  sensorsSleep();

  // Wait until the serial ports have finished transmitting
  Serial.flush();
  Serial1.flush();

  // The next timed interrupt will not be sent until this is cleared
  rtc.clearINTStatus();

  // Disable ADC
  ADCSRA &= ~_BV(ADEN);

  // Sleep time
  noInterrupts();
  sleep_enable();
  interrupts();
  sleep_cpu();
  sleep_disable();

  // Enbale ADC
  ADCSRA |= _BV(ADEN);

  // This method handles any sensor specific wake setup
  sensorsWake();
}



void sensorsSleep()
{
  // Add any code which your sensors require before sleep
}

void sensorsWake()
{
  // Add any code which your sensors require after waking
}


// Helper function to get the current date/time from the RTC
// and convert it to a human-readable string.
String getDateTime()
{
  String dateTimeStr;

  // Create a DateTime object from the current time
  DateTime dt(rtc.makeDateTime(rtc.now().getEpoch()));

  currentepochtime = (dt.get());    //Unix time in seconds

  currentminute = (dt.minute());
  // Convert it to a String
  dt.addToString(dateTimeStr);
  return dateTimeStr;
}


// Helper function to get the current date/time from the RTC
// as a unix timestamp
uint32_t getNow()
{
  currentepochtime = rtc.now().getEpoch();
  return currentepochtime;
}


// Flashess to Mayfly's LED's
void greenred4flash()
{
  for (int i = 1; i <= 4; i++) {
    digitalWrite(8, HIGH);
    digitalWrite(9, LOW);
    delay(50);
    digitalWrite(8, LOW);
    digitalWrite(9, HIGH);
    delay(50);
  }
  digitalWrite(9, LOW);
}


// Initializes the SDcard and prints a header to it
void setupLogFile()
{
  // Initialise the SD card
  if (!SD.begin(SD_SS_PIN))
  {
    Serial.println("Error: SD card failed to initialise or is missing.");
    //Hang
    //  while (true);
  }

  // Check if the file already exists
  bool oldFile = SD.exists(FILE_NAME);

  // Open the file in write mode
  File logFile = SD.open(FILE_NAME, FILE_WRITE);

  // Add header information if the file did not already exist
  if (!oldFile)
  {
    logFile.println(LOGGERNAME);
    logFile.println(DATA_HEADER);
  }

  //Close the file to save it
  logFile.close();
}


// Writes a string to a text file on the SDCar
void logData(String rec)
{
  // Re-open the file
  File logFile = SD.open(FILE_NAME, FILE_WRITE);

  // Write the CSV data
  logFile.println(rec);

  // Close the file to save it
  logFile.close();
}


// Helper function
static void addFloatToString(String & str, float val, char width, unsigned char precision)
{
  char buffer[10];
  dtostrf(val, width, precision, buffer);
  str += buffer;
}


// Create a String type data record in csv format
// TimeDate, Loggertime, Temp_DS, Diff1, Diff2, boardtemp
String createDataRecord()
{
  String data = getDateTime();
  data += ",-5,";   //adds UTC-timezone offset (5 hours is the offset between UTC and EST)
  // TODO:  Better timezone handling

  // Convert current temperature into registers
  rtc.convertTemperature();
  // Read temperature sensor value
  boardtemp = rtc.getTemperature();

  batterysenseValue = analogRead(batteryPin);
  batteryvoltage = (3.3 / 1023.) * 1.47 * batterysenseValue;
  // TODO: Figure out where these values come from

  data += currentepochtime;
  data += ",";

  addFloatToString(data, boardtemp, 3, 1);    //float
  data += ",";
  addFloatToString(data, batteryvoltage, 4, 2);


  //  Serial.print("Data Record: ");
  //  Serial.println(data);
  return data;
}


// Uses SDI-12 to communicate with a Decagon Devices CTD
// TODO:  Make this more flexible to allow user to adjust number of readings to average.
void CTDMeasurement(char i) {   //averages 6 readings in this one loop
  CTDdepthmm = 0;
  CTDtempC = 0;
  CTDcond = 0;

  for (int j = 0; j < 6; j++) {

    String command = "";
    command += i;
    command += "M!"; // SDI-12 measurement command format  [address]['M'][!]
    mySDI12.sendCommand(command);
    delay(500); // wait a while
    mySDI12.flush(); // we don't care about what it sends back

    command = "";
    command += i;
    command += "D0!"; // SDI-12 command to get data [address][D][dataOption][!]
    mySDI12.sendCommand(command);
    delay(500);
    if (mySDI12.available() > 0) {
      float junk = mySDI12.parseFloat();
      int x = mySDI12.parseInt();
      float y = mySDI12.parseFloat();
      int z = mySDI12.parseInt();

      CTDdepthmm += x;
      CTDtempC += y;
      CTDcond += z;
    }

    mySDI12.flush();
  }     // end of averaging loop

  CTDdepthmm /= 6.0 ;
  CTDtempC /= 6.0;
  CTDcond /= 6.0;


  dataRec += ",";
  addFloatToString(dataRec, CTDdepthmm, 3, 1);
  dataRec += ",";
  addFloatToString(dataRec, CTDtempC, 3, 1);
  dataRec += ",";
  addFloatToString(dataRec, CTDcond, 3, 1);
  //dataRec += ",";

}  //end of CTDMeasurement


// function that takes reading from analog OBS3+ turbidity sensor
void analogturbidity()
{
  int16_t adc0, adc1; //  adc2, adc3;      //tells which channels are to be read

  adc0 = ads.readADC_SingleEnded(0);
  adc1 = ads.readADC_SingleEnded(1);

  //now convert bits into millivolts
  float lowvoltage = (adc0 * 3.3) / 17585.0;
  float highvoltage = (adc1 * 3.3) / 17585.0;

  // calibration information below if only for instrument SN# S9743
  // TODO:  set this up so calibration can be input at top for each instrument
  lowturbidity =  (4.6641 * square (lowvoltage)) + (92.512 * lowvoltage) - 0.38548;
  highturbidity = (53.845 * square (highvoltage)) + (383.18 * highvoltage) - 1.3555;

  dataRec += ",";
  addFloatToString(dataRec, lowturbidity, 3, 1);
  dataRec += ",";
  addFloatToString(dataRec, highturbidity, 3, 1);

}


// EXAMPLE of how to Assemble RESTful post request.
// MODIFY for your receiving data system.
void assembleURL()
{
  targetURL = "";
  targetURL = "http://somewebsite.com/somescript.php?";  // UPDATE with your recieving data site
  targetURL += "LoggerID=SL0xx&Loggertime=";
  targetURL += currentepochtime;
  targetURL += "&CTDdepth=";
  addFloatToString(targetURL, CTDdepthmm, 3, 1);    //float
  targetURL += "&CTDtemp=";
  addFloatToString(targetURL, CTDtempC, 3, 1);    //float
  targetURL += "&CTDcond=";
  addFloatToString(targetURL, CTDcond, 3, 1);    //float
  targetURL += "&TurbLow=";
  addFloatToString(targetURL, lowturbidity, 3, 1);    //float
  targetURL += "&TurbHigh=";
  addFloatToString(targetURL, highturbidity, 3, 1);   //float
  targetURL += "&BoardTemp=";
  addFloatToString(targetURL, boardtemp, 3, 1);     //float
  targetURL += "&Battery=";
  addFloatToString(targetURL, batteryvoltage, 4, 2);     //float

}


// Helper function to send data via GPRS.
void sendviaGPRS()
{
  char buffer[10];
  if (gprsbee.doHTTPGET(APN, targetURL.c_str(), buffer, sizeof(buffer))) {
  }
}