# Titan-Audio-Link Java / Kotlin SDK

Maven coordinate `io.github.titancsa:titan-audio-link:1.0.0`. Java22+ Windows x64. Start with `--enable-native-access=ALL-UNNAMED`.

```java
try(var client=new titancsa.products.titan_audio_link.Client()){
  String response=client.call("list", "{}");
}
```

Call returns the JSON envelope `{ok,result}` or `{ok:false,code,message}`. Inspect ok/code in your JSON parser. Kotlin can use the same Client/AutoCloseable API. Native resources are extracted into a SHA-checked product cache. [Schema](docs/API.md). Original driver/license requirements unchanged.
