
#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <SD.h>
#include <SPI.h>
#include <avr/pgmspace.h>

// 핀 정의
#define BUTTON_SET 2      // D3
#define BUTTON_ADJUST 3   // D4
//#define SD_CS 4           // SD 칩 셀렉트
#define A A0
#define B A1
#define C A2
#define D A3

const int SD_CS = 4;  // SD 카드 CS 핀
const int SD_RETRY_COUNT = 3;     // 최대 재시도 횟수
const int SD_RETRY_DELAY = 1000;  // 재시도 간격 (밀리초)

RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);
String prevLine1 = "";  // 이전 1행 내용 저장
String prevLine2 = "";  // 이전 1행 내용 저장
// 상태 변수
bool settingMode = false;
bool adjusting = false;
bool buttonPressed = false;
bool blinkState = true;
unsigned long buttonPressTime = 0;
unsigned long lastBlinkTime = 0;
unsigned long lastButtonTime = 0;
const uint16_t BLINK_INTERVAL = 500;
const uint16_t IDLE_TIMEOUT = 10000;

uint8_t settingIndex = 0;
//uint8_t fixedSecond = 0;
uint16_t eventCounter = 0;
int16_t currentEventIndex = -1;
DateTime now;
bool lastVibState[4] = {HIGH, HIGH, HIGH, HIGH};
bool sdInitialized = false;

// 문자 저장
char currentEvent[17] = "";
char previousEvent[17] = "";
char lastDisplay1[17] = "";
char lastDisplay2[17] = "";
char buf1[17] = "";
char buf2[17] = "";
// 요일 문자열 (PROGMEM)
const char day0[] PROGMEM = "Su";
const char day1[] PROGMEM = "Mo";
const char day2[] PROGMEM = "Tu";
const char day3[] PROGMEM = "We";
const char day4[] PROGMEM = "Th";
const char day5[] PROGMEM = "Fr";
const char day6[] PROGMEM = "Sa";
const char* const days[] PROGMEM = {day0, day1, day2, day3, day4, day5, day6};
bool initializeSD() {
  for (int i = 0; i < SD_RETRY_COUNT; i++) {
    if (SD.begin(SD_CS)) {
      Serial.println(F("SD.begin() succeeded"));
      return true;
    }
    Serial.print(F("SD.begin() attempt "));
    Serial.print(i + 1);
    Serial.println(F(" failed"));
    delay(SD_RETRY_DELAY);
  }
  Serial.println(F("SD.begin() failed after retries"));
  return false;
}
void setup() {
  Serial.begin(9600);

  pinMode(BUTTON_SET, INPUT_PULLUP);
  pinMode(BUTTON_ADJUST, INPUT_PULLUP);
  pinMode(A, INPUT_PULLUP);
  pinMode(B, INPUT_PULLUP);
  pinMode(C, INPUT_PULLUP);
  pinMode(D, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();

  if (!rtc.begin()) {
    lcd.clear();
    lcd.print(F("RTC Error"));
    delay(2000);
  } else {
//    rtc.adjust(DateTime(2025, 5, 24, 22, 42, 0));
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));  // RTC 초기화 필요 시
  }
  }

  SPI.begin();
SPI.setClockDivider(SPI_CLOCK_DIV16); // SPI 속도를 낮춤 (예: 1MHz)

// SD 초기화
pinMode(SD_CS, OUTPUT);
digitalWrite(SD_CS, HIGH);  // CS 비활성화 후 초기화 시도

lcd.clear();
lcd.print(F("SD Initializing..."));

delay(3000);  // SD 안정화 시간
sdInitialized = initializeSD();

if (!sdInitialized) {
  lcd.clear();
  lcd.print(F("SD Init Failed"));
  delay(5000);
  Serial.println(F("SD Card initialization failed!"));
} else {
  lcd.clear();
  lcd.print(F("SD Ready"));
  delay(1000);
}

  eventCounter = loadLastCounter();
  currentEventIndex = getTotalEvents() - 1;
  updateEventDisplay();

  lcd.clear();
  lastButtonTime = millis();
}

void loop() {
  if (!settingMode) now = rtc.now();
  else now = DateTime(now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());

  handleButtons();
  handleVibrationEvents();

  if (settingMode && !adjusting && millis() - lastBlinkTime >= BLINK_INTERVAL) {
    blinkState = !blinkState;
    lastBlinkTime = millis();
  }

  updateDisplay();
  delay(50);
}

// --- 버튼 핸들링 함수 ---
void handleButtons() {
  static unsigned long lastSetPress = 0;
  static unsigned long lastAdjustPress = 0;
  const uint16_t DEBOUNCE = 200;

  // BUTTON_SET (D3)
  if (digitalRead(BUTTON_SET) == LOW) {
    if (!buttonPressed) {
      buttonPressed = true;
      buttonPressTime = millis();
    }
    if (millis() - buttonPressTime >= 1500 && !settingMode) {
      settingMode = true;
      settingIndex = 0;
      blinkState = true;
      lastBlinkTime = millis();
      adjusting = false;
//      fixedSecond = rtc.now().second();
    }
  } else {
    if (buttonPressed) {
      if (millis() - buttonPressTime < 1500 && millis() - lastSetPress >= DEBOUNCE) {
        if (!settingMode) {
          if (getTotalEvents() > 0) {
            currentEventIndex--;
            if (currentEventIndex < 0) currentEventIndex = getTotalEvents() - 1;
            updateEventDisplay();
          }
        } else {
          settingIndex++;
          if (settingIndex > 5) {
            settingMode = false;
            rtc.adjust(DateTime(now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second()));
            currentEventIndex = getTotalEvents() - 1;
            updateEventDisplay();
          }
          adjusting = false;
        }
        lastSetPress = millis();
      }
      buttonPressed = false;
    }
  }

  // BUTTON_ADJUST (D4)
  if (digitalRead(BUTTON_ADJUST) == LOW && millis() - lastAdjustPress >= DEBOUNCE) {
    if (settingMode) {
      adjusting = true;
      adjustTime();
    } else {
      if (getTotalEvents() > 0) {
        currentEventIndex++;
        if (currentEventIndex >= getTotalEvents()) currentEventIndex = 0;
        updateEventDisplay();
      }
    }
    lastAdjustPress = millis();
  } else {
    adjusting = false;
  }
}

// --- 시간 조정 함수 ---
void adjustTime() {
  uint8_t y = now.year() % 100;
  uint8_t mo = now.month();
  uint8_t d = now.day();
  uint8_t h = now.hour();
  uint8_t m = now.minute();
  uint8_t s = now.second();

  switch (settingIndex) {
    case 0: y = (y < 35) ? y + 1 : 25; break;
    case 1: mo = (mo < 12) ? mo + 1 : 1; break;
    case 2: d = (d < 31) ? d + 1 : 1; break;
    case 3: h = (h < 23) ? h + 1 : 0; break;
    case 4: m = (m < 59) ? m + 1 : 0; break;
    case 5: s = (s < 59) ? s + 1 : 0; break;
  }
  now = DateTime(2000 + y, mo, d, h, m, s);
}

// --- 진동 이벤트 처리 ---
void handleVibrationEvents() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 1000) return;
  lastCheck = millis();

  int pins[] = {A, B, C, D};
  char ids[] = {'A', 'B', 'C', 'D'};

  for (int i = 0; i < 4; i++) {
    bool state = digitalRead(pins[i]);
    if (state != lastVibState[i]) {
      eventCounter = (eventCounter + 1) % 1000;
      char buf[17];
      sprintf(buf, "%03d %c%s%02d:%02d:%02d", eventCounter, ids[i],
              state == LOW ? "On " : "Off",
              now.hour(), now.minute(), now.second());
      strcpy(currentEvent, buf);
      logButtonEvent(buf);
      currentEventIndex = getTotalEvents() - 1;
      updateEventDisplay();
      lastVibState[i] = state;
    }
  }
}

// --- 디스플레이 업데이트 ---
void updateDisplay() {
  char timeStr[17];
  char display1[17] = "";
  char display2[17] = "";
  static uint8_t lastSettingIndex = 255;
  static bool forceUpdate = true;
  static uint8_t lastSecond = 255;

  // 영문 요일 2글자 배열
  const char* daysEn[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};

  if (settingMode) {
    sprintf(timeStr, "%02d%02d%02d%s%02d:%02d:%02d",
            now.year() % 100, now.month(), now.day(),
          daysEn[now.dayOfTheWeek()],
            now.hour(), now.minute(), now.second());
    strcpy(display1, timeStr);
    if (!adjusting && lastSettingIndex == settingIndex) {
      uint8_t cursorPos[] = {0, 2, 4, 8, 11, 14};
      uint8_t pos = cursorPos[settingIndex];
      lcd.setCursor(pos, 0);
      if (blinkState) {
        lcd.print(display1[pos] == ' ' ? '0' : display1[pos]);
        lcd.print(display1[pos + 1]);
      } else {
        lcd.print(F("  "));
      }
      return;
    }
    lastSettingIndex = settingIndex;
  } else {
    lastSettingIndex = 255;
    // 1행: 항상 시간 표시
    sprintf(timeStr, "%02d%02d%02d%s%02d:%02d:%02d",
            now.year() % 100, now.month(), now.day(),
          daysEn[now.dayOfTheWeek()],
            now.hour(), now.minute(), now.second());
    strcpy(display1, timeStr);

    // 2행: 최신 이벤트 또는 SD 탐색
    if (currentEvent[0] != '\0') {
      strncpy(display2, currentEvent, 16);
      display2[16] = '\0';
    } else {
      display2[0] = '\0'; // 이벤트 없으면 공백
    }

    if (!forceUpdate && strncmp(display1, lastDisplay1, 8) == 0 && lastSecond != now.second()) {
      lcd.setCursor(8, 0);
      lcd.print(&display1[8]);
      strcpy(lastDisplay1, display1);
      lastSecond = now.second();
      return;
    }
    lastSecond = now.second();
  }

  if (forceUpdate || strcmp(display1, lastDisplay1) != 0) {
    lcd.setCursor(0, 0);
    lcd.print(F("                "));
    lcd.setCursor(0, 0);
    lcd.print(display1);
    strcpy(lastDisplay1, display1);
  }

  if (forceUpdate || strcmp(display2, lastDisplay2) != 0) {
    lcd.setCursor(0, 1);
    lcd.print(F("                "));
    lcd.setCursor(0, 1);
    lcd.print(display2);
    strcpy(lastDisplay2, display2);
    if (display2[0] != '\0') {
      Serial.println(display2);
    }
  }

  forceUpdate = false;
}

// --- SD에 이벤트 기록 ---
void logButtonEvent(const char* str) {
  if (!sdInitialized) return;
  File f = SD.open("logger1.txt", FILE_WRITE);
  if (f) {
    f.println(str);
    f.close();
  }
}

// --- 이벤트 수 로딩 ---
int16_t getTotalEvents() {
  if (!sdInitialized) return 0;
  File f = SD.open("logger1.txt");
  int16_t lines = 0;
  while (f.available()) {
    if (f.read() == '\n') lines++;
  }
  f.close();
  return lines;
}

// --- 마지막 카운터 로딩 ---
uint16_t loadLastCounter() {
  if (!sdInitialized) return 0;
  File f = SD.open("logger1.txt");
  if (!f) return 0;

  String lastLine;
  while (f.available()) {
    lastLine = f.readStringUntil('\n');
  }
  f.close();
  return lastLine.substring(0, 3).toInt();
}

// --- 이벤트 표시 ---
void updateEventDisplay() {
  if (!sdInitialized || currentEventIndex < 0) return;
  File f = SD.open("logger1.txt");
  int i = 0;
  String line;
  while (f.available()) {
    line = f.readStringUntil('\n');
    if (i++ == currentEventIndex) break;
  }
  f.close();
  line.toCharArray(currentEvent, 17);
}
