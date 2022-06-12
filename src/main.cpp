#include <ESP32Servo.h>
#if defined(ESP32)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#endif
#include "addons/RTDBHelper.h"
#include <Firebase_ESP_Client.h>
#include "time.h"
// Provide the token generation process info.
#include <addons/TokenHelper.h>
// Provide the RTDB payload printing info and other helper functions.
#include <addons/RTDBHelper.h>
#include <FirebaseJson.h>
/* 1. Define the WiFi credentials */
#define WIFI_SSID "HarooN.."
#define WIFI_PASSWORD "haroonmectec"
/* 2. Define the API Key */
#define API_KEY "AIzaSyDNy4WIW_68BQReiSimK6mvbgh9pNKzzZA"
/* 3. Define the RTDB URL */
#define DATABASE_URL "aquarium-e91cd-default-rtdb.asia-southeast1.firebasedatabase.app/" //<databaseName>.firebaseio.com or <databaseName>.<region>.firebasedatabase.app
/* 4. Define the user Email and password that alreadey registerd or added in your project */
#define USER_EMAIL "esp32firebase@gmail.com"
#define USER_PASSWORD "password123"
const String timePath = "timestamp";
const char *ntpServer = "pool.ntp.org";
#include <OneWire.h>
#include <DallasTemperature.h>
#include <FirebaseJson.h>
// Define Firebase Data object
const int oneWireBus = 4;
// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(oneWireBus);
// Pass our oneWire reference to Dallas Temperature sensor
DallasTemperature sensors(&oneWire);
FirebaseData fbdo;

FirebaseAuth auth;
FirebaseConfig config;
static const int servoPin = 26;

Servo myservo;
unsigned long sendDataPrevMillis = 0;
unsigned long getTime();
unsigned long count = 0;
float currentTemperature;
float desiredT;
int feedInterval = 10;
int feedNow;
int heaterState;
int heaterPin = 33;
unsigned long int lastFeed;
int runCw;
int runCCw;
int stop;
int toFeedPos;
void feedNowRoutine();
void setup()
{
  pinMode(heaterPin, OUTPUT);
  digitalWrite(heaterPin, HIGH);
  Serial.begin(115200);
  sensors.begin();
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  myservo.setPeriodHertz(50);           // standard 50 hz servo
  myservo.attach(servoPin, 1000, 2000); // attaches the servo on pin 18 to the servo object
  myservo.write(180);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION);
  config.api_key = API_KEY;

  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.database_url = DATABASE_URL;

  config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Firebase.setDoubleDigits(5);
  configTime(18000, 0, ntpServer);
}
unsigned long ms = 0;

void loop()
{
  sensors.requestTemperatures();
  currentTemperature = sensors.getTempCByIndex(0);
  Serial.println(currentTemperature);
  if (currentTemperature < desiredT)
  {
    digitalWrite(heaterPin, LOW); // turn it on
  }
  if (currentTemperature > desiredT)
  {
    digitalWrite(heaterPin, HIGH);
  }
  if (millis() - ms >= 1000)
  {
    int timestamp = getTime();
    Serial.println(timestamp);
    ms = millis();
    if (Firebase.RTDB.getInt(&fbdo, "heaterState"))
    {
      String data = fbdo.to<String>();
      heaterState = data.toFloat();
      Serial.print("OK: heater state: ");
      Serial.println(heaterState);
    }
    else
    {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
    }

    if (Firebase.RTDB.getInt(&fbdo, "feedNow"))
    {
      String data = fbdo.to<String>();
      feedNow = data.toFloat();
      Serial.print("OK: Feeding now: ");
      Serial.println(feedNow);
    }
    else
    {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
    }

    if (Firebase.RTDB.getString(&fbdo, "desiredT"))
    {
      String data = fbdo.to<String>();
      desiredT = data.toFloat();
      Serial.print("OK: desired temperature: ");
      Serial.println(desiredT);
    }
    else
    {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
    }

    if (Firebase.RTDB.getString(&fbdo, "feedInterval"))
    {
      String data = fbdo.to<String>();

      feedInterval = data.toFloat();
      Serial.print("OK: feedInterval: ");
      Serial.println(feedInterval);
    }
    else
    {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
    }

    if (Firebase.RTDB.setFloat(&fbdo, "currentT", currentTemperature))
    {
      Serial.println("Uploaded current temperature to firebase. ");
    }
    else
    {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
    }
  }
  if (feedNow)
  {
    feedNowRoutine();
    feedNow = false;
    if (Firebase.RTDB.setFloat(&fbdo, "feedNow", feedNow))
    {
      Serial.println("Successfully updated feednow");
    }
    else
    {
      Serial.println("ERROR");
    }
    Serial.println("Feednow routine finished.");
    lastFeed = millis();
  }

  // if (heaterState == 1)
  // {
  //   digitalWrite(heaterPin, LOW);
  // }
  // else
  // {
  //   digitalWrite(heaterPin, HIGH);
  // }

  if ((feedInterval != 0) && (millis() - lastFeed >= feedInterval * 1000) && (feedNow == 0)) // replace by 1000 if s
  {
    lastFeed = millis();
    Serial.println("Feeding according to interval");
    feedNowRoutine();
  }
}

// Function that gets current epoch time
unsigned long getTime()
{
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    // Serial.println("Failed to obtain time");
    return (0);
  }
  time(&now);
  return now;
}
void feedNowRoutine()
{
  // run servo to feeding position and bring it back.
  Serial.println("Feeding now.... ");

  // Firebase.RTDB.pushString(&timePath, &timestamp);

  myservo.write(180);
  delay(2000);
  myservo.write(0);
  delay(2000);
  myservo.write(180);
}