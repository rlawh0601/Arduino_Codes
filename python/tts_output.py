#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
[파일 1] weather_fetcher.py
- 공공데이터포털 기상청 단기예보 API 호출
- 우산 챙김 판단에 꼭 필요한 정보만 분석하여 간결한 TTS 음성 문자열 반환
"""

import datetime
import json
import urllib.request
import urllib.parse

# ── 테스트 스위치 ─────────────────────────────────────────────────────────────
# 1로 바꾸면 API를 건너뛰고 "지금 비가 오는 중"으로 처리한다 (인터넷·API 키 없어도 됨).
# 실제 운용 시에는 반드시 0으로 되돌릴 것.
# 주의: 이것만으로 소리가 나지는 않는다. 나머지 두 조건은 물리적으로 만들어야 한다
#       ① 자기센서를 닫힘 → 열림으로 바꾸기  ② FSR을 눌러 우산 있음 상태 만들기
FORCE_RAIN_FOR_TEST = 0

def _get_base_datetime():
    """기상청 단기예보 발표 시각(02, 05, 08, 11, 14, 17, 20, 23시) 계산"""
    now = datetime.datetime.now()
    cur_mins = now.hour * 60 + now.minute
    slots = [
        ("0200", 2 * 60 + 10),
        ("0500", 5 * 60 + 10),
        ("0800", 8 * 60 + 10),
        ("1100", 11 * 60 + 10),
        ("1400", 14 * 60 + 10),
        ("1700", 17 * 60 + 10),
        ("2000", 20 * 60 + 10),
        ("2300", 23 * 60 + 10)
    ]
    for time_str, min_val in reversed(slots):
        if cur_mins >= min_val:
            return now.strftime("%Y%m%d"), time_str
    # 새벽 2시 10분 이전이면 전날 23시 자료 사용
    yesterday = now - datetime.timedelta(days=1)
    return yesterday.strftime("%Y%m%d"), "2300"


def write_text(service_key: str, nx: int = 60, ny: int = 127):
    """
    우산을 챙겨야 하는지 판단하여 안내 텍스트와 비 예보 여부를 반환하는 함수
    :param service_key: 공공데이터포털 일반 인증키 (Decoding)
    :param nx: 기상청 X 격자 (기본값: 서울 60)
    :param ny: 기상청 Y 격자 (기본값: 서울 127)
    :return: (안내 문자열, 비 예보 여부(bool)) 튜플.
             안내할 내용이 없으면(비 예보 없음 / API 실패) 안내 문자열은 빈 문자열이다.
    """
    # 테스트용 강제 비 — API 호출 전에 빠져나간다
    if FORCE_RAIN_FOR_TEST:
        print("[테스트] FORCE_RAIN_FOR_TEST=1 — 비 오는 상황으로 강제합니다")
        return "비가 올 예정입니다. 우산을 챙겨가세요.", True

    base_date, base_time = _get_base_datetime()
    url = "http://apis.data.go.kr/1360000/VilageFcstInfoService_2.0/getVilageFcst"

    params = {
        "serviceKey": urllib.parse.unquote(service_key),
        "pageNo": "1",
        "numOfRows": "1000",
        "dataType": "JSON",
        "base_date": base_date,
        "base_time": base_time,
        "nx": str(nx),
        "ny": str(ny)
    }

    req_url = f"{url}?{urllib.parse.urlencode(params)}"
    req = urllib.request.Request(req_url, headers={"User-Agent": "WeatherApp/1.0"})

    try:
        with urllib.request.urlopen(req, timeout=10) as res:
            data = json.loads(res.read().decode('utf-8'))
        items = data["response"]["body"]["items"]["item"]
    except Exception as e:
        # 안내할 내용이 없으므로 빈 문장을 돌려준다 (음성 출력 안 함). 원인은 로그로만 남긴다
        print(f"날씨 정보를 불러오지 못했습니다. 오류: {e}")
        return "", False

    # 오늘 남은 시간대의 비 예보 분석
    now = datetime.datetime.now()
    today_str = now.strftime("%Y%m%d")
    cur_hour = now.hour

    is_raining_now = False
    rain_hours = []
    max_pop = 0

    for item in items:
        if item.get("fcstDate") != today_str:
            continue
        try:
            fcst_hour = int(item.get("fcstTime")[:2])
        except (ValueError, TypeError):
            continue

        category = item.get("category")
        val = item.get("fcstValue")

        # 1. 강수형태 (PTY: 1 비, 2 비/눈, 4 소나기)
        if category == "PTY":
            pty = int(val)
            if fcst_hour == cur_hour and pty in [1, 2, 4]:
                is_raining_now = True
            if fcst_hour >= cur_hour and pty in [1, 2, 4]:
                if fcst_hour not in rain_hours:
                    rain_hours.append(fcst_hour)

        # 2. 강수확률 (POP)
        elif category == "POP":
            pop = int(val)
            if fcst_hour >= cur_hour:
                if pop > max_pop:
                    max_pop = pop
                # 비 예보 코드가 없더라도 확률 60% 이상이면 포함
                if pop >= 60 and fcst_hour not in rain_hours:
                    rain_hours.append(fcst_hour)

    rain_hours.sort()
    rain = is_raining_now or bool(rain_hours)  # 비 예보 여부: 현재 비 또는 오늘 중 예보 있음

    # --- 우산 안내 텍스트 생성 ---
    # 상황 1: 지금 당장 비가 오는 경우
    if is_raining_now:
        return "현재 바깥에 비가 내리고 있습니다. 외출하실 때 우산을 꼭 챙기세요.", rain

    # 상황 2: 오늘 남은 시간 중 비 예보가 있는 경우
    if rain_hours:
        first_h = rain_hours[0]
        time_text = f"오후 {first_h - 12}시" if first_h >= 12 else f"오전 {first_h}시"
        return f"현재는 비가 오지 않지만, 오늘 {time_text}경부터 비 예보가 있습니다. 최고 강수확률은 {max_pop}퍼센트입니다. 우산을 챙기시기 바랍니다.", rain

    # 상황 3: 오늘 하루 동안 비 예보가 없는 경우 — 안내할 내용이 없으므로 빈 문장 (음성 출력 안 함)
    return "", rain


# 단독 테스트용
if __name__ == "__main__":
    test_key = "38425da5bc3a0f1551dd6789eba56932b5f1570aaa1824d6debb596f77e980ea"
    print("생성된 문장:", write_text(test_key))
