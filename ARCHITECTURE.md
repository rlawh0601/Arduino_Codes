# Umbrella Alarm — 구조 요약 (AI 참고용)

> 코드를 손볼 때 전체를 다시 읽지 않도록 만든 요약본.
> **코드를 바꾸면 이 문서도 같이 갱신한다.** 문서와 코드가 다르면 코드가 정답.
> 마지막 갱신: 2026-11-12 (TTS 음성 출력 연결)

## 1. 한 줄 요약

문이 닫힘→열림으로 바뀐 순간에 "비 예보 있음 + 우산 거치대에 있음"이면
gTTS로 만든 한국어 음성을 아두이노 D5의 LM386 앰프로 재생해 우산을 챙기라고 알리는 장치.

## 2. 좌표

| 항목 | 값 |
| :--- | :--- |
| 앱 ID | `dXNlcjp1bWJyZWxsYV9hbGFybV9hcHA` |
| 보드 경로 | `/home/arduino/ArduinoApps/umbrella_alarm_app` |
| 로컬 체크아웃 | `...\ai-mirror\3265164066__user-umbrella_alarm_app-8b2a9c` |
| GitHub | `https://github.com/damulto5596-oss/Arduino_Codes.git` (사용자가 수동 push) |
| 보드 | Arduino UNO Q (aarch64 / MCU는 STM32U585) |
| 앱 bricks | **없음** (app.yaml에 bricks 항목 자체가 없음) |

## 3. 데이터 흐름

```
[MCU] sensors.cpp         FSR(A0) + 자기센서(D3) 읽기
   ↓ linkSendState()      변화 시 즉시 / 무변화 시 500ms 주기
   ↓ Bridge.notify("state", vector<int>{time_ms, fsr, door_open, umbrella_present})
[Python] main.py _on_state()            ← Bridge 디스패치 스레드
   ↓ process_sensor_state()  (arduino_speaker.py)
   ↓ check_alert()           (alert_logic.py) → 알릴 문장(str) 또는 None
   ↓ Bridge.notify("sound_decision", 0/1)     — 판단 시점 로그용
   ↓ 문장이 있으면 _pending_tts_text 에 넣기만 함
[Python] main.py loop()                 ← user_loop 스레드 (알림보다 **먼저**, 문장이 바뀔 때만)
   ↓ tts_player.prepare_text(weather_text)
   ↓   gTTS → MP3 → ffmpeg → PCM(8kHz, u8, mono)
   ↓ Bridge.call("audio_begin", size) → call("audio_chunk", 128B)×N → call("audio_commit")
[MCU] audio_rx.cpp  버퍼에 보관(audioValid=true), 상태는 IDLE 유지 — 아직 재생 안 함

   ── 문이 열리는 순간 ──
[Python] _on_state() → tts_player.request_play() → Bridge.notify("audio_play")  ← RPC 1회
[MCU] audio_rx.cpp  AUDIO_RECEIVED로 전이
   ↓ sketch.ino runStateMachine()  RECEIVING → PLAYING
   ↓ audio_player.cpp  전용 스레드가 8kHz로 D5 PWM 듀티 갱신 → LM386 → 스피커
   ↓ 재생 끝 → audioRxReset() → SENSING 복귀 + STATE 1회 전송

별도 경로 (Python):
  refresh_weather() ─600초마다─> tts_output.write_text() ─> 기상청 API ─> (안내문장, rain_flag)
  loop() ─0.1초마다─> csv_logger.log_row() ─> python/data/sensor_log.csv
```

**왜 "미리 올려두고 나중에 재생"인가**: 문이 열린 뒤에 생성·전송을 시작하면
gTTS 네트워크 호출(1~2초) + 수백 회의 Bridge RPC 때문에 **문이 닫힌 뒤에야 소리가 났다**(실제 증상).
그래서 문장이 정해진 시점에 `audio_commit`까지 끝내두고, 알림 순간에는 `audio_play` notify 1회만 보낸다.

**왜 notify인가**: Python `Bridge.provide` 핸들러(`_on_state`) 안에서는 `Bridge.call`이
금지(중첩 호출 불가)되어 있다. `notify`는 응답을 기다리지 않으므로 핸들러에서도 쓸 수 있다.

## 4. 파일별 역할

### Python (`python/`) — Linux(MPU)에서 실행
| 파일 | 역할 | 상태 |
| :--- | :--- | :--- |
| `main.py` | **진입점**. `state` 핸들러 등록 + 0.1초 루프(CSV, 날씨 갱신, 대기 중 음성 전송) | 동작 |
| `arduino_speaker.py` | 오케스트레이터. `refresh_weather()`, `process_sensor_state()`. 모듈 전역 `weather_text`/`rain_flag` | 동작 |
| `alert_logic.py` | 최종 판단. 모듈 전역 `previous_door_open`으로 문 전환 감지. **반환: 문장(str) 또는 None** | 동작 |
| `tts_output.py` | 기상청 단기예보 API 호출·파싱. `write_text()` → `(안내문장, rain:bool)`. **비 예보 없음·API 실패 시 안내문장은 빈 문자열**(음성 출력 안 함) | 동작 |
| `csv_logger.py` | CSV 기록. `update_sensors()`/`update_decision()`로 캐시 → `log_row()` | 동작 |
| `tts_player.py` | gTTS → PCM → Bridge 전송. `prepare_text()`(미리 올리기) / `request_play()`(즉시 재생 지시) | 동작 |
| `task_processor.py` | `light_text()` 래퍼. **현재 main.py에서 호출하지 않음**(고아) | 미사용 |
| `update_location.py` | 지명 → 기상청 격자(nx,ny) 변환 유틸. 런타임에선 호출 안 됨 | 독립 유틸 |
| `config.json` | API 키 + 위치. **실제 기상청 키가 평문** (사용자가 GitHub 공개 선택) | — |
| `README.md` | **내용이 낡음** (task_processor를 "스케줄러"라 설명 — 사실 아님) | 낡음 |
| `weather.md` | 원래 3단계 설계 명세서. **통신 규약 부분은 전부 폐기**(Serial→Bridge RPC) | 낡음 |
| `data/sensor_log.csv` | 수집 결과. 런타임 생성물 | 동작 |

### 스케치 (`sketch/`) — MCU(Zephyr)에서 실행
| 파일 | 역할 | 상태 |
| :--- | :--- | :--- |
| `sketch.ino` | `setup()`/`loop()` + `runStateMachine()`. `SENSING`→`RECEIVING`→`PLAYING` | 동작 |
| `sensors.cpp/.h` | FSR 이동평균+히스테리시스, 자기센서 디바운스 → `SystemState` | 동작 |
| `link.cpp/.h` | Bridge 초기화, `linkSendState()`, `onSoundDecision()`, `linkSendLine()` | 동작 |
| `config.h` | 모든 상수. 핀·임계값·타이밍·PWM 설정 | 동작 |
| `audio_rx.cpp/.h` | **Bridge RPC 수신**(`audio_begin`/`audio_chunk`/`audio_play`). `k_mutex`로 스레드 보호 | 동작 |
| `audio_player.cpp/.h` | D5 PWM PCM 재생(전용 스레드) / 부저 폴백 | 동작(미검증) |
| `sketch.yaml` | `arduino:zephyr` 플랫폼, 라이브러리 없음 | — |

## 5. 핀 / 상수 (config.h)

| 상수 | 값 | 비고 |
| :--- | :--- | :--- |
| `PIN_FSR` | `A0` | FSR402 (우산 무게) — 확정. **주의: A0=PA4는 DAC0과 겹침** |
| `PIN_DOOR` | `3` | 자기센서 MC-38 (문) — 확정 |
| `PCM_PWM_PIN` | `5` | LM386 입력 — 확정. D5=PA11=TIM1_CH4 |
| `PCM_PWM_DT_INDEX` | `2` | `#define`이어야 함(DT 매크로 토큰 결합). pwms 배열에서 D5의 위치 |
| `PCM_PWM_CARRIER_HZ` | `32000` | 가청 위. DTS의 `PWM_HZ(500)`을 런타임에 덮어씀 |
| `PCM_SAMPLE_RATE` | `8000` | **python의 `PCM_SAMPLE_RATE`와 반드시 일치** |
| `AUDIO_OUTPUT_MODE` | `D5_PWM` | `BUZZER`로 내리면 비프음 폴백 |
| `AMP_MUTE_ENABLED` | `false` | **배선 후 true로 바꿀 것.** 유휴 시 앰프 전원을 끊어 노이즈 제거 |
| `AMP_MUTE_PIN` / `AMP_ON_LEVEL` | `4` / `HIGH` | 앰프 전원 스위치 제어. 실제 회로에 맞춰 교체(P-MOSFET 직결이면 보통 LOW가 켜짐) |
| `AMP_WAKE_MS` / `AMP_TAIL_MS` | `200` / `150` | 앰프 안정화 대기 / 꼬리 대기. 앞뒤가 잘리면 늘린다 |
| `PCM_FADE_MS` | `8` | 재생 시작·끝 페이드. 클릭음 제거 |
| `AUDIO_MAX_BYTES` | `64000` | 8000B/s × 8초. **`tts_player.py`의 같은 이름 상수와 반드시 일치시킬 것** |
| `PCM_THREAD_PRIORITY` | `5` | 재생 중 Bridge가 밀리면 조정 |
| `DOOR_CLOSED_LEVEL` | `LOW` | NC 가정. 실측 미확인 |
| `FSR_ON/OFF_THRESHOLD` | `300`/`250` | 히스테리시스. 9주차 실측 후 교체 |
| `AUDIO_CHUNK_BYTES` | `128` | **python/tts_player.py에 있음**(보내는 쪽이 주인) |

## 4-1. MCU 로그 보는 법 (중요)

스케치의 `linkSendLine()`은 **Monitor와 Python 로그 양쪽**으로 나간다
(`Bridge.notify("mcu_log", ...)` → `main.py`의 `_on_mcu_log` → `[MCU] ...`).
따라서 `apps_logs`만 봐도 MCU의 `DBG,`/`ERR,`/`OK`를 확인할 수 있다.
**이게 없으면 재생 실패 원인을 전혀 볼 수 없다** — 시리얼 모니터를 열지 않는 한 MCU는 침묵한다.

정상 흐름에서 보여야 할 줄:
`[MCU] DBG,PWMREADY` (부팅) → `[MCU] DBG,BEGIN,43776` → `[MCU] OK` → `[MCU] DBG,COMMIT,43776`
→ (문 열림) `[MCU] DBG,PLAYREQ` → `[MCU] DBG,PLAY,43776`

주요 오류: `ERR,PWMINIT`(PWM 초기화 실패 — pinctrl 우회 실패), `ERR,PLAY`(재생 불가),
`ERR,NOAUDIO`(올려둔 음성 없음), `ERR,BUSY`(수신/재생 중), `ERR,SIZE`(버퍼 초과)

## 4-2. 앰프 전원 차단 + 팝 억제

LM386은 셧다운 핀이 없어서 외부 스위치로 V+를 끊어야 유휴 노이즈가 사라진다.
`audio_player.cpp`가 재생 전후로 그 스위치를 제어하고, 팝은 아래 3가지로 억제한다.

1. **타이밍** — 앰프 ON → `AMP_WAKE_MS` 대기 → 재생 → `AMP_TAIL_MS` 대기 → 앰프 OFF
   (전용 PCM 스레드에서 수행하므로 `loop()`는 막히지 않는다)
2. **페이드 인/아웃** — 시작·끝 `PCM_FADE_MS` 구간에서 유휴 레벨 ↔ 샘플을 선형 보간.
   첫/마지막 샘플이 유휴 레벨과 멀 때 나는 "딱" 소리를 없앤다
3. **유휴 레벨 일치** — 재생 전후로 핀이 머무는 값과 페이드의 시작·끝 값을 같게 맞춘다.
   `idleLevel()`이 그 값을 정한다:
   - `AMP_MUTE_ENABLED=true` → **128**(무음 중앙). 커플링 캡이 미리 충전돼 팝이 작다
   - `AMP_MUTE_ENABLED=false` → **0**. 앰프를 못 끄는데 DC를 걸면 LM386이 포화되므로 LOW 주차

**출력 커패시터(220µF) 충전으로 생기는 팝은 하드웨어 영역이라 소프트웨어로 막을 수 없다.**
완전히 없애려면 셧다운 핀과 팝 억제 회로가 내장된 앰프(PAM8403, MAX98357A)로 교체해야 한다.

## 5-1. 테스트 스위치

`python/tts_output.py` 맨 위의 **`FORCE_RAIN_FOR_TEST`를 `0` → `1`**로 바꾸면
API를 건너뛰고 "지금 비가 오는 중"으로 강제한다(인터넷·API 키 불필요).
`rain=True` + 비어 있지 않은 문장을 동시에 만들어야 알림이 성립하므로 둘을 함께 반환한다.
나머지 두 조건(문 닫힘→열림 전환, FSR 눌러 우산 있음)은 물리적으로 만들어야 한다.
**운용 시 반드시 0으로 되돌릴 것.**

참고: `config.h`의 `SIMULATE_SENSORS`는 Serial을 직접 쓰므로 Bridge 전환 후에는 **사용 불가**다.

## 6. CSV 규칙

- 경로: `python/data/sensor_log.csv` (폴더·헤더 자동 생성)
- 헤더: `time,fsr,door_open,rain,umbrella_taken,alert`
- 간격: **0.1초**. `time` 형식 `2026-11-04 17:06:14.9`
- `umbrella_taken` = `umbrella_present`와 같은 값 (1 = 거치대에 우산 있음)
- `alert` = 최종 판단(문장이 나왔으면 1)
- **"최신값 캐시" 방식**: STATE가 올 때만 갱신되고 CSV는 0.1초마다 캐시를 찍는다
  → 같은 값이 최대 5줄 반복, `alert=1`도 다음 STATE까지(~0.5초) 유지
  → 음성 전송 중에는 MCU가 STATE를 보내지 않으므로 그 수 초간 값이 고정된다

## 7. 플랫폼 제약 — 반드시 기억할 것

### MCU↔Linux 통신은 **Bridge만** 가능
- `/dev/ttyGS0`은 `arduino-router-serial.service`가 점유(`socat ... b9600 tcp:127.0.0.1:7500`)
- pyserial로 직접 열면 충돌 → **절대 금지**. `/dev/ttyUSB0`은 존재하지 않음
- 스케치: `#include "Arduino_RouterBridge.h"`, `Bridge.begin()`, `Monitor.begin(115200)`

### Bridge RPC 페이로드 상한 ≈ **256바이트**
- `Arduino_RPClite/src/Arduino_RPClite.h`: `DECODER_BUFFER_SIZE 1024`
- `request.h`: `DEFAULT_RPC_BUFFER_SIZE = DECODER_BUFFER_SIZE / 4` = **256**
- `bridge.h:217`이 `notify(SET_BUF_METHOD, 256)`으로 라우터에 통보 → 초과 시 `BUFFER_LIMIT_EXCEEDED(0x06)`
- 그래서 PCM 청크는 128바이트. 원래 설계의 512는 불가능

### 바이너리 전송은 가능
- MessagePack-RPC 기반(`router_bridge/protocol.py`)
- 라이브러리 선례(`hci.h`): 송신 `BinaryView`, 수신 `std::vector<uint8_t>`
- Python에서 `bytes`를 넘기면 msgpack `bin`으로 전달됨

### Python `Bridge.provide` 핸들러 안에서 `Bridge.call` 금지
```
RuntimeError: Cannot call 'X' from a provided handler: nested bridge calls are not supported.
```
→ 핸들러에서는 `notify`만. `call`은 `user_loop`에서. (이 때문에 `_pending_tts_text` 큐가 있다)

### PWM / DAC 실측 정보 (코어·DTS 직접 확인)
- 오버레이의 `pwms` 순서: `0=D2, 1=D3, 2=D5, 3=D6, 4=D7, 5=D8, 6=D9, 7=D10, 8=D11, 9=D12`
- 전부 `PWM_HZ(500)`이지만 이는 **`analogWrite()`가 쓰는 기본값**일 뿐.
  `pwm_set_dt()`로 런타임에 주기를 바꿀 수 있다 (= 오디오 반송파 확보 가능)
- `analogWrite()`는 주파수 변경 API가 없음(`analogWriteFrequency` 부재)
- **코어의 pinctrl 적용 함수가 static** → `analogWrite(pin, v)`을 1회 호출해 먹싱시킨 뒤
  `pwm_set_dt()`로 넘겨받는 우회를 쓴다 (`audio_player.cpp`의 `playerBegin()`)
- `tone()`은 PWM이 아니라 **k_timer + GPIO 토글** (`Tone.cpp`)
- **DAC도 존재**: `dac-channels = <1>,<2>`, 12bit, PA4(=**A0**)·PA5(=**A1**).
  `analogWrite(DAC0/DAC1, v)` API 있음. A0은 FSR이 쓰는 중이라 **A1만 여유**.
  D5 PWM이 실패하면 LM386을 A1로 옮기는 게 가장 안전한 대안 (RC 필터도 불필요)
- 스케치/라이브러리에서 zephyr 헤더 include 가능 (코어의 RTC/SPI가 이미 그렇게 함)

### 앱 venv에 pip 패키지 설치하는 방법
앱은 도커 컨테이너로 실행. venv는 `/app/.cache/.venv`(호스트 `<앱경로>/.cache/.venv`),
`uv venv --system-site-packages`로 생성되고 **존재하면 재사용**(설치 결과 유지).
호스트 쉘에는 python/uv/pip이 없으므로 같은 이미지로 1회용 컨테이너를 띄운다:

```sh
docker run --rm --network umbrella_alarm_app_default --entrypoint sh \
  -v /home/arduino/ArduinoApps/umbrella_alarm_app:/app \
  ghcr.io/arduino/app-bricks/python-apps-base:0.13.1 \
  -c "uv pip install --python /app/.cache/.venv/bin/python <패키지>"
```
- 이미지 태그 확인: `docker inspect umbrella_alarm_app-main-1 --format '{{.Config.Image}}'`
- `--network` 없으면 DNS 실패. 보드 인터넷도 필요(`wifi_status` — **자주 끊김**)
- 참고: `python/requirements.txt`를 두면 `/run.sh`가 기동 시 자동 설치 (아직 안 씀)

### 설치 현황 (앱 venv) — 2026-11-12 기준
| 패키지 | 상태 |
| :--- | :--- |
| `gTTS` 2.5.4 | **설치됨** + `python/requirements.txt`에 고정 |
| `imageio-ffmpeg` 0.8.1 | **설치됨** + 고정. aarch64 ffmpeg 바이너리 동봉(약 58MB) |
| `pyserial` 3.5 | 설치됨이지만 **더 이상 안 씀** (requirements.txt에도 없음) |

`python/requirements.txt`가 있으므로 venv가 지워져도 다음 기동 때 `/run.sh`가 자동 복구한다.
수동 설치가 필요하면 위 docker 명령을 쓴다.

**실측 검증 완료(2026-11-12)**: ffmpeg aarch64 바이너리 실행 OK,
gTTS 한국어 "우산을 챙기세요" → MP3 14,208B → PCM 14,208B = **1.78초, 128B 청크 111개**.
`AUDIO_MAX_BYTES`(40,000) 안에 충분히 들어간다.

### 체크아웃 함정 (실제로 당했음)
다른 앱/예제를 체크아웃하면 이전 체크아웃 폴더가 **비활성**이 되고,
그 폴더를 편집해도 **보드에 반영되지 않는다**(경고만 뜸).
→ 작업 전 `apps_checkout`으로 최신 경로 재확인. 증상: 고친 코드인데 옛 에러가 계속 남.

### 런타임 생성 파일
앱이 실행 중 만든 파일(CSV 등)은 체크아웃 미러에 자동으로 안 내려옴 → `apps_checkout` 다시 하면 내려옴.

### 이 세션의 권한
`apps_start`가 차단되어 **AI가 앱을 시작할 수 없음** → 사용자가 직접 시작해야 로그 확인 가능.
로컬 터미널(git/gh) 도구도 없음 → GitHub push는 사용자가 수동으로.

## 8. 미해결 / 다음 할 일

1. ~~의존성 설치~~ — **완료**(2026-11-12). gTTS·imageio-ffmpeg 설치 + requirements.txt 고정 + 변환 실측 검증
2. **D5 PWM 재생 실측 검증** — 아래 "위험" 표가 모두 미검증 (가장 큰 남은 위험)
3. **RC 저역통과 필터** — D5와 LM386 입력 사이에 필요 (하드웨어 작업)
4. 낡은 문서 갱신 — `README.md`, `weather.md`
5. 실측 보정 — `FSR_ON/OFF_THRESHOLD`, `DOOR_CLOSED_LEVEL` 극성
6. `pyserial` 의존성 제거 (더 이상 안 씀)

## 9. 위험 / 미검증 (정직하게)

| 항목 | 내용 |
| :--- | :--- |
| **RC 필터 없음** | 32kHz PWM을 LM386에 직결하면 사각파를 그대로 증폭한다. 저항+커패시터 필터가 필요. 없으면 소리는 나도 거칠다 |
| pinctrl 우회 | `analogWrite()` 선행 호출로 TIM1_CH4 먹싱을 얻는 방식은 **미검증**. 실패 시 `ERR,PWMINIT` 로그가 뜨고 소리가 안 남. **주의: 이 호출은 반드시 값 0으로** — 0이 아니면 오버레이 기본값 500Hz 사각파가 그대로 출력돼 노이즈로 들린다(2026-11-12 실제 관측) |
| **gTTS는 온라인 전용** | 인터넷이 끊기면 `음성 생성 실패(gTTS): Failed to connect`가 나고 음성이 아예 만들어지지 않는다. 보드 Wi-Fi가 자주 끊기므로 재현이 잦다. 오프라인이 필요하면 espeak-ng/piper 같은 로컬 TTS로 바꿔야 한다 |
| 8kHz 타이밍 | `k_cycle_get_32()` 스핀 대기로 페이싱. 재생 중(2~5초) Bridge/센서 스레드가 밀릴 수 있음 → `PCM_THREAD_PRIORITY` 조정 |
| Bridge 처리량 | 실측: 짧은 안내문 1.78초 = 14,208B = **128B 청크 111회 RPC**. 왕복 지연은 **미측정** — 느리면 `AUDIO_CHUNK_BYTES`를 올린다(최대 ~240이면 59회로 감소) |
| `vector<uint8_t>` 수신 | 스케치 핸들러가 msgpack bin을 `std::vector<uint8_t>`로 받는 패턴은 공식 예제에 없음(라이브러리 내부 `hci.h` 선례만). 컴파일/런타임에서 1차 검증 필요 |
| UNO Q RAM | 측정(2026-11-13): 버퍼 40,000일 때 스케치 `.bss` = **49,016바이트**. 8초(64,000)로 올리면 약 73,000바이트. 전체 SRAM은 링커 맵이 재배치 오브젝트라 **확정 못 함** — 더 늘릴 때 재확인 필요 |
| **긴 문장 초과** | 상황 2 문장("오늘 오후 N시경부터… 최고 강수확률은 N퍼센트입니다…")은 10초를 넘을 수 있어 8초 버퍼로도 부족할 수 있다. 근본 해결은 **스트리밍 재생**(받는 대로 재생)이고, 임시로는 `AUDIO_MAX_SECONDS`를 올리거나 `PCM_SAMPLE_RATE`를 4000으로 낮춰 데이터를 절반으로 줄이는 방법이 있다 |
