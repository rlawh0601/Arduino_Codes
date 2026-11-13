#pragma once
#include <Arduino.h>

// 재생 구현은 config.h의 AUDIO_OUTPUT_MODE로 고른다.
//  - AUDIO_MODE_D5_PWM : D5의 PWM 반송파 듀티를 8kHz로 바꿔 PCM 음성을 재생 (실제 음성)
//  - AUDIO_MODE_BUZZER : 정해진 비프 패턴만 울리는 폴백 (수신 데이터는 쓰지 않음)
// 공개 시그니처는 1~3단계와 동일하게 유지한다 — sketch.ino의 상태머신이 그대로 쓴다.

void playerBegin();
void playerStart(const uint8_t* data, int size); // 재생 시작 (블로킹 안 함)
void playerUpdate(unsigned long now);            // 매 loop()에서 호출 (블로킹 금지)
bool playerIsPlaying();
void playerStop();
