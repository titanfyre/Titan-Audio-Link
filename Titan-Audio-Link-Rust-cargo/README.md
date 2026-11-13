# Titan-Audio-Link Rust SDK

`cargo add titan-audio-link`

Windows x64 MSVC Rust1.82+. Bundled DLL/import library; copy native DLL beside deployed executable.

```rust,no_run
use titan_audio_link::Client;
let mut client=Client::new()?;
let result=client.call("list",serde_json::json!({}))?;
# Ok::<(),Box<dyn std::error::Error>>(())
```

[API](docs/API.md). Drop closes owned session; original driver and licensing requirements unchanged.
