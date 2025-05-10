#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

Adafruit_PWMServoDriver myServo;

#define SERVO_MIN_PULSE 150
#define SERVO_MAX_PULSE 600
static const uint16_t SERVO_CENTER = (SERVO_MIN_PULSE + SERVO_MAX_PULSE) / 2;

// Calibration: neutral positions for the two eyebrows
static const uint16_t EYEBROW_BASE_LEFT  = SERVO_CENTER + 10;
static const uint16_t EYEBROW_BASE_RIGHT = SERVO_CENTER - 30;

// Eyebrow expression offsets (in PWM units)
static const int16_t EYEBROW_OFFSET_NEUTRAL   =   0;
static const int16_t EYEBROW_OFFSET_SLIGHT_UP = +20;
static const int16_t EYEBROW_OFFSET_REAL_UP   = +35;
static const int16_t EYEBROW_OFFSET_DOWN      = -30;

// Named indices for each servo channel on the PCA9685
enum ServoChannel {
  CHANNEL_LEFT_EYEBROW = 0,
  CHANNEL_RIGHT_EYEBROW,
  CHANNEL_EYE_MOVEMENT,
  CHANNEL_MOUTH_LEFT,
  CHANNEL_MOUTH_RIGHT,
  CHANNEL_ANTENNA,
  CHANNEL_ARM
};

// Mouth animation step size
static const uint8_t  MOUTH_MOVE_STEP = 10;

// Antenna motion parameters
static const uint16_t ANTENNA_NEUTRAL_OFFSET   = 100;
static const uint16_t ANTENNA_NEUTRAL_POSITION = SERVO_CENTER - ANTENNA_NEUTRAL_OFFSET;
static const uint16_t ANTENNA_SWEEP_AMPLITUDE  = 37;

// Timing and state for antenna
typedef unsigned long ul;
static ul  lastAntennaToggleTime = 0;
static ul  antennaToggleInterval = 300;
static bool antennaToggleState    = false;
static bool antennaErraticMode    = false;

// Timing and state for arm wave
static bool armWaveEnabled          = false;
static ul   armWaveStartTime        = 0;
static const ul ARM_WAVE_DURATION   = 5000;
static const ul ARM_TOGGLE_INTERVAL = 500;
static ul   lastArmToggleTime       = 0;
static bool armToggleState          = false;

// Mouth servo positions (current vs. target)
int currentMouthLeftPWM, currentMouthRightPWM;
int targetMouthLeftPWM,  targetMouthRightPWM;

// Last applied expression & serial buffer
static String lastExpression    = "";
static String serialInputBuffer = "";

/**
 * @brief Write a new PWM value only if it’s changed, to reduce I2C traffic.
 * @param channel PCA9685 channel index.
 * @param pwmValue Desired PWM duty cycle (between SERVO_MIN_PULSE and SERVO_MAX_PULSE).
 */
void writeServoIfChanged(uint8_t channel, uint16_t pwmValue) {
  static uint16_t lastPWM[16] = {0};
  if (lastPWM[channel] != pwmValue) {
    myServo.setPWM(channel, 0, pwmValue);
    lastPWM[channel] = pwmValue;
  }
}

/**
 * @brief Apply a uniform offset to both eyebrows.
 *        Right brow moves with the offset; left brow inverts it.
 * @param offset Positive to raise, negative to lower.
 */
void updateEyebrowOffset(int16_t offset) {
  uint16_t rightPos = constrain(int(EYEBROW_BASE_RIGHT) + offset, SERVO_MIN_PULSE, SERVO_MAX_PULSE);
  uint16_t leftPos  = constrain(int(EYEBROW_BASE_LEFT)  - offset, SERVO_MIN_PULSE, SERVO_MAX_PULSE);
  writeServoIfChanged(CHANNEL_LEFT_EYEBROW,  leftPos);
  writeServoIfChanged(CHANNEL_RIGHT_EYEBROW, rightPos);
}

void setup() {
  Serial.begin(9600);
  myServo.begin();
  myServo.setPWMFreq(60);
  delay(10);

  // Initialize mouth to center
  currentMouthLeftPWM   = targetMouthLeftPWM   = SERVO_CENTER;
  currentMouthRightPWM  = targetMouthRightPWM  = SERVO_CENTER;

  // Neutral expression
  updateEyebrowOffset(EYEBROW_OFFSET_NEUTRAL);
  writeServoIfChanged(CHANNEL_MOUTH_LEFT, 2 * SERVO_CENTER - currentMouthLeftPWM);
  writeServoIfChanged(CHANNEL_MOUTH_RIGHT, currentMouthRightPWM);
  writeServoIfChanged(CHANNEL_EYE_MOVEMENT, SERVO_CENTER);
  writeServoIfChanged(CHANNEL_ANTENNA, ANTENNA_NEUTRAL_POSITION);
  writeServoIfChanged(CHANNEL_ARM, SERVO_CENTER);
}

void loop() {
  // Buffer up serial until newline, then parse
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      parseIncomingSerial(serialInputBuffer);
      serialInputBuffer = "";
    } else {
      serialInputBuffer += c;
      if (serialInputBuffer.length() > 200)
        serialInputBuffer = serialInputBuffer.substring(serialInputBuffer.length() - 200);
    }
  }

  // Update all dynamic motions
  updateAntennaSwing();
  updateMouthAnimation();
  updateArmWavingMotion();
}

/**
 * @brief Handle a complete line from serial: expressions or user enter/leave.
 * @param line Trimmed serial line, e.g. "Anger,Left 50px" or "UserEntered".
 */
void parseIncomingSerial(const String &line) {
  String msg = line; msg.trim();

  if (msg == "UserEntered") {
    armWaveEnabled    = true;
    armWaveStartTime  = millis();
    lastArmToggleTime = millis();
    armToggleState    = false;
    return;
  }
  if (msg == "UserLeft") {
    armWaveEnabled = false;
    writeServoIfChanged(CHANNEL_ARM, SERVO_CENTER);
    return;
  }

  int commaIndex = msg.indexOf(',');
  String expression = (commaIndex > 0 ? msg.substring(0, commaIndex) : msg);
  String eyeData    = (commaIndex > 0 ? msg.substring(commaIndex + 1) : "");

  if (expression != lastExpression) {
    applyExpressionSettings(expression);
    lastExpression = expression;
  }
  updateEyeServoPosition(eyeData);
}

/**
 * @brief Set target positions and timings based on the named emotion.
 * @param expression One of "Sadness", "Anger", "Neutral", "Super_Happy", "Semi_Happy", "Curious", "Fear".
 */
void applyExpressionSettings(const String &expression) {
  antennaErraticMode = false;

  if (expression == "Sadness") {
    updateEyebrowOffset(EYEBROW_OFFSET_DOWN);
    targetMouthLeftPWM  = targetMouthRightPWM = SERVO_CENTER - 80;
    antennaToggleInterval = 600;
  }
  else if (expression == "Anger") {
    updateEyebrowOffset(EYEBROW_OFFSET_DOWN);
    targetMouthLeftPWM  = targetMouthRightPWM = SERVO_CENTER - 80;
    antennaToggleInterval = 100;
  }
  else if (expression == "Neutral") {
    updateEyebrowOffset(EYEBROW_OFFSET_NEUTRAL);
    targetMouthLeftPWM  = targetMouthRightPWM = SERVO_CENTER;
    antennaToggleInterval = 300;
  }
  else if (expression == "Super_Happy") {
    updateEyebrowOffset(EYEBROW_OFFSET_REAL_UP);
    targetMouthLeftPWM  = targetMouthRightPWM = SERVO_CENTER + 200;
    antennaToggleInterval = 50;
  }
  else if (expression == "Semi_Happy") {
    updateEyebrowOffset(EYEBROW_OFFSET_SLIGHT_UP);
    targetMouthLeftPWM  = targetMouthRightPWM = SERVO_CENTER + 100;
    antennaToggleInterval = 150;
  }
  else if (expression == "Curious") {
    // Curious: only the right brow goes fully up
    uint16_t leftPos  = constrain(int(EYEBROW_BASE_LEFT)  + EYEBROW_OFFSET_NEUTRAL, SERVO_MIN_PULSE, SERVO_MAX_PULSE);
    uint16_t rightPos = constrain(int(EYEBROW_BASE_RIGHT) + EYEBROW_OFFSET_REAL_UP,   SERVO_MIN_PULSE, SERVO_MAX_PULSE);
    writeServoIfChanged(CHANNEL_LEFT_EYEBROW,  leftPos);
    writeServoIfChanged(CHANNEL_RIGHT_EYEBROW, rightPos);

    targetMouthLeftPWM  = SERVO_CENTER - 40;
    targetMouthRightPWM = SERVO_CENTER;
    antennaToggleInterval = 300;
  }
  else if (expression == "Fear") {
    updateEyebrowOffset(EYEBROW_OFFSET_SLIGHT_UP);
    targetMouthLeftPWM  = targetMouthRightPWM = SERVO_CENTER;
    antennaErraticMode  = true;
  }
}

/**
 * @brief Smoothly interpolate mouth servos toward their targets.
 */
void updateMouthAnimation() {
  // Left mouth
  if (currentMouthLeftPWM <  targetMouthLeftPWM)
    currentMouthLeftPWM = min(currentMouthLeftPWM + MOUTH_MOVE_STEP, targetMouthLeftPWM);
  else if (currentMouthLeftPWM > targetMouthLeftPWM)
    currentMouthLeftPWM = max(currentMouthLeftPWM - MOUTH_MOVE_STEP, targetMouthLeftPWM);
  writeServoIfChanged(
    CHANNEL_MOUTH_LEFT,
    constrain(2 * SERVO_CENTER - currentMouthLeftPWM, SERVO_MIN_PULSE, SERVO_MAX_PULSE)
  );

  // Right mouth
  if (currentMouthRightPWM <  targetMouthRightPWM)
    currentMouthRightPWM = min(currentMouthRightPWM + MOUTH_MOVE_STEP, targetMouthRightPWM);
  else if (currentMouthRightPWM > targetMouthRightPWM)
    currentMouthRightPWM = max(currentMouthRightPWM - MOUTH_MOVE_STEP, targetMouthRightPWM);
  writeServoIfChanged(CHANNEL_MOUTH_RIGHT,
                     constrain(currentMouthRightPWM, SERVO_MIN_PULSE, SERVO_MAX_PULSE)
  );
}

/**
 * @brief Swing the antenna back and forth. In "erratic" mode, it alternates rapidly.
 */
void updateAntennaSwing() {
  ul now = millis();
  ul interval = antennaErraticMode ? (antennaToggleState ? 50 : 300)
                                   : antennaToggleInterval;

  if (now - lastAntennaToggleTime >= interval) {
    uint16_t pos = antennaToggleState
                   ? (ANTENNA_NEUTRAL_POSITION + ANTENNA_SWEEP_AMPLITUDE)
                   : (ANTENNA_NEUTRAL_POSITION - ANTENNA_SWEEP_AMPLITUDE);
    writeServoIfChanged(CHANNEL_ANTENNA, pos);
    antennaToggleState      = !antennaToggleState;
    lastAntennaToggleTime   = now;
  }
}

/**
 * @brief Perform a brief waving motion on the arm when a user enters.
 */
void updateArmWavingMotion() {
  if (!armWaveEnabled) return;

  ul now = millis();
  if (now - armWaveStartTime > ARM_WAVE_DURATION) {
    armWaveEnabled = false;
    writeServoIfChanged(CHANNEL_ARM, SERVO_CENTER);
    return;
  }

  if (now - lastArmToggleTime >= ARM_TOGGLE_INTERVAL) {
    uint16_t leftPos = SERVO_CENTER - 112;
    uint16_t pos     = armToggleState ? leftPos : SERVO_MIN_PULSE;
    writeServoIfChanged(CHANNEL_ARM, pos);
    armToggleState      = !armToggleState;
    lastArmToggleTime   = now;
  }
}

/**
 * @brief Convert a string like "Left 50px" into a servo movement on the eye channel.
 * @param eyeData e.g. "Left 50px" or "Right 30px".
 */
void updateEyeServoPosition(const String &eyeData) {
  String s = eyeData; s.trim();
  int pwmTarget = SERVO_CENTER;

  if (s.startsWith("Left") || s.startsWith("Right")) {
    int idx = s.indexOf(' ');
    if (idx > 0) {
      String numStr = s.substring(idx + 1);
      numStr.replace("px", "");
      int px = constrain(numStr.toInt(), 0, 200);
      int mapped = map(px, 0, 200, 0, 150);
      pwmTarget += s.startsWith("Left") ? mapped : -mapped;
    }
  }

  writeServoIfChanged(
    CHANNEL_EYE_MOVEMENT,
    constrain(pwmTarget, SERVO_MIN_PULSE, SERVO_MAX_PULSE)
  );
}
