import time

from arduino.app_utils import App, Bridge
import arduino_speaker
from arduino_speaker import refresh_weather, process_sensor_state
import csv_logger
import tts_player


WEATHER_REFRESH_INTERVAL_S = 600  # 임의의 값: 10분마다 날씨 재조회
CSV_LOG_INTERVAL_S = 0.1  # CSV 수집 간격

_last_weather_refresh = 0.0


def _on_state(values):
    """아두이노가 Bridge로 보낸 STATE(time_ms, fsr, door_open, umbrella_present)를 받아
    최종 판단까지 하고, 알림이면 즉시 재생을 지시한다.

    음성은 loop()가 미리 만들어 아두이노에 올려두므로 여기서는 notify 1회만 보낸다.
    (예전에는 여기서 생성·전송을 시작해 문이 닫힌 뒤에야 소리가 났다)"""
    _time_ms, fsr, door_open, umbrella_present = values

    alert_text = process_sensor_state(door_open, umbrella_present)
    should_play_sound = bool(alert_text)

    csv_logger.update_sensors(fsr, door_open, umbrella_present)
    csv_logger.update_decision(1 if arduino_speaker.rain_flag else 0, 1 if should_play_sound else 0)

    weather_text = "비 예보 있음" if arduino_speaker.rain_flag else "비 예보 없음"
    door_text = "열림" if door_open else "닫힘"
    umbrella_text = "있음" if umbrella_present else "없음"
    print(f"[센서] 날씨: {weather_text} | 문: {door_text} | 우산: {umbrella_text}")

    # 판단 결과 통보 (실제 재생은 loop()가 음성을 보낸 뒤 MCU가 시작한다)
    Bridge.notify("sound_decision", 1 if should_play_sound else 0)
    print(f"[Bridge 전송] {'소리 출력' if should_play_sound else '소리 출력 X'}")

    if alert_text:
        tts_player.request_play()  # 미리 올려둔 음성을 즉시 재생


def _on_mcu_log(message):
    """아두이노가 보낸 로그(DBG/ERR/OK 등)를 앱 로그에 그대로 찍는다.
    시리얼 모니터를 열지 않아도 MCU 쪽 진단을 볼 수 있게 하려는 것."""
    print(f"[MCU] {message}")


Bridge.provide("state", _on_state)      # 아두이노가 보내는 STATE를 받는 핸들러 등록
Bridge.provide("mcu_log", _on_mcu_log)  # 아두이노의 진단 로그


def loop():
    """날씨 갱신(주기) + 음성 미리 준비 + CSV 기록(0.1초)을 담당한다.
    센서 STATE와 재생 지시는 Bridge 콜백(_on_state)이 비동기로 처리한다."""
    global _last_weather_refresh

    now = time.monotonic()
    if now - _last_weather_refresh >= WEATHER_REFRESH_INTERVAL_S:
        refresh_weather()
        _last_weather_refresh = now

    # 알림이 울리기 전에 음성을 미리 만들어 아두이노에 올려둔다.
    # 수 초가 걸릴 수 있어 그 동안 CSV 간격이 늘어지지만, 문장이 바뀔 때만 1회 수행된다.
    if arduino_speaker.weather_text:
        tts_player.prepare_text(arduino_speaker.weather_text)

    csv_logger.log_row()
    time.sleep(CSV_LOG_INTERVAL_S)


App.run(user_loop=loop)
