import unittest

from functional_host import ERROR_COUNTERS, require_functional_exit


class FunctionalExitTests(unittest.TestCase):
    def test_only_reported_timer_misses_are_allowed_explicitly(self):
        healthy = dict.fromkeys(ERROR_COUNTERS + ('sink_deadline_misses',), 0)
        self.assertEqual(require_functional_exit(0, healthy), 0)
        delayed = healthy | {'sink_deadline_misses': 203}
        with self.assertRaises(RuntimeError):
            require_functional_exit(2, delayed)
        self.assertEqual(require_functional_exit(2, delayed, True), 203)
        for code, status in ((1, delayed), (-9, delayed), (2, healthy), (0, delayed)):
            with self.subTest(code=code, status=status), self.assertRaises(RuntimeError):
                require_functional_exit(code, status, True)
        for key in ERROR_COUNTERS:
            for value in (1, -1, float('nan'), .5):
                with self.subTest(key=key, value=value), self.assertRaises(RuntimeError):
                    require_functional_exit(2, delayed | {key: value}, True)
            missing = delayed.copy()
            del missing[key]
            with self.subTest(missing=key), self.assertRaises(RuntimeError):
                require_functional_exit(2, missing, True)


if __name__ == '__main__':
    unittest.main()
