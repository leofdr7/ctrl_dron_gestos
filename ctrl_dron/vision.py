"""Cámara, inferencia YOLO y envío de intenciones al receptor de mesa."""

import argparse
import json
from pathlib import Path

from .decision import GestureFilter
from .link import CommandPublisher
from .protocol import COMMANDS


def load_mapping(path: Path) -> dict[str, str]:
    mapping = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(mapping, dict) or not mapping:
        raise ValueError("El mapa de gestos debe ser un objeto JSON no vacío")
    allowed = COMMANDS - {"ESTOP", "NEUTRAL", "ARM", "TAKEOFF", "DISARM"}
    if any(not isinstance(k, str) or not k or v not in allowed for k, v in mapping.items()):
        raise ValueError("El mapa contiene una etiqueta u orden inválida")
    return mapping


def camera_source(value: str):
    return int(value) if value.isdecimal() else value


def main() -> None:
    parser = argparse.ArgumentParser(description="YOLO + OpenCV para gestos, solo enlace de mesa")
    parser.add_argument("--model", type=Path, required=True, help="Pesos YOLO entrenados para los gestos")
    parser.add_argument("--mapping", type=Path, default=Path(__file__).resolve().parents[1] / "config/gestures.json")
    parser.add_argument("--camera", default="0", help="Índice de cámara, ruta de video o URL")
    parser.add_argument("--host", help="IP del simulador o ESP32-S3; omitido = solo visualización")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--confidence", type=float, default=0.75)
    parser.add_argument("--stable-frames", type=int, default=5)
    parser.add_argument("--no-display", action="store_true")
    args = parser.parse_args()
    if not args.model.is_file():
        parser.error("No existe el modelo. Se necesitan pesos entrenados para los gestos de config/gestures.json")
    if not 0 < args.confidence <= 1:
        parser.error("--confidence debe estar entre 0 y 1")

    import cv2
    from ultralytics import YOLO

    mapping = load_mapping(args.mapping)
    model = YOLO(str(args.model))
    model_labels = set(model.names.values()) if isinstance(model.names, dict) else set(model.names)
    if not model_labels.intersection(mapping):
        raise ValueError("El modelo no contiene ninguna clase de config/gestures.json; se necesita un YOLO entrenado para estos gestos")
    filter_ = GestureFilter(mapping, args.stable_frames)
    cap = cv2.VideoCapture(camera_source(args.camera))
    if not cap.isOpened():
        raise RuntimeError(f"No se pudo abrir la cámara: {args.camera}")
    publisher = None
    last_shown = None
    try:
        if args.host:
            publisher = CommandPublisher(args.host, args.port)
        while True:
            if publisher:
                publisher.check()
            ok, frame = cap.read()
            if not ok:
                print("Cámara sin cuadros; se cierra el enlace")
                break
            result = model.predict(frame, conf=args.confidence, verbose=False)[0]
            classes = result.boxes.cls.int().tolist() if result.boxes is not None else []
            labels = [result.names[int(index)] for index in classes]
            command = filter_.observe(labels)
            if command != last_shown:
                print(f"Gesto → {command}")
                last_shown = command
            if publisher:
                publisher.update_vision(command)
            if not args.no_display:
                annotated = result.plot()
                cv2.putText(annotated, f"Orden: {command}", (15, 30),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 255), 2)
                cv2.imshow("VANT-IA-MK1", annotated)
                key = cv2.waitKey(1) & 0xFF
                manual = {ord("a"): "ARM", ord("t"): "TAKEOFF", ord("d"): "DISARM"}.get(key)
                if manual:
                    if publisher:
                        publisher.manual(manual, wait=True)
                    print(f"Orden manual: {manual}" if publisher else f"{manual} (sin enlace)")
                if key == ord("e"):
                    if publisher:
                        publisher.manual("ESTOP", wait=True)
                    print("ESTOP solicitado")
                    break
                if key == ord("q"):
                    break
    except KeyboardInterrupt:
        pass
    finally:
        if publisher:
            publisher.close()
        cap.release()
        if not args.no_display:
            cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
