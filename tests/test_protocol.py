import unittest

from ctrl_dron.decision import GestureFilter
from ctrl_dron.protocol import decode_command, encode_command
from ctrl_dron.simulator import BenchState


class ProtocolTests(unittest.TestCase):
    def test_roundtrip(self):
        self.assertEqual(decode_command(encode_command(3, "LEFT")), (3, "LEFT"))

    def test_invalid_command(self):
        with self.assertRaises(ValueError):
            decode_command(b'{"v":1,"seq":2,"command":"THROTTLE_100"}')

    def test_repeated_sequence_and_watchdog(self):
        state = BenchState()
        self.assertTrue(state.accept(encode_command(1, "UP"), 0.0)["ok"])
        self.assertFalse(state.accept(encode_command(1, "LEFT"), 0.1)["ok"])
        state.tick(0.6)
        self.assertEqual(state.state, "NEUTRAL")

    def test_estop_latches(self):
        state = BenchState()
        state.accept(encode_command(1, "ESTOP"), 0.0)
        state.accept(encode_command(2, "UP"), 0.1)
        self.assertEqual(state.state, "ESTOP")
        next_connection = BenchState(state.estop)
        next_connection.accept(encode_command(1, "NEUTRAL"), 0.2)
        self.assertEqual(next_connection.state, "ESTOP")

    def test_gesture_must_be_stable_and_unique(self):
        filter_ = GestureFilter({"arriba": "UP"}, stable_frames=3)
        self.assertEqual(filter_.observe(["arriba"]), "NEUTRAL")
        self.assertEqual(filter_.observe(["arriba"]), "NEUTRAL")
        self.assertEqual(filter_.observe(["arriba"]), "UP")
        self.assertEqual(filter_.observe(["arriba", "arriba"]), "NEUTRAL")
        self.assertEqual(filter_.observe([]), "NEUTRAL")


if __name__ == "__main__":
    unittest.main()
