"""Entrenamiento de YOLO con un dataset de gestos etiquetado por el usuario."""

import argparse
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description="Entrenar YOLO para los gestos definidos en el dataset")
    parser.add_argument("--data", type=Path, required=True, help="data.yaml del dataset real")
    parser.add_argument("--base-model", type=Path, required=True, help="pesos YOLO de detección de partida")
    parser.add_argument("--epochs", type=int, default=80)
    parser.add_argument("--imgsz", type=int, default=640)
    args = parser.parse_args()
    if not args.data.is_file() or not args.base_model.is_file():
        parser.error("Se necesitan un data.yaml real y pesos YOLO locales")
    if args.epochs < 1 or args.imgsz < 64:
        parser.error("--epochs y --imgsz deben ser positivos")

    from ultralytics import YOLO

    model = YOLO(str(args.base_model))
    model.train(data=str(args.data), epochs=args.epochs, imgsz=args.imgsz,
                project="runs/gestos", name="vant_ia_mk1")


if __name__ == "__main__":
    main()
