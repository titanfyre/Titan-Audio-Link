# Titan-Audio-Link Go SDK

`go get github.com/titanfyre/Titan-Audio-Link/Titan-Audio-Link-Go-modules@v1.0.0`

Windows x64; pure Go syscall bindings, no cgo. DLL embedded and SHA-checked in user cache.

```go
client, err := sdk.New() // import sdk "github.com/titanfyre/Titan-Audio-Link/Titan-Audio-Link-Go-modules"
if err != nil { panic(err) }; defer client.Close()
status, err := client.Call("list", nil) // json.RawMessage
```

[API](docs/API.md). Use NewWithLibrary for an explicit DLL. Matching driver installed separately.
