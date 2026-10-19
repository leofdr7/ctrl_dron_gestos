"""Receptor TCP de mesa para verificar el enlace sin hardware."""

import argparse
import json
import socket
import time

from .protocol import decode_command

WATCHDOG_SECONDS = 0.5


class BenchState:
    def __init__(self, estop: bool = False):
        self.state = "ESTOP" if estop else "NEUTRAL"
        self.last_seq = -1
        self.last_received = time.monotonic()
        self.estop = estop

    def accept(self, line: bytes, now: float) -> dict:
        try:
            seq, command = decode_command(line)
            if seq <= self.last_seq:
                raise ValueError("secuencia repetida")
            self.last_seq = seq
            self.last_received = now
            if command == "ESTOP":
                self.estop = True
            if self.estop:
                self.state = "ESTOP"
            else:
                self.state = command
            return {"ok": True, "seq": seq, "state": self.state, "actuation_enabled": False}
        except ValueError as exc:
            return {"ok": False, "seq": None, "state": self.state, "reason": str(exc), "actuation_enabled": False}

    def tick(self, now: float) -> None:
        if not self.estop and now - self.last_received > WATCHDOG_SECONDS:
            self.state = "NEUTRAL"


def handle_connection(conn: socket.socket, state: BenchState) -> None:
    conn.settimeout(0.1)
    buffer = bytearray()
    while True:
        try:
            chunk = conn.recv(256)
        except socket.timeout:
            state.tick(time.monotonic())
            continue
        if not chunk:
            state.state = "ESTOP" if state.estop else "NEUTRAL"
            print(f"Cliente desconectado; estado {state.state}")
            return
        buffer.extend(chunk)
        if len(buffer) > 1024:
            print("Mensaje excesivo; conexión cerrada")
            return
        while b"\n" in buffer:
            line, _, rest = buffer.partition(b"\n")
            buffer = bytearray(rest)
            response = state.accept(line, time.monotonic())
            print(f"{response['state']}: {response.get('reason', 'recibido')}")
            conn.sendall((json.dumps(response, separators=(",", ":")) + "\n").encode("utf-8"))


def main() -> None:
    parser = argparse.ArgumentParser(description="Simulador de recepción sin motores")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((args.host, args.port))
        server.listen(1)
        print(f"Simulador escuchando en {args.host}:{args.port}")
        estop_latched = False
        while True:
            conn, addr = server.accept()
            print(f"Cliente: {addr}")
            state = BenchState(estop_latched)
            with conn:
                try:
                    handle_connection(conn, state)
                except OSError as exc:
                    print(f"Enlace interrumpido: {exc}")
            estop_latched = state.estop


if __name__ == "__main__":
    main()
