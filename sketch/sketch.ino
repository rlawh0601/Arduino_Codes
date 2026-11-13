#include "config.h"
#include "sensors.h"
#include "link.h"
#include "audio_rx.h"
#include "audio_player.h"

enum AppState { SENSING, RECEIVING, PLAYING };

static AppState      appState    = SENSING;
static SystemState   prevState   = {0, false, false};
static unsigned long lastSendMs  = 0;
static unsigned long playStartMs = 0;
static bool          firstLoop   = true;

// 현재 센서 상태를 STATE로 즉시 전송 (Bridge로 Python에 전달)
static void sendState() {
  SystemState s = sensorsGetState();
  linkSendState(millis(), s.fsr, s.doorOpen, s.umbrellaPresent);
  lastSendMs = millis();
  prevState  = s;
  firstLoop  = false;
}

// 전체 상태 전환 로직 — loop()는 이 함수와 센서/수신 갱신만 호출
static void runStateMachine(unsigned long now) {
  AudioRxState rxSt = audioRxGetState();

  if (appState == SENSING) {
    SystemState s = sensorsGetState();
    bool changed = (s.doorOpen != prevState.doorOpen ||
                    s.umbrellaPresent != prevState.umbrellaPresent);
    if (firstLoop || changed || (now - lastSendMs >= INTERVAL_STATE_MS))
      sendState();

    if (rxSt == AUDIO_RECEIVED) {
      // 미리 올려둔 음성에 재생 요청이 들어왔다 — 전송을 기다리지 않고 바로 재생
      playerStart(audioRxGetData(), audioRxGetSize());
      appState = PLAYING; playStartMs = now;
    } else if (rxSt == AUDIO_RECEIVING) {
      appState = RECEIVING;  // 미리 올리는 중 (알림과 무관)
    }

  } else if (appState == RECEIVING) {
    // 미리 올리는 중. 끝나면(commit 성공/실패 모두) IDLE이 되므로 센서 감지로 복귀한다
    if (rxSt == AUDIO_RECEIVED) {
      playerStart(audioRxGetData(), audioRxGetSize());
      appState = PLAYING; playStartMs = now;
    } else if (rxSt == AUDIO_IDLE) {
      appState = SENSING; sendState();
    }

  } else if (appState == PLAYING) {
    playerUpdate(now);
    bool timeout = (now - playStartMs > PLAY_TIMEOUT_MS);
    if (timeout) {
      playerStop();
      if (ERR_REPORT_ENABLED) linkSendLine("ERR,PLAY");
    }
    if (timeout || !playerIsPlaying()) {
      audioRxReset();  // 버퍼 초기화 + IDLE 복귀 (다음 음성을 받을 수 있게)
      appState = SENSING; sendState();
    }
  }
}

void setup() {
  linkBegin(SERIAL_BAUD);
  sensorsBegin();
  audioRxBegin(); // IDLE 진입 + 음성 수신 Bridge 핸들러 3개 등록
  playerBegin();
}

void loop() {
  unsigned long now = millis();
  sensorsUpdate(now);
  audioRxUpdate(now);
  runStateMachine(now);
}
