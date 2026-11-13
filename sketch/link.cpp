#include "link.h"
#include "Arduino_RouterBridge.h"
#include <vector>

// Python이 Bridge로 보낸 최종 판단(소리 출력 여부)을 받는다.
// 실제 재생은 이 신호가 아니라 audio_rx의 "audio_play"가 트리거한다.
// 이 핸들러는 판단 시점을 Monitor에서 확인하고 CSV와 대조하기 위한 로그용이다.
static void onSoundDecision(int shouldPlay) {
  linkSendLine(shouldPlay ? "SOUND,1" : "SOUND,0");
}

// Bridge 초기화 + Python → MCU 핸들러 등록 (baud는 더 이상 쓰지 않음)
void linkBegin(long baud) {
  (void)baud;
  Monitor.begin(115200);
  Bridge.begin();
  Bridge.provide("sound_decision", onSoundDecision);
}

// STATE를 Bridge로 Python에 전달 (Python 쪽 Bridge.provide("state", ...)가 받음)
void linkSendState(unsigned long timeMs, int fsr, bool doorOpen, bool umbrellaPresent) {
  std::vector<int> payload = { (int)timeMs, fsr, doorOpen ? 1 : 0, umbrellaPresent ? 1 : 0 };
  Bridge.notify("state", payload);
}

// 로그 한 줄 — 시리얼 모니터와 Python 로그(앱 로그) 양쪽으로 보낸다.
// Python으로도 보내는 이유: 시리얼 모니터를 열지 않아도 MCU의 DBG/ERR을 앱 로그에서 볼 수 있어야
// 재생이 왜 안 되는지 진단할 수 있다.
void linkSendLine(const char* line) {
  Monitor.println(line);
  Bridge.notify("mcu_log", MsgPack::str_t(line));
}
