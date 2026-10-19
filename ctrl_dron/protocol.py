"""Contrato de intenciones entre la computadora y el ESP32-S3."""

import json

COMMANDS = frozenset({
    "NEUTRAL", "UP", "DOWN", "LEFT", "RIGHT", "FORWARD", "BACK",
    "ARM", "TAKEOFF", "LAND", "DISARM", "ESTOP"
})


def encode_command(seq: int, command: str) -> bytes:
    if not isinstance(seq, int) or seq < 0:
        raise ValueError("seq debe ser un entero no negativo")
    if command not in COMMANDS:
        raise ValueError("orden desconocida")
    return (json.dumps({"v": 1, "seq": seq, "command": command}, separators=(",", ":")) + "\n").encode("utf-8")


def decode_command(line: bytes) -> tuple[int, str]:
    if len(line) > 256:
        raise ValueError("mensaje demasiado largo")
    try:
        data = json.loads(line)
    except (ValueError, UnicodeDecodeError) as exc:
        raise ValueError("JSON inválido") from exc
    if not isinstance(data, dict) or data.get("v") != 1:
        raise ValueError("versión inválida")
    seq = data.get("seq")
    command = data.get("command")
    if type(seq) is not int or seq < 0 or command not in COMMANDS:
        raise ValueError("orden o secuencia inválida")
    return seq, command
