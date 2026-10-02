"""Functional integration checks; timing certification stays in check_stream."""
import math

ERROR_COUNTERS = ('underruns', 'invalid_samples', 'recording_dropped', 'stream_dropped',
                  'audio_timestamp_discontinuities', 'audio_invalid_timestamps',
                  'audio_callback_overruns', 'audio_callback_errors', 'audio_device_overloads')


def require_functional_exit(returncode, status, allow_scheduler_delays=False):
    def counter(key):
        try:
            value = float(status[key])
        except (KeyError, TypeError, ValueError):
            raise RuntimeError(f'Missing or invalid host counter: {key}') from None
        if not math.isfinite(value) or value < 0 or value != int(value):
            raise RuntimeError(f'Missing or invalid host counter: {key}')
        return int(value)

    for key in ERROR_COUNTERS:
        if counter(key):
            raise RuntimeError(f'Functional host error: {key}={status[key]}')
    misses = counter('sink_deadline_misses')
    if returncode == 0 and misses == 0:
        return 0
    if allow_scheduler_delays and returncode == 2 and misses > 0:
        return misses
    raise RuntimeError(f'Host exited {returncode} with {misses} software-sink deadline misses')
