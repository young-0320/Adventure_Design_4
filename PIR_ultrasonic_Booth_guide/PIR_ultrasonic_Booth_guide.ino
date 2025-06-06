#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

// 핀 정의
const int TRIG_PIN = 3;
const int ECHO_PIN = 2;
const int PIR_PIN  = 7;
const int LED_PIN  = 9;
const int BUTTON_PIN = 8;

// DFPlayer Mini
SoftwareSerial mySerial(10, 11); // RX, TX
DFRobotDFPlayerMini dfplayer;

// 상수
const unsigned long DISTANCE_CHECK_INTERVAL = 1000;
const unsigned long VOICE_MIN_INTERVAL = 3000;
const unsigned long BUTTON_PAUSE_DURATION = 20000;  // 버튼 일시중지 시간 (20초)
const unsigned long ARRIVAL_PAUSE_DURATION = 10000; // 도착 후 대기 시간 (10초)
const long RESET_DISTANCE_THRESHOLD = 100;  // 시스템 초음파 리셋 임계값 (100cm)

const long ARRIVAL_DISTANCE_CM = 10;  // 도착 거리 설정 (10cm)
const long DISTANCE_CHANGE_SENSITIVITY = 20;  // 거리 변화 민감도 (20cm)

// 음성 트랙 정의
const int TRACK_FAR = 1;        // 멀어짐
const int TRACK_CLOSER = 2;     // 가까워짐
const int TRACK_STATIONARY = 3; // 정체
const int TRACK_ARRIVED = 4;    // 도착
const int TRACK_NEAR_BUS_STOP = 5;

// 상태 변수
long previousDistance = -1;
bool voiceIsPlaying = false;
bool systemActive = false;
bool arrivalPending = false;            // 도착 음성 예약 플래그
unsigned long voicePausedUntil = 0;
unsigned long lastVoicePlayedTime = 0;
unsigned long lastDistanceCheckTimeActive = 0;
unsigned long lastDistanceCheckTimeInactive = 0;
unsigned long buttonPauseStartTime = 0;
unsigned long arrivalReachedTime = 0;  // 도착 시각 기록 플래그 (0이면 도착하지 않음)

int lastButtonState = HIGH;

void setup() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT);

  Serial.begin(9600);
  mySerial.begin(9600);

  Serial.println("DFPlayer 초기화 시도 중...");
  delay(1000);

  int retries = 0;
  while (!dfplayer.begin(mySerial) && retries < 5) {
    Serial.print("DFPlayer 초기화 실패. 재시도 ");
    Serial.println(retries + 1);
    delay(1000);
    retries++;
  }
  if (retries >= 5) {
    Serial.println("DFPlayer 초기화 실패, 프로그램 중지.");
    while (true);
  }
  dfplayer.volume(4);
  Serial.println("시스템 대기 중...");
}

void playVoice(int track) {
  unsigned long now = millis();

  if (voiceIsPlaying) return;
  if (now < voicePausedUntil) return;
  if (track != TRACK_ARRIVED && now - lastVoicePlayedTime < VOICE_MIN_INTERVAL) return;

  dfplayer.play(track);
  voiceIsPlaying = true;
  lastVoicePlayedTime = now;

  Serial.print(now / 1000.0);
  Serial.print("s: playVoice 호출 - 트랙 ");
  Serial.println(track);
}

void stopVoice() {
  if (voiceIsPlaying) {
    dfplayer.stop();
    voiceIsPlaying = false;
    Serial.println("음성 중단");
  }
}

long getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 35000);
  if (duration == 0) return -1;
  return min(duration / 58.2, 400L);
}

void loop() {
  unsigned long now = millis();

  // 버튼 처리 - 비차단 방식
  int buttonState = digitalRead(BUTTON_PIN);
  if (buttonState == HIGH && lastButtonState == LOW) {
    Serial.println("버튼 눌림 - 20초 음소거 및 비활성화 시작");
    systemActive = false;
    stopVoice();
    voicePausedUntil = now + BUTTON_PAUSE_DURATION;
    previousDistance = -1;
    arrivalPending = false;
    arrivalReachedTime = 0;
    buttonPauseStartTime = now;
  }
  lastButtonState = buttonState;

  // 버튼 일시중단 20초 지나면 음성 재생 가능
  if (buttonPauseStartTime != 0 && now - buttonPauseStartTime >= BUTTON_PAUSE_DURATION) {
    buttonPauseStartTime = 0;
    Serial.println("버튼 일시중단 해제");
  }

  // DFPlayer 상태 체크
  if (dfplayer.available()) {
    uint8_t type = dfplayer.readType();
    int val = dfplayer.read();

    if (type == DFPlayerPlayFinished) {
      voiceIsPlaying = false;
      Serial.println("음성 재생 완료");

      // 도착 음성 재생 완료 시 시스템 비활성화 처리
      if (arrivalPending) {
        Serial.println("도착 음성 재생 시작");
        playVoice(TRACK_ARRIVED);
        arrivalPending = false;
      } else if (arrivalReachedTime != 0) {
        Serial.println("도착 음성 완료 - 시스템 비활성화");
        systemActive = false;
        previousDistance = -1;
      }
    } else if (type == DFPlayerError) {
      Serial.print("DFPlayer 오류 코드: ");
      Serial.println(val);
      voiceIsPlaying = false;
    }
  }

  // 음성 일시중단 중이면 이후 로직 중단
  if (now < voicePausedUntil) return;

  // PIR 처리
  int pirVal = digitalRead(PIR_PIN);
  if (pirVal == HIGH && !systemActive && arrivalReachedTime == 0) {
    Serial.println("PIR 감지 - 시스템 활성화");
    systemActive = true;
    previousDistance = -1;
    arrivalPending = false;
  }

  // 비활성화 상태 거리 측정 및 리셋
  if (!systemActive && now - lastDistanceCheckTimeInactive >= DISTANCE_CHECK_INTERVAL) {
    lastDistanceCheckTimeInactive = now;
    long dist = getDistance();
    if (dist != -1) {
      Serial.print("비활성화 상태 거리 측정: ");
      Serial.print(dist);
      Serial.println("cm");

      if (dist > RESET_DISTANCE_THRESHOLD) {
        Serial.println("거리 임계값 초과 - 시스템 초기 상태로 리셋");
        arrivalReachedTime = 0;
        previousDistance = -1;
        voicePausedUntil = 0;
        buttonPauseStartTime = 0;
        arrivalPending = false;
        lastVoicePlayedTime = 0;
        systemActive = false;  // 확실히 비활성화 유지
      }
    }
    // 비활성화 상태에서는 절대 음성 안내 호출 안 함
  }

  // 활성화 상태 거리 측정 및 음성 안내
  if (systemActive && now - lastDistanceCheckTimeActive >= DISTANCE_CHECK_INTERVAL) {
    lastDistanceCheckTimeActive = now;
    long dist = getDistance();

    if (dist == -1) {
      Serial.println("거리 측정 실패");
      return;
    }

    Serial.print("측정 거리: ");
    Serial.print(dist);
    Serial.println("cm");

    if (dist <= ARRIVAL_DISTANCE_CM) {
      if (arrivalReachedTime == 0 && !arrivalPending) {
        Serial.println("도착 거리 이내, 도착 음성 예약");
        arrivalPending = true;
        arrivalReachedTime = now;
        previousDistance = -1;
      }
    }

    if (!voiceIsPlaying) {
      if (previousDistance != -1) {
        if (dist > previousDistance + DISTANCE_CHANGE_SENSITIVITY) {
          playVoice(TRACK_FAR);
          previousDistance = dist;
        } else if (dist < previousDistance - DISTANCE_CHANGE_SENSITIVITY) {
          playVoice(TRACK_CLOSER);
          previousDistance = dist;
        } else if (now - lastVoicePlayedTime >= VOICE_MIN_INTERVAL) {
          playVoice(TRACK_STATIONARY);
          previousDistance = dist;
        }
      } else {
        previousDistance = dist;
      }
    }
  }
}
