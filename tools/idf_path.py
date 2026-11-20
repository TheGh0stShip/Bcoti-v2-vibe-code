"""Resolve the activated ESP-IDF installation for offline inspection/tests."""
import os
from pathlib import Path


def idf_path():
    value = os.environ.get('IDF_PATH')
    if not value:
        raise SystemExit('Activate ESP-IDF v5.3.1 (IDF_PATH is required). See docs/BUILD.md.')
    path = Path(value).resolve()
    if not (path / 'tools/idf.py').is_file():
        raise SystemExit('IDF_PATH does not contain an ESP-IDF checkout.')
    return path
