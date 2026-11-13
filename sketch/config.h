#pragma once

// ── 1단계: 센서 · 시리얼 ──────────────────────────────────────────────────────
const int          PIN_FSR            = A0;     // 실제 값: FSR402 핀
const int          PIN_DOOR           = 3;      // 실제 값: 자기 센서(MC-38) 핀
const long         SERIAL_BAUD        = 115200; // 임의의 값: 리눅스 측과 맞춰야 함

const unsigned long INTERVAL_FSR_MS   = 50;    // 임의의 값: FSR 읽기 주기(ms)
const unsigned long INTERVAL_DOOR_MS  = 20;    // 임의의 값: MC-38 읽기 주기(ms)
const int          FSR_SAMPLE_COUNT   = 5;     // 임의의 값: FSR 이동 평균 샘플 수
const int          FSR_ON_THRESHOLD   = 300;   // 임의의 값: 우산 있음 판정 기준. 9주차 실측 후 교체
const int          FSR_OFF_THRESHOLD  = 250;   // 임의의 값: 우산 없음 판정 기준. 9주차 실측 후 교체
const unsigned long DOOR_DEBOUNCE_MS  = 30;    // 임의의 값: MC-38 디바운스 시간(ms). 실측 후 조정
const unsigned long INTERVAL_STATE_MS = 500;   // 임의의 값: 변화 없을 때 STATE 전송 주기(ms)

// 임의의 값: NC 타입이므로 LOW=닫힘. 실제 극성 확인 후 HIGH로 변경 가능
const int          DOOR_CLOSED_LEVEL  = LOW;

// 임의의 값: true이면 시리얼 입력으로 센서 흉내(f=우산 토글, d=문 토글). 기본 false
// 주의: SIMULATE_SENSORS=true 상태에서 2단계 음성 수신과 Serial이 충돌함. 동시에 쓰지 말 것
const bool         SIMULATE_SENSORS   = false;

// ── 2단계: 음성 수신 (Bridge RPC 방식) ────────────────────────────────────────
// 수신은 Bridge.provide("audio_begin"/"audio_chunk"/"audio_play")로 이뤄진다.
// 청크 크기는 보내는 쪽(python/tts_player.py의 AUDIO_CHUNK_BYTES)이 정하고, MCU는 들어온 만큼 담는다.
// 주의: Bridge RPC 1회 페이로드 상한은 약 256바이트다(Arduino_RPClite의
//       DECODER_BUFFER_SIZE 1024 / 4 = 256, bridge.h가 라우터에 이 값을 통보).
//       초과하면 BUFFER_LIMIT_EXCEEDED(0x06)로 실패한다.

// 임의의 값: 허용 최대 PCM 길이(초). 버퍼 = 8000바이트/초 x 이 값
// 5초였을 때 실제 안내 문장(5.47초 = 43,776바이트)이 거절당해서 8초로 올렸다.
// 측정값(2026-11-13): 버퍼 40,000일 때 스케치 .bss 총량이 49,016바이트였다.
//   → 8초(64,000)로 올리면 .bss는 약 73,000바이트가 된다.
// 주의: 더 늘릴 때는 RAM을 재확인할 것. 링커 맵이 재배치 오브젝트라 전체 SRAM을 확정하지 못했고,
//       코드 주석의 "786KB"는 미검증 수치다. 상황 2의 긴 문장(10초 이상)은 여전히 넘칠 수 있다.
const int           AUDIO_MAX_SECONDS    = 8;
const int           AUDIO_MAX_BYTES      = 8000 * AUDIO_MAX_SECONDS; // = 64,000바이트

// 임의의 값: audio_begin 이후 청크가 끊겼다고 보는 대기 시간(ms). 초과 시 버퍼 비우고 IDLE 복귀
const unsigned long AUDIO_CHUNK_TIMEOUT_MS = 3000;

// 임의의 값: ERR 줄을 출력할지 여부
const bool          ERR_REPORT_ENABLED   = true;
// 임의의 값: DBG 줄 출력 여부. 운용 시 false로 변경
const bool          DEBUG_PRINT          = true;

// ── 3단계: 음성 재생 ──────────────────────────────────────────────────────────
#define AUDIO_MODE_BUZZER   0
#define AUDIO_MODE_D5_PWM   1
// 임의의 값: 0=부저 비프 패턴(폴백), 1=D5 PWM으로 PCM 음성 재생.
// D5 경로가 하드웨어 문제로 안 되면 0으로 내려서 비프음만이라도 확인할 수 있게 둔 스위치
const int           AUDIO_OUTPUT_MODE       = AUDIO_MODE_D5_PWM;

// ── PCM 재생 (AUDIO_MODE_D5_PWM) ──
// LM386 아날로그 앰프가 D5(PA11, TIM1_CH4)에 연결되어 있다.
// 중요: D5와 LM386 입력 사이에 RC 저역통과 필터가 필요하다. 없으면 반송파 사각파를 그대로 증폭한다.
const int           PCM_PWM_PIN             = 5;     // 실제 값: LM386 입력이 연결된 핀
// D5가 오버레이의 zephyr,user/pwms 배열에서 몇 번째인지. 확인된 순서:
// 0=D2, 1=D3, 2=D5, 3=D6, 4=D7, 5=D8, 6=D9, 7=D10, 8=D11, 9=D12
// const가 아니라 #define이어야 한다 — PWM_DT_SPEC_GET_BY_IDX가 매크로 토큰 결합에 쓰므로 리터럴이 필요
#define PCM_PWM_DT_INDEX    2                        // 확인된 값: D5/PA11 → TIM1_CH4

// 임의의 값: PWM 반송파 주파수(Hz). 가청 영역(20kHz) 위여야 하고, 높을수록 음질이 좋지만
// 듀티 분해능이 줄어든다. 오버레이의 PWM_HZ(500)은 analogWrite() 기본값일 뿐이며 여기서 덮어쓴다
const unsigned int  PCM_PWM_CARRIER_HZ      = 32000;
const unsigned int  PCM_PWM_PERIOD_NS       = 1000000000u / PCM_PWM_CARRIER_HZ; // = 31,250ns

// PCM 포맷: 8kHz, unsigned 8bit, mono (python 쪽 ffmpeg 변환과 반드시 일치해야 함)
const unsigned int  PCM_SAMPLE_RATE         = 8000;
const unsigned int  PCM_SAMPLE_PERIOD_US    = 1000000u / PCM_SAMPLE_RATE; // = 125us

// 임의의 값: 샘플 펌프 스레드 스택(바이트). 늘릴 일은 거의 없음
// #define 유지 — K_THREAD_STACK_DEFINE 매크로에 들어가므로
#define PCM_THREAD_STACK_SIZE   1024
// 임의의 값: 샘플 펌프 스레드 우선순위. 숫자가 작을수록 높다.
// 너무 높이면 재생 중 Bridge 통신(STATE 전송)이 밀리고, 너무 낮추면 음성이 끊긴다. 들어보고 조정
const int           PCM_THREAD_PRIORITY     = 5;

// ── 앰프 전원 차단 (유휴 시 노이즈 제거) ──
// LM386은 셧다운 핀이 없으므로, 외부 스위치(P-MOSFET 등)로 V+를 끊어 완전 무음을 만든다.
// 재생하지 않는 동안 앰프를 꺼두면 앰프 자체가 만드는 히스까지 사라진다.
//
// 임의의 값: 배선하기 전까지는 false로 둔다.
//   true로 바꾸기 전에 반드시 AMP_MUTE_PIN과 AMP_ON_LEVEL을 실제 회로에 맞출 것.
//   배선 없이 true로 두면 앰프가 영영 켜지지 않아 소리가 안 난다.
const bool          AMP_MUTE_ENABLED        = false;

// 임의의 값: 앰프 전원 스위치를 제어할 핀. 비어 있는 핀 아무거나(D4, D6~D13) 쓰면 된다
const int           AMP_MUTE_PIN            = 4;
// 임의의 값: 이 레벨일 때 앰프가 켜진다.
//   P-MOSFET 하이사이드 스위치를 게이트 직결로 쓰면 보통 LOW가 켜짐이다. 회로 확정 후 교체
const int           AMP_ON_LEVEL            = HIGH;

// 임의의 값: 앰프를 켠 뒤 안정될 때까지 기다리는 시간(ms). 짧으면 앞부분이 잘린다
const int           AMP_WAKE_MS             = 200;
// 임의의 값: 재생이 끝나고 앰프를 끄기까지 기다리는 시간(ms). 짧으면 끝부분이 잘린다
const int           AMP_TAIL_MS             = 150;

// ── 팝 억제 ──
// unsigned 8bit PCM의 무음 수준. 재생 전후로 이 값에 머물러야 단차(=팝)가 생기지 않는다
const int           PCM_SILENCE_LEVEL       = 128;
// 임의의 값: 재생 시작·끝에서 음량을 서서히 올리고 내리는 시간(ms).
//   첫 샘플이 무음 수준에서 멀면 "딱" 소리가 나는데, 이 페이드가 그걸 없앤다.
//   너무 길면 말머리가 뭉개진다
const int           PCM_FADE_MS             = 8;

// ── 부저 폴백 (AUDIO_MODE_BUZZER) ──
const int           BUZZER_PIN              = 5;    // 임의의 값: 폴백 시에도 같은 핀을 쓴다고 가정
const int           BEEP_FREQ               = 1000; // 임의의 값: 비프 주파수(Hz)
const int           BEEP_COUNT              = 3;    // 임의의 값: 비프 횟수. 사용자 테스트 후 조정
const unsigned long BEEP_ON_MS              = 200;  // 임의의 값: 비프 켜짐 시간(ms)
const unsigned long BEEP_OFF_MS             = 150;  // 임의의 값: 비프 꺼짐 간격(ms)

// 임의의 값: 재생 최대 허용 시간(ms). 초과 시 ERR,PLAY 후 복귀.
// 최대 길이 음성(5초)보다 충분히 커야 한다
const unsigned long PLAY_TIMEOUT_MS         = 30000;
