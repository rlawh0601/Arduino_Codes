#include "audio_rx.h"
#include "config.h"
#include "link.h"
#include "Arduino_RouterBridge.h"
#include <vector>
#include <zephyr/kernel.h>

// AudioBuffer 추상화 — 스트리밍 방식으로 교체 시 이 블록만 수정
static uint8_t  audioBuf[AUDIO_MAX_BYTES];
static int      audioBodySize = 0;   // audio_begin이 선언한 전체 크기
static int      audioRecvd    = 0;   // 지금까지 실제로 받은 양
static bool     audioValid    = false; // commit까지 끝나 재생 가능한 음성이 들어있는지

static AudioRxState  rxState     = AUDIO_IDLE;
static unsigned long lastChunkMs = 0;

// 수신 핸들러는 Bridge 디스패치 스레드에서, 상태 조회는 loop() 스레드에서 호출된다.
// 두 스레드가 같은 상태를 건드리므로 뮤텍스로 보호한다.
K_MUTEX_DEFINE(audio_rx_mutex);

// 내부용 — 버퍼 내용까지 통째로 버린다 (새 전송 시작 / 오류). 뮤텍스를 잡은 상태에서만 호출
static void clearLocked() {
  rxState       = AUDIO_IDLE;
  audioBodySize = 0;
  audioRecvd    = 0;
  audioValid    = false;
}

// Python이 전송 시작을 알린다. 반환 0=수락, 음수=거절
static int onAudioBegin(int totalSize) {
  k_mutex_lock(&audio_rx_mutex, K_FOREVER);

  if (totalSize <= 0 || totalSize > AUDIO_MAX_BYTES) {
    clearLocked();
    k_mutex_unlock(&audio_rx_mutex);
    if (ERR_REPORT_ENABLED) linkSendLine("ERR,SIZE");
    return -1;
  }

  // 재생 중이면 새 전송을 받지 않는다 (재생 중인 버퍼를 덮어쓰면 안 됨)
  if (rxState == AUDIO_RECEIVED) {
    k_mutex_unlock(&audio_rx_mutex);
    if (ERR_REPORT_ENABLED) linkSendLine("ERR,BUSY");
    return -2;
  }

  // 이전에 올려둔 음성은 버리고 새로 받는다
  clearLocked();
  audioBodySize = totalSize;
  rxState       = AUDIO_RECEIVING;
  lastChunkMs   = millis();

  k_mutex_unlock(&audio_rx_mutex);

  if (DEBUG_PRINT) {
    char dbg[40];
    snprintf(dbg, sizeof(dbg), "DBG,BEGIN,%d", totalSize);
    linkSendLine(dbg);
  }
  return 0;
}

// Python이 PCM 조각을 보낸다. 반환=누적 수신량, 음수=오류
static int onAudioChunk(std::vector<uint8_t> chunk) {
  k_mutex_lock(&audio_rx_mutex, K_FOREVER);

  if (rxState != AUDIO_RECEIVING) {
    k_mutex_unlock(&audio_rx_mutex);
    if (ERR_REPORT_ENABLED) linkSendLine("ERR,STATE");
    return -1;
  }

  // 선언한 크기를 넘으면 버린다 (버퍼 오버런 방지)
  if (audioRecvd + (int)chunk.size() > audioBodySize) {
    clearLocked();
    k_mutex_unlock(&audio_rx_mutex);
    if (ERR_REPORT_ENABLED) linkSendLine("ERR,SIZE");
    return -2;
  }

  for (size_t i = 0; i < chunk.size(); i++) {
    audioBuf[audioRecvd++] = chunk[i];
  }
  lastChunkMs = millis();

  const int received = audioRecvd;
  k_mutex_unlock(&audio_rx_mutex);
  return received;
}

// Python이 전송 완료를 알린다 — 버퍼만 유효 표시하고 재생은 하지 않는다.
// 반환 0=보관 완료, 음수=오류
static int onAudioCommit() {
  k_mutex_lock(&audio_rx_mutex, K_FOREVER);

  if (rxState != AUDIO_RECEIVING || audioRecvd != audioBodySize) {
    const int got = audioRecvd, want = audioBodySize;
    clearLocked();
    k_mutex_unlock(&audio_rx_mutex);
    if (ERR_REPORT_ENABLED) linkSendLine("ERR,INCOMPLETE");
    if (DEBUG_PRINT) {
      char dbg[48];
      snprintf(dbg, sizeof(dbg), "DBG,INCOMPLETE,%d/%d", got, want);
      linkSendLine(dbg);
    }
    return -1;
  }

  audioValid = true;
  rxState    = AUDIO_IDLE;  // 재생 대기 — sketch는 아직 아무것도 하지 않는다
  const int size = audioBodySize;
  k_mutex_unlock(&audio_rx_mutex);

  linkSendLine("OK");
  if (DEBUG_PRINT) {
    char dbg[40];
    snprintf(dbg, sizeof(dbg), "DBG,COMMIT,%d", size);
    linkSendLine(dbg);
  }
  return 0;
}

// Python이 재생을 지시한다 (알림 순간). 미리 올려둔 버퍼를 그대로 쓰므로 즉시 시작된다.
// Python 쪽 Bridge 핸들러에서는 call을 못 쓰므로 notify로 들어온다 — 반환값은 무시된다.
static int onAudioPlay() {
  k_mutex_lock(&audio_rx_mutex, K_FOREVER);

  if (!audioValid) {
    k_mutex_unlock(&audio_rx_mutex);
    if (ERR_REPORT_ENABLED) linkSendLine("ERR,NOAUDIO");
    return -1;
  }
  if (rxState != AUDIO_IDLE) {   // 수신 중이거나 이미 재생 중
    k_mutex_unlock(&audio_rx_mutex);
    if (ERR_REPORT_ENABLED) linkSendLine("ERR,BUSY");
    return -2;
  }

  // sketch.ino의 상태머신이 RECEIVED를 보고 재생을 시작한다
  rxState = AUDIO_RECEIVED;
  k_mutex_unlock(&audio_rx_mutex);

  if (DEBUG_PRINT) linkSendLine("DBG,PLAYREQ");
  return 0;
}

// 재생이 끝난 뒤 sketch가 호출 — IDLE로 돌아가되 버퍼는 남겨 다시 재생할 수 있게 한다
void audioRxReset() {
  k_mutex_lock(&audio_rx_mutex, K_FOREVER);
  rxState = AUDIO_IDLE;
  k_mutex_unlock(&audio_rx_mutex);
}

// 초기화 — Python이 호출할 수신 메서드 4개를 Bridge에 등록 (setup에서 linkBegin 이후 호출)
void audioRxBegin() {
  k_mutex_lock(&audio_rx_mutex, K_FOREVER);
  clearLocked();
  k_mutex_unlock(&audio_rx_mutex);

  Bridge.provide("audio_begin",  onAudioBegin);
  Bridge.provide("audio_chunk",  onAudioChunk);
  Bridge.provide("audio_commit", onAudioCommit);
  Bridge.provide("audio_play",   onAudioPlay);
}

// 매 loop()에서 호출 — 수신이 중간에 끊겼는지만 감시한다
void audioRxUpdate(unsigned long now) {
  k_mutex_lock(&audio_rx_mutex, K_FOREVER);

  const bool stalled = (rxState == AUDIO_RECEIVING) &&
                       (now - lastChunkMs > AUDIO_CHUNK_TIMEOUT_MS);
  if (stalled) clearLocked();

  k_mutex_unlock(&audio_rx_mutex);

  if (stalled && ERR_REPORT_ENABLED) linkSendLine("ERR,TIMEOUT");
}

bool audioRxIsComplete() {
  k_mutex_lock(&audio_rx_mutex, K_FOREVER);
  const bool done = (rxState == AUDIO_RECEIVED);
  k_mutex_unlock(&audio_rx_mutex);
  return done;
}

const uint8_t* audioRxGetData() { return audioBuf; }

int audioRxGetSize() {
  k_mutex_lock(&audio_rx_mutex, K_FOREVER);
  const int size = audioBodySize;
  k_mutex_unlock(&audio_rx_mutex);
  return size;
}

AudioRxState audioRxGetState() {
  k_mutex_lock(&audio_rx_mutex, K_FOREVER);
  const AudioRxState st = rxState;
  k_mutex_unlock(&audio_rx_mutex);
  return st;
}
