# Titan-Audio-Link Ruby SDK

`gem install titan-audio-link`

Windows x64 Ruby3.1+.

```ruby
require "titan_audio_link"
TitanAudioLinkSDK::Client.open { |client| p client.call("list") }
```

[API and operation schema](docs/API.md). Matching driver installed separately.
