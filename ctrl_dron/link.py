"""Cliente TCP síncrono para pruebas de mesa."""

import json
import socket
import threading
import time
from collections import deque

from .protocol import encode_command


class CommandLink:
    def __init__(self, host: str, port: int = 8765):
        self.sock = socket.create_connection((host, port), timeout=2.0)
        self.sock.settimeout(0.4)
        self.stream = self.sock.makefile("rb")
        self.seq = 0

    def send(self, command: str) -> dict:
        self.seq += 1
        self.sock.sendall(encode_command(self.seq, command))
        line = self.stream.readline(257)
        if not line or len(line) > 256:
            raise ConnectionError("respuesta ausente o demasiado larga")
        response = json.loads(line)
        if not response.get("ok") or response.get("seq") != self.seq:
            raise ConnectionError(f"orden rechazada: {response}")
        return response

    def close(self) -> None:
        self.stream.close()
        self.sock.close()


class CommandPublisher:
    """Envía a 10 Hz y deja expirar el enlace si se congela la visión."""

    def __init__(self, host: str, port: int = 8765, interval: float = 0.1,
                 vision_timeout: float = 0.3):
        self._link = CommandLink(host, port)
        self._interval = interval
        self._vision_timeout = vision_timeout
        self._lock = threading.Lock()
        self._wake = threading.Event()
        self._stop = threading.Event()
        self._manual = deque()
        self._latest = "NEUTRAL"
        self._last_vision = 0.0
        self._error = None
        self._thread = threading.Thread(target=self._run, name="command-publisher", daemon=True)
        self._thread.start()

    def update_vision(self, command: str) -> None:
        with self._lock:
            self._latest = command
            self._last_vision = time.monotonic()

    def manual(self, command: str, wait: bool = False) -> None:
        sent = threading.Event()
        with self._lock:
            if command == "ESTOP":
                self._manual.appendleft((command, sent))
            else:
                self._manual.append((command, sent))
        self._wake.set()
        if wait:
            if not sent.wait(timeout=1.5):
                self.check()
                raise TimeoutError(f"No se confirmó {command}")
            self.check()

    def check(self) -> None:
        if self._error is not None:
            raise ConnectionError("Falló el enlace con el ESP32-S3") from self._error

    def _run(self) -> None:
        try:
            while not self._stop.is_set():
                confirmation = None
                with self._lock:
                    if self._manual:
                        command, confirmation = self._manual.popleft()
                    elif time.monotonic() - self._last_vision <= self._vision_timeout:
                        command = self._latest
                    else:
                        command = None
                if command is not None:
                    self._link.send(command)
                    if confirmation:
                        confirmation.set()
                self._wake.wait(self._interval)
                self._wake.clear()
        except Exception as exc:
            self._error = exc

    def close(self) -> None:
        self._stop.set()
        self._wake.set()
        self._thread.join(timeout=2.0)
        if self._thread.is_alive():
            self._link.sock.close()
            self._thread.join(timeout=1.0)
        if not self._thread.is_alive():
            try:
                self._link.send("NEUTRAL")
            except (OSError, ConnectionError, ValueError):
                pass
            self._link.close()
