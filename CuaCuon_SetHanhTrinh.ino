// esp32wroom
#include <Preferences.h>
#include <Matter.h>
#if !CONFIG_ENABLE_CHIPOBLE
#include <WiFi.h>
const char *ssid = "Mang 2.4G";         // Đổi thành SSID WiFi của bạn
const char *password = "dauloxo@"; // Đổi thành mật khẩu WiFi của bạn
#endif

#define RL1_PIN 12 // LÊN
#define RL2_PIN 13 // DỪNG
#define RL3_PIN 14 // XUỐNG
#define SW1_PIN 16 // SW lên
#define SW2_PIN 17 // SW dừng
#define SW3_PIN 5 // SW xuống
#define LED1_PIN 18 // Led lên
#define LED2_PIN 19 // Led dừng
#define LED3_PIN 21 // Led xuống
#define BT_SET_PIN 15 // Nút set hành trình
#define LED_HT_PIN 2 // Led hành trình
#define MATTER_RESET_PIN 0 // Nút reset Matter (GPIO0 - nút BOOT), nhấn 5 lần để reset

int i = 0;
bool lastSW1State = HIGH, lastSW2State = HIGH, lastSW3State = HIGH;

// Biến dùng để đếm thời gian ReLay được bật
unsigned long RelayOnTime = 0;
const unsigned long RelayTimeout = 500; // 500ms

// ==================== SET HÀNH TRÌNH ====================
Preferences prefs;

const unsigned long SET_HOLD_TIME = 5000; // giữ 5s để vào chế độ set

enum SetState {
  SET_WAIT_UP,       
  SET_UP_RUNNING,     
  SET_UP_STOPPED,     
  SET_WAIT_DOWN,      
  SET_DOWN_RUNNING,    
  SET_DOWN_STOPPED,   
  SET_READY_FINAL     
};

bool setModeActive = false;
SetState setState = SET_WAIT_UP;

unsigned long startUpTime = 0, stopUpTime = 0;
unsigned long startDownTime = 0, stopDownTime = 0;
unsigned long t_up = 0, t_down = 0;
unsigned long totalTravelTime = 0; // tổng thời gian hành trình 0% -> 100% (ms)
float msPerPercent = 0;

int currentPercent = 0; // vị trí cửa ước lượng hiện tại (0% đóng, 100% mở)

bool btSetLastState = HIGH;
unsigned long btSetPressStart = 0;
bool btSetLongTriggered = false;

// ==================== ĐIỀU KHIỂN THEO % ====================
bool percentMoveActive = false;
int percentMoveStartPercent = 0, percentMoveTargetPercent = 0;
unsigned long percentMoveStartTime = 0, percentMoveDuration = 0;
bool percentMoveNeedStop = true;
unsigned long lastPercentPrint = 0;

// CỜ CHỐNG VÒNG LẶP ĐỆ QUY MATTER
bool isUpdatingFromMatter = false;

// Khai báo prototype
void moveToPercent(int target, bool isFromMatter = false);
void reportMatterPosition(int pct);
void reportMatterTarget(int pct);
void setMatterMoving(bool movingUp);
void setMatterStalled();
void saveHanhTrinh();
void loadHanhTrinh();
void chedo_setmode();
void chedo_congtac();
void handleSerialInput();
void updatePercentMove();
void startManualMove(int dir);
void stopManualMove();
void handleSetButton();
void handleMatterResetButton();
void enterSetMode();
void handleSetClick();
void blinkLedHT(int times, int ms);

// ==================== MATTER ====================
MatterWindowCovering RollingDoor;
unsigned long lastMatterPrintTime = 0; 

// ==================== RESET MATTER ====================
bool matterResetLastState = HIGH;
int matterResetPressCount = 0;
unsigned long matterResetWindowStart = 0;
const unsigned long MATTER_RESET_WINDOW = 3000; 
const int MATTER_RESET_PRESS_NEEDED = 5;

void setup() {
  Serial.begin(115200);

  pinMode(SW1_PIN, INPUT_PULLUP);
  pinMode(SW2_PIN, INPUT_PULLUP);
  pinMode(SW3_PIN, INPUT_PULLUP);
  pinMode(RL1_PIN, OUTPUT);
  pinMode(RL2_PIN, OUTPUT);
  pinMode(RL3_PIN, OUTPUT);
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
  pinMode(BT_SET_PIN, INPUT_PULLUP);
  pinMode(LED_HT_PIN, OUTPUT);
  pinMode(MATTER_RESET_PIN, INPUT_PULLUP);

  loadHanhTrinh();

#if !CONFIG_ENABLE_CHIPOBLE
  Serial.print(F("Dang ket noi WiFi: "));
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print(F("WiFi da ket noi, IP: "));
  Serial.println(WiFi.localIP());
#endif

  uint8_t initialMatterLift = 100 - currentPercent;
  RollingDoor.begin(initialMatterLift, 0, MatterWindowCovering::ROLLERSHADE);

  RollingDoor.onOpen(matterOpen);
  RollingDoor.onClose(matterClose);
  RollingDoor.onStop(matterStop);
  RollingDoor.onGoToLiftPercentage(matterGoToLiftPercentage);

  Matter.begin();

  if (Matter.isDeviceCommissioned()) {
    Serial.println(F("Matter da duoc commissioning, san sang su dung."));
  } else {
    Serial.println(F("Matter CHUA duoc commissioning."));
    Serial.print(F("Ma pairing thu cong: "));
    Serial.println(Matter.getManualPairingCode());
  }
}

void loop() {
  if (!Matter.isDeviceCommissioned() && millis() - lastMatterPrintTime >= 10000) {
    lastMatterPrintTime = millis();
    Serial.println(F("Matter chua duoc commissioning. Ma pairing:"));
    Serial.println(Matter.getManualPairingCode());
  }

  handleSetButton(); 
  handleMatterResetButton(); 

  if (setModeActive) {
    chedo_setmode(); 
  } else {
    chedo_congtac(); 
    handleSerialInput(); 
    updatePercentMove();
  }

  if ((i == 1 || i == 2 || i == 3) && (millis() - RelayOnTime >= RelayTimeout)) {
    digitalWrite(RL1_PIN, 0);
    digitalWrite(RL3_PIN, 0);
    digitalWrite(RL2_PIN, 0);
    digitalWrite(LED1_PIN, 0);
    digitalWrite(LED2_PIN, 0);
    digitalWrite(LED3_PIN, 0);
  }
}

void van_hanh() {
  if (i == 1) {
    digitalWrite(RL2_PIN, 0);
    digitalWrite(RL3_PIN, 0);
    digitalWrite(RL1_PIN, 1);
    digitalWrite(LED1_PIN, 1);
    digitalWrite(LED2_PIN, 0);
    digitalWrite(LED3_PIN, 0);
    RelayOnTime = millis();
  }

  if (i == 2) {
    digitalWrite(RL1_PIN, 0);
    digitalWrite(RL3_PIN, 0);
    digitalWrite(RL2_PIN, 1);
    digitalWrite(LED1_PIN, 0);
    digitalWrite(LED2_PIN, 1);
    digitalWrite(LED3_PIN, 0);
    RelayOnTime = millis();
  }

  if (i == 3) {
    digitalWrite(RL1_PIN, 0);
    digitalWrite(RL2_PIN, 0);
    digitalWrite(RL3_PIN, 1);
    digitalWrite(LED1_PIN, 0);
    digitalWrite(LED2_PIN, 0);
    digitalWrite(LED3_PIN, 1);
    RelayOnTime = millis();
  }
}

void chedo_congtac() {
  bool SW1 = digitalRead(SW1_PIN);
  if (SW1 == LOW && lastSW1State == HIGH) {
    startManualMove(1);
  }
  lastSW1State = SW1;

  bool SW2 = digitalRead(SW2_PIN);
  if (SW2 == LOW && lastSW2State == HIGH) {
    i = 2;
    van_hanh();
    stopManualMove();
  }
  lastSW2State = SW2;

  bool SW3 = digitalRead(SW3_PIN);
  if (SW3 == LOW && lastSW3State == HIGH) {
    startManualMove(3);
  }
  lastSW3State = SW3;
}

void startManualMove(int dir) {
  i = dir;
  van_hanh();
  setMatterMoving(dir == 1);

  if (totalTravelTime == 0) return;

  int target = (dir == 1) ? 100 : 0;
  if (target == currentPercent && !percentMoveActive) return;

  percentMoveActive = true;
  percentMoveStartPercent = currentPercent;
  percentMoveTargetPercent = target;
  percentMoveStartTime = millis();
  percentMoveDuration = (unsigned long)(abs(target - currentPercent) * msPerPercent);
  percentMoveNeedStop = false;
  lastPercentPrint = millis();

  reportMatterTarget(target);

  Serial.print(dir == 1 ? F("Dang mo cua (tay)... huong toi ") : F("Dang dong cua (tay)... huong toi "));
  Serial.print(target);
  Serial.println(F("%"));
}

void stopManualMove() {
  if (!percentMoveActive) {
    setMatterStalled();
    return;
  }

  unsigned long elapsed = millis() - percentMoveStartTime;
  float progress = (percentMoveDuration > 0) ? min(1.0f, (float)elapsed / (float)percentMoveDuration) : 1.0f;
  currentPercent = percentMoveStartPercent + (int)((percentMoveTargetPercent - percentMoveStartPercent) * progress);

  percentMoveActive = false;
  saveHanhTrinh();

  reportMatterPosition(currentPercent);
  setMatterStalled();

  Serial.print(F("Da dung. Vi tri hien tai: "));
  Serial.print(currentPercent);
  Serial.println(F("%"));
}

// ---- Đồng bộ vị trí sang Matter ----
void reportMatterPosition(int pct) {
  isUpdatingFromMatter = true; 
  uint16_t matterPercent100ths = (uint16_t)((100 - constrain(pct, 0, 100)) * 100);
  RollingDoor.setCurrentLiftPercent100ths(matterPercent100ths);
  isUpdatingFromMatter = false;
}

void reportMatterTarget(int pct) {
  isUpdatingFromMatter = true;
  uint16_t matterPercent100ths = (uint16_t)((100 - constrain(pct, 0, 100)) * 100);
  RollingDoor.setTargetLiftPercent100ths(matterPercent100ths);
  isUpdatingFromMatter = false;
}

void setMatterMoving(bool movingUp) {
  isUpdatingFromMatter = true;
  RollingDoor.setOperationalState(
    MatterWindowCovering::LIFT,
    movingUp ? MatterWindowCovering::MOVING_UP_OR_OPEN : MatterWindowCovering::MOVING_DOWN_OR_CLOSE
  );
  isUpdatingFromMatter = false;
}

void setMatterStalled() {
  isUpdatingFromMatter = true;
  RollingDoor.setOperationalState(MatterWindowCovering::LIFT, MatterWindowCovering::STALL);
  isUpdatingFromMatter = false;
}

// ---- Callback Matter ----
bool matterOpen() {
  if (isUpdatingFromMatter) return true;
  startManualMove(1);
  return true;
}

bool matterClose() {
  if (isUpdatingFromMatter) return true;
  startManualMove(3);
  return true;
}

bool matterStop() {
  if (isUpdatingFromMatter) return true;
  i = 2;
  van_hanh();
  stopManualMove();
  return true;
}

bool matterGoToLiftPercentage(uint8_t liftPercent) {
  if (isUpdatingFromMatter) return true;
  int target = 100 - liftPercent;
  moveToPercent(target, true); 
  return true;
}

// ==================== RESET MATTER ====================
void handleMatterResetButton() {
  if (matterResetPressCount > 0 && millis() - matterResetWindowStart > MATTER_RESET_WINDOW) {
    matterResetPressCount = 0;
  }

  bool state = digitalRead(MATTER_RESET_PIN);
  if (state == LOW && matterResetLastState == HIGH) {
    if (matterResetPressCount == 0) {
      matterResetWindowStart = millis();
    }
    matterResetPressCount++;

    if (matterResetPressCount >= MATTER_RESET_PRESS_NEEDED) {
      Serial.println(F("=== RESET MATTER (decommission) ==="));
      blinkLedHT(10, 100);
      Matter.decommission();
      matterResetPressCount = 0;
      delay(500);
      ESP.restart();
    }
  }
  matterResetLastState = state;
}

// ==================== NÚT SET ====================
void handleSetButton() {
  bool btSetState = digitalRead(BT_SET_PIN);

  if (btSetState == LOW && btSetLastState == HIGH) {
    btSetPressStart = millis();
    btSetLongTriggered = false;
  }

  if (btSetState == LOW && !setModeActive && !btSetLongTriggered) {
    if (millis() - btSetPressStart >= SET_HOLD_TIME) {
      btSetLongTriggered = true;
      enterSetMode();
    }
  }

  if (btSetState == HIGH && btSetLastState == LOW) {
    if (setModeActive && !btSetLongTriggered) {
      handleSetClick();
    }
  }

  btSetLastState = btSetState;
}

void enterSetMode() {
  i = 2;
  van_hanh();

  setModeActive = true;
  setState = SET_WAIT_UP;
  percentMoveActive = false;

  blinkLedHT(5, 300);
  Serial.println(F("== Da vao che do SET HANH TRINH =="));
}

void handleSetClick() {
  switch (setState) {
    case SET_UP_STOPPED:
      t_up = stopUpTime - startUpTime;
      blinkLedHT(1, 300);
      setState = SET_WAIT_DOWN;
      break;

    case SET_DOWN_STOPPED:
      t_down = stopDownTime - startDownTime;
      blinkLedHT(1, 300);
      setState = SET_READY_FINAL;
      break;

    case SET_READY_FINAL:
      totalTravelTime = t_up + t_down;
      msPerPercent = totalTravelTime / 100.0;
      saveHanhTrinh();

      blinkLedHT(3, 200);
      setModeActive = false;
      currentPercent = 0;
      reportMatterPosition(currentPercent);
      setMatterStalled();
      break;

    default:
      break;
  }
}

void chedo_setmode() {
  bool SW1 = digitalRead(SW1_PIN);
  bool SW2 = digitalRead(SW2_PIN);
  bool SW3 = digitalRead(SW3_PIN);

  if (setState == SET_WAIT_UP) {
    if (SW1 == LOW && lastSW1State == HIGH) {
      i = 1;
      van_hanh();
      startUpTime = millis();
      setState = SET_UP_RUNNING;
    }
  } else if (setState == SET_UP_RUNNING) {
    if (SW2 == LOW && lastSW2State == HIGH) {
      i = 2;
      van_hanh();
      stopUpTime = millis();
      setState = SET_UP_STOPPED;
    }
  } else if (setState == SET_WAIT_DOWN) {
    if (SW3 == LOW && lastSW3State == HIGH) {
      i = 3;
      van_hanh();
      startDownTime = millis();
      setState = SET_DOWN_RUNNING;
    }
  } else if (setState == SET_DOWN_RUNNING) {
    if (SW2 == LOW && lastSW2State == HIGH) {
      i = 2;
      van_hanh();
      stopDownTime = millis();
      setState = SET_DOWN_STOPPED;
    }
  }

  lastSW1State = SW1;
  lastSW2State = SW2;
  lastSW3State = SW3;
}

void blinkLedHT(int times, int ms) {
  for (int k = 0; k < times; k++) {
    digitalWrite(LED_HT_PIN, HIGH);
    delay(ms);
    digitalWrite(LED_HT_PIN, LOW);
    delay(ms);
  }
}

// ==================== LƯU / ĐỌC NVS ====================
void loadHanhTrinh() {
  prefs.begin("hanhtrinh", true); // Bật chế độ Read-Only để tránh lock
  t_up = prefs.getULong("t_up", 0);
  t_down = prefs.getULong("t_down", 0);
  totalTravelTime = prefs.getULong("total", 0);
  currentPercent = prefs.getInt("cur_pct", 0);
  prefs.end();

  if (totalTravelTime > 0) {
    msPerPercent = totalTravelTime / 100.0;
  }
}

void saveHanhTrinh() {
  prefs.begin("hanhtrinh", false);
  prefs.putULong("t_up", t_up);
  prefs.putULong("t_down", t_down);
  prefs.putULong("total", totalTravelTime);
  prefs.putInt("cur_pct", currentPercent);
  prefs.end();
}

// ==================== ĐIỀU KHIỂN THEO % ====================
void handleSerialInput() {
  if (Serial.available()) {
    String s = Serial.readStringUntil('\n');
    s.trim();
    if (s.length() == 0) return;

    int val = s.toInt();
    moveToPercent(val, false);
  }
}

void moveToPercent(int target, bool isFromMatter) {
  if (setModeActive || totalTravelTime == 0) return;

  target = constrain(target, 0, 100);

  if (target == currentPercent && !percentMoveActive) {
    return;
  }

  percentMoveActive = true;
  percentMoveStartPercent = currentPercent;
  percentMoveTargetPercent = target;
  percentMoveStartTime = millis();
  percentMoveDuration = (unsigned long)(abs(target - currentPercent) * msPerPercent);
  percentMoveNeedStop = !(target == 0 || target == 100);

  if (target > currentPercent) {
    i = 1;
    van_hanh(); 
    setMatterMoving(true);
  } else {
    i = 3;
    van_hanh(); 
    setMatterMoving(false);
  }

  if (!isFromMatter) {
    reportMatterTarget(target);
  }

  lastPercentPrint = millis();
}

void updatePercentMove() {
  if (!percentMoveActive) return;

  unsigned long elapsed = millis() - percentMoveStartTime;

  if (percentMoveDuration > 0) {
    float progress = min(1.0f, (float)elapsed / (float)percentMoveDuration);
    int curPct = percentMoveStartPercent + (int)((percentMoveTargetPercent - percentMoveStartPercent) * progress);

    if (millis() - lastPercentPrint >= 1500) { 
      Serial.print(F("Cua dang chay: "));
      Serial.print(curPct);
      Serial.println(F("%"));
      reportMatterPosition(curPct); 
      lastPercentPrint = millis();
    }
  }

  if (elapsed >= percentMoveDuration) {
    if (percentMoveNeedStop) {
      i = 2;
      van_hanh(); 
    }
    currentPercent = percentMoveTargetPercent;
    percentMoveActive = false;

    // Dừng báo cáo Matter trước khi ghi NVS để giải phóng RAM/Stack
    reportMatterPosition(currentPercent);
    setMatterStalled();

    saveHanhTrinh(); 

    Serial.print(F("Cua da den vi tri: "));
    Serial.print(currentPercent);
    Serial.println(F("%"));
  }
}