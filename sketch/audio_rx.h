#pragma once
#include <Arduino.h>

// 음성은 "미리 올려두고(commit) 나중에 재생(play)"하는 2단계로 동작한다.
// 문이 열린 뒤에 생성·전송을 시작하면 수 초가 걸려 늦기 때문에,
// Python이 날씨 문장이 정해진 시점에 미리 전송해두고 알림 순간에는 재생만 지시한다.
//
// Bridge 메서드: audio_begin → audio_chunk×N → audio_commit (여기까지 미리) → audio_play (알림 순간)

enum AudioRxState { AUDIO_IDLE, AUDIO_RECEIVING, AUDIO_RECEIVED };

void           audioRxBegin();
void           audioRxUpdate(unsigned long now); // 매 loop()에서 호출 (수신 타임아웃 감시)
bool           audioRxIsComplete();
const uint8_t* audioRxGetData();
int            audioRxGetSize();
AudioRxState   audioRxGetState();
void           audioRxReset(); // 재생 후 IDLE 복귀 — 버퍼는 남겨서 다시 재생할 수 있게 한다
