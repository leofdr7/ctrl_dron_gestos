"""ESP32-S3 / MicroPython: receptor de órdenes para pruebas de mesa.

No importa machine.PWM y no activa motores.
"""

import network
import socket
import time
import ujson

from config import WIFI_SSID, WIFI_PASSWORD, PORT

COMMANDS = ("NEUTRAL", "UP", "DOWN", "LEFT", "RIGHT", "FORWARD", "BACK",
            "ARM", "TAKEOFF", "LAND", "DISARM", "ESTOP")
WATCHDOG_MS = 500


def connect_wifi():
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    wlan.connect(WIFI_SSID, WIFI_PASSWORD)
    deadline = time.ticks_add(time.ticks_ms(), 15000)
    while not wlan.isconnected():
        if time.ticks_diff(deadline, time.ticks_ms()) <= 0:
            raise RuntimeError("Wi-Fi no disponible")
        time.sleep_ms(100)
    print("IP ESP32-S3:", wlan.ifconfig()[0])
    return wlan


def reply(conn, ok, seq, state, reason=None):
    data = {"ok": ok, "seq": seq, "state": state, "actuation_enabled": False}
    if reason is not None:
        data["reason"] = reason
    conn.send((ujson.dumps(data) + "\n").encode())


def serve_client(conn, server_state):
    conn.settimeout(0.1)
    buffer = b""
    state = "ESTOP" if server_state["estop"] else "NEUTRAL"
    estop = server_state["estop"]
    last_seq = -1
    last_rx = time.ticks_ms()
    try:
        while True:
            try:
                chunk = conn.recv(256)
            except OSError as exc:
                if exc.args and exc.args[0] in (11, 110, 116):
                    chunk = None  # EAGAIN/ETIMEDOUT; no dato nuevo
                else:
                    raise
            if chunk == b"":
                print("Cliente desconectado;", "ESTOP" if estop else "NEUTRAL")
                return
            if chunk:
                buffer += chunk
                if len(buffer) > 1024:
                    print("Mensaje demasiado largo; cierre")
                    return
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    try:
                        if len(line) > 256:
                            raise ValueError("mensaje largo")
                        data = ujson.loads(line.decode())
                        seq = data.get("seq")
                        command = data.get("command")
                        if data.get("v") != 1 or type(seq) is not int or seq <= last_seq or command not in COMMANDS:
                            raise ValueError("orden o secuencia inválida")
                        last_seq = seq
                        last_rx = time.ticks_ms()
                        if command == "ESTOP":
                            estop = True
                            server_state["estop"] = True
                        state = "ESTOP" if estop else command
                        print("Intención:", state, "(sin actuadores)")
                        reply(conn, True, seq, state)
                    except (ValueError, TypeError, AttributeError):
                        reply(conn, False, None, state, "mensaje inválido")
            if not estop and time.ticks_diff(time.ticks_ms(), last_rx) > WATCHDOG_MS:
                if state != "NEUTRAL":
                    print("Watchdog: NEUTRAL")
                state = "NEUTRAL"
    finally:
        conn.close()


def main():
    connect_wifi()
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("0.0.0.0", PORT))
    server.listen(1)
    print("Receptor de mesa escuchando:", PORT)
    server_state = {"estop": False}
    while True:
        conn, addr = server.accept()
        print("Cliente:", addr)
        try:
            serve_client(conn, server_state)
        except OSError as exc:
            print("Enlace interrumpido:", exc)


main()
