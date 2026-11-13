from tts_player import light_text


def run_my_task(input_text):
    """알림 문장을 음성 재생 모듈로 넘긴다. 반드시 user_loop에서 호출해야 한다
    (tts_player가 Bridge.call을 쓰므로 Bridge 핸들러 안에서는 불가)."""
    output_text = input_text.strip()

    if not output_text:
        return False

    return light_text(output_text)
