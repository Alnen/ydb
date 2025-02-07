# Generated by devtools/yamaker (pypi).

PY3_LIBRARY()

VERSION(1.30.4)

LICENSE(BSD-3-Clause)

PEERDIR(
    contrib/python/python-dateutil
    contrib/python/scramp
)

NO_LINT()

PY_SRCS(
    TOP_LEVEL
    pg8000/__init__.py
    pg8000/converters.py
    pg8000/core.py
    pg8000/dbapi.py
    pg8000/exceptions.py
    pg8000/legacy.py
    pg8000/native.py
    pg8000/types.py
)

RESOURCE_FILES(
    PREFIX contrib/python/pg8000/
    .dist-info/METADATA
    .dist-info/top_level.txt
)

END()
