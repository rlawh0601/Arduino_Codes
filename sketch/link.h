#pragma once
#include <Arduino.h>

// 확인 완료: UNO Q에서 MCU↔리눅스 통신은 Arduino Router Bridge(Bridge.call/notify/provide)를 쓴다.
// ttyGS0 시리얼은 arduino-router-serial 서비스(Monitor/Bridge 전용 프록시)가 이미 점유하고 있어
// 사용자 코드에서 Serial로 직접 접근하면 안 된다.
//
// 음성 수신은 이 모듈이 아니라 audio_rx.cpp가 Bridge.provide로 직접 처리한다.
// (바이트 단위 읽기 함수들은 Bridge 전환으로 쓸모가 없어져 제거했다)

void linkBegin(long baud); // baud는 쓰지 않음(Bridge로 전환) — 호출부를 바꾸지 않으려 시그니처만 유지
void linkSendState(unsigned long timeMs, int fsr, bool doorOpen, bool umbrellaPresent); // STATE를 Python에 전달
void linkSendLine(const char* line); // 로그 한 줄 — Monitor로 출력 (OK/ERR/DBG)
