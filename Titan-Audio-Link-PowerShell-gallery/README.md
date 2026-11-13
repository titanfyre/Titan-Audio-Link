# Titan-Audio-Link PowerShell SDK

PowerShell7.4+ Windows x64. `Install-Module TitanAudioLink -Scope CurrentUser`.

```powershell
$client=Connect-TitanAudioLinkSDK
try { Invoke-TitanAudioLinkSDK -Client $client -Operation list } finally { Disconnect-TitanAudioLinkSDK -Client $client }
```

Use -Arguments with a hashtable matching [schema](docs/API.md). Native errors retain their Windows code through the inner exception. Matching driver installed separately.
