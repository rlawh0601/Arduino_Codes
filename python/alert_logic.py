previous_door_open = None


def check_alert(door_open, umbrella_present, rain, text):
    """문이 닫힘→열림으로 바뀌는 순간 + 비 예보 + 우산 보유 여부로 최종 알림 여부를 판단한다.

    반환: 알릴 문장(str) 또는 None.
    실제 음성 출력은 이 함수가 하지 않는다 — Bridge 핸들러 안에서는 Bridge.call을 쓸 수 없으므로,
    호출자(main.py)가 이 문장을 받아 user_loop에서 전송한다.
    """
    global previous_door_open

    current_door_open = bool(door_open)

    # 첫 호출에서는 현재 문 상태만 저장
    if previous_door_open is None:
        previous_door_open = current_door_open
        return None

    # 닫힘 → 열림 전환 확인
    just_opened = (
        previous_door_open is False
        and current_door_open is True
    )

    previous_door_open = current_door_open

    if not just_opened:
        return None

    if rain != 1 or umbrella_present != 1:
        return None

    text = text.strip()

    if not text:
        return None

    print(f"[최종 판단] 우산을 챙기세요: {text}")
    return text
