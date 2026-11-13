# Titan-Audio-Link Python SDK

`pip install titan-audio-link`

Windows x64 Python3.10+. No driver is installed by this package.

```python
from titan_audio_link import Client
with Client() as client:
    info = client.call("list")
```

[Operation schema and API](docs/API.md). Check errors and the product licensing state before device creation.
