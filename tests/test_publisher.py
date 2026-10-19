import time
import unittest
from unittest.mock import patch

from ctrl_dron.link import CommandPublisher


class FakeLink:
    def __init__(self, host, port):
        self.sent = []

    def send(self, command):
        self.sent.append(command)
        return {"ok": True}

    def close(self):
        pass


def wait_for(predicate, timeout=1.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if predicate():
            return True
        time.sleep(0.005)
    return False


class PublisherTests(unittest.TestCase):
    def test_stale_vision_stops_heartbeat_and_manual_is_confirmed(self):
        with patch("ctrl_dron.link.CommandLink", FakeLink):
            publisher = CommandPublisher("unused", interval=0.01, vision_timeout=0.08)
            try:
                publisher.update_vision("UP")
                self.assertTrue(wait_for(lambda: "UP" in publisher._link.sent))
                publisher.update_vision("NEUTRAL")
                self.assertTrue(wait_for(lambda: "NEUTRAL" in publisher._link.sent))
                time.sleep(0.11)
                count = len(publisher._link.sent)
                time.sleep(0.04)
                self.assertEqual(len(publisher._link.sent), count)
                publisher.manual("ESTOP", wait=True)
                self.assertIn("ESTOP", publisher._link.sent)
                publisher.check()
            finally:
                publisher.close()


if __name__ == "__main__":
    unittest.main()
