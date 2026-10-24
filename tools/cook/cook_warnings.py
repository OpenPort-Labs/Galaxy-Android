"""Conversion notes that belong to one worker thread.

The Android cooker runs conversions in a thread pool. A plain module-level
list would be cleared and appended by several files at once, losing notes and
attributing them to the wrong file, so each thread gets its own list.
"""
import threading


class ThreadWarnings:
    def __init__(self):
        self._local = threading.local()

    def _items(self):
        items = getattr(self._local, 'items', None)
        if items is None:
            items = self._local.items = []
        return items

    def append(self, message):
        self._items().append(message)

    def __delitem__(self, key):
        del self._items()[key]

    def __iter__(self):
        return iter(self._items())

    def __len__(self):
        return len(self._items())

    def __add__(self, other):
        return self._items() + list(other)

    def __radd__(self, other):
        return list(other) + self._items()
