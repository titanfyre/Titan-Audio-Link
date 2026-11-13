# Titan-Audio-Link .NET8 SDK

Package ID `TitanAudioLink.SDK`. NuGet upload remains on hold at owner request.

```csharp
using var client=new TitanCSA.Products.TitanAudioLink.Client();
string statusJson=client.Call("list", "{}");
```

Client implements IDisposable; Call takes raw argument JSON object and returns result JSON. NativeException exposes Code. UInt64 fields remain decimal strings. [API](docs/API.md). Windows x64 and separately installed matching driver required.
