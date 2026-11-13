#include "audio_player.h"
#include "config.h"
#include "link.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>

static volatile bool playing_ = false;

// ── 부저 폴백용 상태 ──
static unsigned long beepTimer = 0;
static int           beepsDone = 0;
static bool          beepOn    = false;

// ── D5 PWM PCM 재생용 상태 ──
// D5의 PWM 스펙. analogWrite()는 오버레이의 PWM_HZ(500)을 쓰지만, 여기서는
// pwm_set_dt()로 반송파 주기를 직접 지정해 오디오용으로 쓴다.
static const struct pwm_dt_spec pcmPwm =
    PWM_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), PCM_PWM_DT_INDEX);

static const uint8_t* pcmData    = nullptr;
static int            pcmSize    = 0;
static volatile bool  pcmStopReq = false;
static bool           pcmReady   = false;  // PWM 장치 준비 성공 여부

K_THREAD_STACK_DEFINE(pcm_stack, PCM_THREAD_STACK_SIZE);
static struct k_thread pcm_thread_data;
// static을 붙이지 않는다 — 커널 오브젝트 매크로는 섹션 속성을 포함하므로
// 코어(Tone.cpp의 K_MUTEX_DEFINE)와 같은 방식으로 파일 스코프에 그대로 둔다
K_SEM_DEFINE(pcm_start_sem, 0, 1);

// ── 앰프 전원 제어 ──

// 앰프 전원을 켜거나 끈다 (배선 전에는 AMP_MUTE_ENABLED=false라 아무것도 하지 않는다)
static void ampPower(bool on) {
  if (!AMP_MUTE_ENABLED) return;
  digitalWrite(AMP_MUTE_PIN, on ? AMP_ON_LEVEL : !AMP_ON_LEVEL);
}

// 재생하지 않을 때 핀이 머무는 레벨.
//  - 앰프를 끌 수 있으면: 무음 수준(128). 커플링 커패시터가 미리 충전돼 있어 팝이 작다
//  - 앰프를 못 끄면: 0. DC를 계속 걸어두면 LM386이 포화되므로 LOW로 주차해야 한다
// 페이드의 시작·끝 지점도 이 레벨이어야 단차(팝)가 생기지 않는다
static inline int idleLevel() {
  return AMP_MUTE_ENABLED ? PCM_SILENCE_LEVEL : 0;
}

// 8bit 샘플을 PWM 듀티(ns)로 바꾼다.
// fadeNum/fadeDen이 0이면 유휴 레벨, 1이면 샘플 그대로 — 그 사이를 선형 보간한다.
// 유휴 레벨을 기준으로 보간하므로 페이드 시작·끝에서 단차가 생기지 않는다.
static inline uint32_t sampleToPulse(uint8_t sample, int fadeNum, int fadeDen) {
  const int32_t idle = idleLevel();

  int32_t level = (int32_t)sample;
  if (fadeNum < fadeDen) {
    level = idle + ((int32_t)sample - idle) * fadeNum / fadeDen;
  }

  if (level < 0)   level = 0;
  if (level > 255) level = 255;

  return (uint32_t)level * PCM_PWM_PERIOD_NS / 255u;
}

// 유휴 레벨로 핀을 유지한다 (페이드의 시작·끝 지점과 같은 값)
static void holdIdle() {
  pwm_set_dt(&pcmPwm, PCM_PWM_PERIOD_NS,
             (uint32_t)idleLevel() * PCM_PWM_PERIOD_NS / 255u);
}

// 샘플 펌프 스레드 — 세마포어를 받으면 앰프를 켜고, 8kHz로 PWM 듀티에 샘플을 흘려보낸다.
// loop()와 별도 스레드이므로 loop()를 블로킹하지 않는다 (프로젝트 규칙 준수).
static void pcmThreadFn(void*, void*, void*) {
  const uint32_t cyclesPerSample = sys_clock_hw_cycles_per_sec() / PCM_SAMPLE_RATE;

  while (true) {
    k_sem_take(&pcm_start_sem, K_FOREVER);

    const uint8_t* data = pcmData;
    const int      size = pcmSize;

    // ① 앰프를 켜고 안정될 때까지 기다린다.
    //    핀은 이미 무음 수준을 내보내고 있으므로 입력 단차로 인한 팝이 없다.
    //    (출력 커패시터 충전으로 인한 팝은 하드웨어 영역이라 여기서 못 막는다)
    ampPower(true);
    if (AMP_MUTE_ENABLED) k_msleep(AMP_WAKE_MS);

    // ② 페이드 구간 길이 — 음성이 아주 짧으면 절반까지만 쓴다
    int fadeSamples = (PCM_SAMPLE_RATE * PCM_FADE_MS) / 1000;
    if (fadeSamples > size / 2) fadeSamples = size / 2;
    if (fadeSamples < 1)        fadeSamples = 1;

    uint32_t next = k_cycle_get_32();

    for (int i = 0; i < size; i++) {
      if (pcmStopReq) break;

      // 시작은 서서히 키우고 끝은 서서히 줄여 "딱" 소리를 없앤다
      int fadeNum = fadeSamples, fadeDen = fadeSamples;
      if (i < fadeSamples)             fadeNum = i;
      else if (i >= size - fadeSamples) fadeNum = size - 1 - i;

      pwm_set_dt(&pcmPwm, PCM_PWM_PERIOD_NS, sampleToPulse(data[i], fadeNum, fadeDen));

      // pwm_set_dt에 걸린 시간을 보정해 샘플 간격을 유지한다
      // (k_busy_wait(125)만 쓰면 호출 시간이 더해져 음정이 낮아진다)
      next += cyclesPerSample;
      while ((int32_t)(k_cycle_get_32() - next) < 0) {
        /* 다음 샘플 시점까지 스핀 */
      }
    }

    // ③ 유휴 레벨로 되돌린다. 페이드아웃이 이미 이 값까지 내려왔으므로 단차가 없다
    holdIdle();

    if (AMP_MUTE_ENABLED) {
      k_msleep(AMP_TAIL_MS);   // 꼬리가 잘리지 않게 기다린 뒤
      ampPower(false);         // 앰프를 끈다 — 유휴 노이즈가 사라진다
    } else {
      // 앰프를 못 끄면 타이머가 핀을 계속 잡지 않도록 GPIO LOW로 주차한다.
      // 유휴 레벨이 0이라 여기서도 단차가 없다
      pinMode(PCM_PWM_PIN, OUTPUT);
      digitalWrite(PCM_PWM_PIN, LOW);
    }

    playing_ = false;
  }
}

// 핀·장치 초기화 (setup에서 1회 호출)
void playerBegin() {
  if (AMP_MUTE_ENABLED) {
    pinMode(AMP_MUTE_PIN, OUTPUT);
    ampPower(false);  // 부팅 직후에는 앰프를 꺼둔다
  }

  if (AUDIO_OUTPUT_MODE == AUDIO_MODE_BUZZER) {
    pinMode(BUZZER_PIN, OUTPUT);
    return;
  }

  // 코어의 pinctrl/채널 셋업 함수가 static이라 직접 못 부른다.
  // analogWrite()를 한 번 호출해 D5를 TIM1_CH4로 먹싱시킨 뒤, 이후에는
  // pwm_set_dt()로 반송파 주기를 직접 지정해 넘겨받는다.
  // 반드시 값 0으로 호출한다 — 0이 아니면 오버레이 기본값인 500Hz 사각파가 그대로 나간다.
  analogWrite(PCM_PWM_PIN, 0);

  pcmReady = pwm_is_ready_dt(&pcmPwm);
  if (!pcmReady) {
    analogWrite(PCM_PWM_PIN, 0);  // 실패해도 핀을 무음으로 확실히 두고 나간다
    if (ERR_REPORT_ENABLED) linkSendLine("ERR,PWMINIT");
    return;
  }

  if (AMP_MUTE_ENABLED) {
    // 앰프가 꺼져 있는 동안 핀을 무음 수준(DC 중앙)으로 유지한다.
    // 커플링 커패시터가 미리 충전돼 있어야 앰프를 켤 때 팝이 작다.
    holdIdle();
  } else {
    // 앰프를 끌 수 없으면 DC를 걸어두면 LM386이 포화되므로 GPIO LOW로 주차한다
    holdIdle();  // idleLevel()==0
    pinMode(PCM_PWM_PIN, OUTPUT);
    digitalWrite(PCM_PWM_PIN, LOW);
  }

  k_thread_create(&pcm_thread_data, pcm_stack, PCM_THREAD_STACK_SIZE,
                  pcmThreadFn, NULL, NULL, NULL,
                  PCM_THREAD_PRIORITY, 0, K_NO_WAIT);
  k_thread_name_set(&pcm_thread_data, "pcm");

  if (DEBUG_PRINT) linkSendLine("DBG,PWMREADY");
}

// 재생 시작 — BUZZER: 비프 패턴 / D5_PWM: 샘플 펌프 스레드를 깨운다
void playerStart(const uint8_t* data, int size) {
  if (AUDIO_OUTPUT_MODE == AUDIO_MODE_BUZZER) {
    playing_  = true;
    beepsDone = 0;
    beepOn    = true;
    beepTimer = millis();
    ampPower(true);
    tone(BUZZER_PIN, BEEP_FREQ);
    return;
  }

  if (!pcmReady || size <= 0) {
    if (ERR_REPORT_ENABLED) linkSendLine("ERR,PLAY");
    return;
  }

  // 유휴 중 GPIO로 주차해 둔 경우가 있으므로 PWM 출력으로 되돌리고 유휴 레벨부터 시작한다.
  // 값 0으로 호출해야 한다 — 0이 아니면 500Hz 사각파가 순간적으로 나간다.
  analogWrite(PCM_PWM_PIN, 0);
  holdIdle();

  pcmData    = data;
  pcmSize    = size;
  pcmStopReq = false;
  playing_   = true;
  k_sem_give(&pcm_start_sem);

  if (DEBUG_PRINT) {
    char dbg[40];
    snprintf(dbg, sizeof(dbg), "DBG,PLAY,%d", size);
    linkSendLine(dbg);
  }
}

// 매 loop()에서 호출 — 부저 모드의 패턴 진행만 담당.
// D5_PWM 모드는 전용 스레드가 재생하므로 여기서 할 일이 없다.
void playerUpdate(unsigned long now) {
  if (!playing_) return;
  if (AUDIO_OUTPUT_MODE != AUDIO_MODE_BUZZER) return;

  if (beepOn) {
    if (now - beepTimer >= BEEP_ON_MS) {
      noTone(BUZZER_PIN);
      beepOn = false;
      beepsDone++;
      beepTimer = now;
      if (beepsDone >= BEEP_COUNT) {
        playing_ = false;
        ampPower(false);
      }
    }
  } else {
    if (now - beepTimer >= BEEP_OFF_MS) {
      tone(BUZZER_PIN, BEEP_FREQ);
      beepOn    = true;
      beepTimer = now;
    }
  }
}

bool playerIsPlaying() { return playing_; }

// 재생 강제 중단
void playerStop() {
  if (AUDIO_OUTPUT_MODE == AUDIO_MODE_BUZZER) {
    noTone(BUZZER_PIN);
    playing_ = false;
    ampPower(false);
    return;
  }

  // 스레드가 다음 샘플에서 루프를 빠져나오고, 페이드·꼬리 처리 후 스스로 앰프를 끈다
  pcmStopReq = true;
}
