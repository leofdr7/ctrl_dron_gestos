"""Filtro temporal conservador para clases de gestos de un YOLO propio."""


class GestureFilter:
    def __init__(self, mapping: dict[str, str], stable_frames: int = 5):
        if stable_frames < 1:
            raise ValueError("stable_frames debe ser positivo")
        self.mapping = mapping
        self.stable_frames = stable_frames
        self.candidate = None
        self.count = 0

    def observe(self, labels: list[str]) -> str:
        """Exige una sola detección conocida y N cuadros seguidos para mover."""
        known = [label for label in labels if label in self.mapping]
        if len(known) != 1:
            self.candidate = None
            self.count = 0
            return "NEUTRAL"
        label = known[0]
        if label == self.candidate:
            self.count += 1
        else:
            self.candidate = label
            self.count = 1
        if self.count < self.stable_frames:
            return "NEUTRAL"
        return self.mapping[label]
