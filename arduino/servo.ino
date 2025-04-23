#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <String.h>

Adafruit_PWMServoDriver myServo = Adafruit_PWMServoDriver();

#define SERVOMIN 150
#define SERVOMAX 600

#define LEFT_EYEBROW 0
#define RIGHT_EYEBROW 1

uint8_t servonum = 0;
uint8_t numberOfServos = 6;
String inputString = "";
boolean stringComplete = false;

void moveEyebrow(uint8_t servo, uint16_t position) {
  position = constrain(position, SERVOMIN, SERVOMAX);
  myServo.setPWM(servo, 0, position);
}

void displayEmotion(String emotion) {
  // Trim any whitespace or newline characters
  emotion.trim();

  if (emotion == "Happiness") {
    moveEyebrow(LEFT_EYEBROW, 375);
    moveEyebrow(RIGHT_EYEBROW, 375);
    Serial.println("Emotion: Happiness");
  } else if (emotion == "Sadness") {
    moveEyebrow(LEFT_EYEBROW, 320);
    moveEyebrow(RIGHT_EYEBROW, 320);
    Serial.println("Emotion: Sadness");
  } else if (emotion == "Anger") {
    moveEyebrow(LEFT_EYEBROW, 450);
    moveEyebrow(RIGHT_EYEBROW, 450);
    Serial.println("Emotion: Anger");
  } else if (emotion == "Surprise") {
    moveEyebrow(LEFT_EYEBROW, 250);
    moveEyebrow(RIGHT_EYEBROW, 250);
    Serial.println("Emotion: Surprise");
  } else if (emotion == "Fear") {
    moveEyebrow(LEFT_EYEBROW, 260);
    moveEyebrow(RIGHT_EYEBROW, 280);
    Serial.println("Emotion: Fear");
  } else if (emotion == "Neutral") {
    moveEyebrow(LEFT_EYEBROW, 350);
    moveEyebrow(RIGHT_EYEBROW, 350);
    Serial.println("Emotion: Neutral");
  }
}

void setup() {
  Serial.begin(9600);

  // Print a message to identify this Arduino
  Serial.println("Emotion-detecting eyebrow controller ready");

  myServo.begin();
  myServo.setPWMFreq(60);
  delay(10);
  inputString.reserve(50);  // Reserve 50 bytes for the input string
}

void loop() {
  // Check if there's a complete string
  if (stringComplete) {
    moveEyebrow(LEFT_EYEBROW, 150);
    moveEyebrow(RIGHT_EYEBROW, 150);
    displayEmotion(inputString);
    inputString = "";
    stringComplete = false;
  }

  // Read serial data
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    inputString += inChar;

    // Check for newline character
    if (inChar == '\n') {
      stringComplete = true;
    }
  }
}