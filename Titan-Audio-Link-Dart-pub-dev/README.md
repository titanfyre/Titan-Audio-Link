# Titan-Audio-Link Dart / Flutter Windows SDK

`dart pub add titan_audio_link` or `flutter pub add titan_audio_link`.

```dart
final client=await Client.open(); // import package:titan_audio_link/titan_audio_link.dart
try { print(client.call("list")); } finally { client.close(); }
```

Windows x64 only. Calls are synchronous; use a worker isolate for UI responsiveness. For compiled Dart or Flutter Windows deployment, explicitly copy `lib/native/TitanAudioLinkJsonSDK.dll` beside the executable or pass its absolute libraryPath. This is an FFI package; automatic Flutter DLL bundling is not provided. A full Flutter application has not been compiled in this validation. [API](doc/API.md). Matching driver/license required separately.
