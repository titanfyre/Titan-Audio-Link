# Titan Audio Link - Windows Kernel Virtual Audio I/O Driver & C++ SDK

**Connect a virtual playback output to a virtual microphone input. Build repeatable audio tests and integrate virtual audio into Windows software.**

<!-- full-version-contact:start -->
> [!IMPORTANT]
> **Full version and commercial licensing**
>
> **Discord username: `titancsa`**
>
> Open Discord, choose **Friends > Add Friend**, enter **`titancsa`**, then send a direct message. Mention **Titan Audio Link** and ask about the **full / production version, commercial use or OEM redistribution**.
>
> Include your intended use and deployment size to discuss pricing, availability and the appropriate licensing / access instructions.
<!-- full-version-contact:end -->

Titan Audio Link provides paired Windows audio endpoints, a native desktop application, a console tool and a small C++ SDK. Send audio to the pair's playback endpoint in one application and select its recording endpoint in another. Each pair has its own kernel transport.

> **Microsoft-signed evaluation bundle.** Application, SDK, examples and self-contained UI/console installers are included. Check [validation status](docs/VALIDATION.md) for exercised platforms and remaining production qualification. No WHQL certification, universal compatibility or zero-latency claim is made.

![Native application in explicitly labelled offline preview mode](docs/app.png)

## What you can do

- Route an application's output into another application's microphone/recording input through explicitly selected endpoints.
- Generate repeatable audio fixtures for conferencing, recording and communication software tests.
- Inspect endpoint identity, state, format and advertised Windows audio periods.
- Verify a known signal through a paired virtual playback/recording path and copy its diagnostic report.
- Embed the two-file C++ SDK to enumerate endpoints and stream raw PCM through your own worker thread.

The initial native format is **48 kHz, stereo, signed 16-bit PCM**. The SDK presents interleaved floating-point stereo samples and handles its supported Windows shared-engine format conversion. It targets **shared-mode WASAPI**; exclusive capture is unsupported. This preview is not a general mixing desk, network audio system, physical microphone recorder or ASIO driver.

## Install and use

1. Download the complete Windows x64 ZIP from [Releases](https://github.com/titanfyre/Titan-Audio-Link/releases) and extract it.
2. Run `Dependencies\TitanAudioLinkSetup.exe` as administrator and choose **Install / reinstall**. Restart Windows only if setup requests it.
3. Open `TitanAudioLink.exe`. Refresh the endpoint list and select the playback/recording pair.
4. In the source application's output settings, select the **Titan Audio Link playback endpoint**. In the receiving application's input settings, select the corresponding Titan Audio Link recording endpoint. Windows can prefix their names with localized labels such as **Line** and **Line In**, and numbers distinguish additional adapters. The application pairs them by their adapter identity, not just their display names.
5. Use **Test virtual cable** to play quiet, independent left/right fixtures into the virtual output and check their arrival at the paired virtual input. Every frame of both sequences must match at the same sample offset, within explicit PCM16 conversion-noise bounds. The test does not open a physical microphone or speaker and does not record audio to disk.

Endpoints are deliberately excluded from Windows default-device selection. Select them explicitly in each application. This also means they cannot be selected as system or communications defaults in this edition. Your physical speakers and microphone remain separate devices.

The application and normal WASAPI streaming do not require administrator rights. Installation, adding pairs and removing them require elevation. Windows microphone privacy permissions can still affect a virtual recording endpoint: allow desktop microphone access in Windows settings if your intended application is denied access.

The setup console supports `/install`, `/uninstall`, `/add-pair` and `/remove-pair <instance-id>`. It manages only this product's root devices and matching driver packages. Up to four pairs can be managed; a pending restart is reported rather than forced. Uninstall removes the owned pairs. The application does not silently create, delete or reconfigure audio devices.

## Run without a driver

```powershell
.\TitanAudioLink.exe --preview
.\AudioLinkCLI.exe --preview
.\AudioLinkCLI.exe --list
```

Preview mode uses clearly labelled synthetic data. A normal inventory is read-only. A live test requires a real, enabled Titan Audio Link pair and reports errors rather than falling back to sample data.

## Minimal C++ use

Add `SDK/AudioLink.h` and `SDK/AudioLink.cpp` to a Windows x64 C++17 project. Link `ole32`, `uuid`, `propsys`, `cfgmgr32` and `avrt`. No WDK or kernel source is needed to build the application.

```cpp
#include "AudioLink.h"
#include <iostream>

int main() {
    auto inventory = AudioLink::List();
    if (inventory.error) {
        std::wcerr << AudioLink::Describe(inventory.error);
        return 1;
    }
    for (const auto& endpoint : inventory.endpoints)
        std::wcout << endpoint.name << L"\n" << endpoint.id << L"\n";
}
```

`List()` returns verified Titan endpoints; `List(true)` also returns read-only information about other Windows audio endpoints. Each endpoint includes its stable Windows endpoint ID, direction, state, adapter instance, mix format and advertised periods. IDs identify installed endpoint instances; they are not permanent machine identities.

`Stream(endpointId, callback, stopEvent, durationMs)` detects the endpoint's direction. A playback callback fills the supplied stereo sample block; a recording callback reads it. Return `false` to stop. Keep the callback short, do not resize or retain its sample block, and run the function on a worker thread. COM, WASAPI and cleanup stay on that same thread. See [the streaming example](Examples/GenerateTone.cpp).

`Test(renderId, captureId, durationMs, stopEvent)` verifies an explicitly selected pair and returns metrics plus a detailed English error when necessary. It rejects physical endpoints and endpoints from different adapters. `Describe(error)` includes the failing stage and HRESULT. See [SDK details](docs/SDK.md).

## Understand the results

Audio periods are Windows API values, not measured end-to-end delay. The transport reserves **5 ms plus the current native playback and capture packet durations** to absorb independently scheduled batches: two 10 ms packets mean a 25 ms reserve. WaveRT/Windows audio-engine buffering and normal scheduling add further delay. Native packets are limited to 1-40 ms. The bounded kernel ring can hold 160 ms; its capacity is not a fixed 160 ms delay.

Signal correlation and sample offset describe the test fixture's alignment through this virtual path. The reported delivery interval uses WASAPI capture timestamps and render submission timestamps. It is not physical microphone latency, acoustic latency, application processing time or a round-trip through a game, meeting service or network. WASAPI discontinuity flags are not a complete count of every possible internal underrun. See [measurement boundaries](docs/VALIDATION.md).

## Build

Demo/evaluation driver source is available; see [driver/README.md](driver/README.md) for
build instructions and separate evaluation-source terms. Those terms apply to
Titan-authored driver code. Third-party source licenses and existing application/SDK
licenses remain applicable. Full/commercial implementation and private signing or
activation material are excluded.

Install Visual Studio 2022 with C++ desktop tools, CMake and a Windows SDK:

```powershell
.\Build.ps1
.\Build.ps1 -AddressSanitizer
```

Application, SDK, examples and offline tests are public. The evaluation driver source and its retained third-party notices are documented in [driver/README.md](driver/README.md). Driver development and signing are separate from building this application. Current targets are Windows 10 2004+ and Windows 11 x64; actual tested builds are listed in the validation document.

## OEM integration and support

For virtual audio in your software, a branded package, reproducible audio QA fixtures or deployment/support work, add **`titancsa` on Discord**. Mention the intended workflow, supported Windows builds and deployment volume. We agree scope, acceptance tests and redistribution terms before a commercial engagement. No purchase or automatic activation is performed by this application.

See [commercial integration](docs/COMMERCIAL.md) for the pilot and delivery process.

The application and its original user-mode SDK are MIT licensed. The prebuilt driver has separate terms and incorporates Microsoft sample components under MS-PL; see [third-party notices](THIRD-PARTY-NOTICES.md). The published `driver/Driver/**` source files, including Titan changes in those files, retain MS-PL; see [the file-specific license map](driver/LICENSE-MAP.txt). The download includes `BINARY-LICENSE.txt` evaluation terms, the full MS-PL license and third-party notices. This evaluation build does not implement an enforced timer or online licensing. Existing Interceptor/Vdevice activation keys do not activate this driver.

Use the software only for authorized audio routing and testing. No illegal or unauthorized use is endorsed. Software is provided AS IS under its applicable license; users remain responsible for their actions, subject to applicable law.

<!-- central-sdk-packages:start -->
## Paquets et installation

Les sources, licences, guides API et exemples sont regroupes dans [le depot SDK dedie](https://github.com/TitanCSA/sdk-packages/tree/main/projects/Titan-Audio-Link). Choisis le paquet a installer puis execute sa commande dans son dossier.

| Registre | Paquet | Installation / utilisation |
| --- | --- | --- |
| vcpkg | `titan-audio-link` | `vcpkg install titan-audio-link:x64-windows` |
| Conan | `titan-audio-link` | `conan install --requires=titan-audio-link/0.1.1 -r titancsa --build=missing -s compiler.cppstd=17` |
| NuGet | `Titan-Audio-Link.Native` | `Install-Package Titan-Audio-Link.Native -Version 0.1.1` |
| NuGet | `TitanAudioLink.SDK` | `dotnet add package TitanAudioLink.SDK --version 1.0.0` |
| PyPI | `titan_audio_link` | Commande prevue apres publication : `python -m pip install titan-audio-link==1.0.0` |
| Conda | `titan-audio-link` | `conda install --override-channels -c https://raw.githubusercontent.com/TitanCSA/sdk-packages/089bc73095aa8eda89bacbe10d0b9e58394b0207/conda-channel -c conda-forge titan-audio-link` (canal TitanCSA) |
| crates.io | `titan_audio_link` | `cargo add titan-audio-link@1.0.0` |
| npm | `titan_audio_link` | Commande prevue apres publication : `npm install titan-audio-link@1.0.0` |
| Maven Central | `io.github.titancsa:titan-audio-link` | `io.github.titancsa:titan-audio-link:1.0.0 (pom.xml / Gradle)` |
| JitPack | `com.github.titanfyre:Titan-Audio-Link` | `com.github.titanfyre:Titan-Audio-Link:java-sdk-v1.0.0 (avec le depot JitPack)` |
| Go Modules | `github.com/titanfyre/Titan-Audio-Link/Titan-Audio-Link-Go-modules` | `go get github.com/titanfyre/Titan-Audio-Link/Titan-Audio-Link-Go-modules@v1.0.0` |
| pkg.go.dev | `github.com/titanfyre/Titan-Audio-Link/Titan-Audio-Link-Go-modules` | `Documentation : https://pkg.go.dev/github.com/titanfyre/Titan-Audio-Link/Titan-Audio-Link-Go-modules@v1.0.0` |
| PowerShell Gallery | `titan_audio_link` | `Install-Module TitanAudioLink -RequiredVersion 1.0.0 -Scope CurrentUser` |
| pub.dev | `titan_audio_link` | Commande prevue apres publication : `dart pub add titan_audio_link:1.0.0` |
| RubyGems | `titan_audio_link` | `gem install titan-audio-link -v 1.0.0` |
| Packagist | `titan_audio_link` | Commande prevue apres publication : `composer require titancsa/titan-audio-link:1.0.0` |

Pour vcpkg et Conan, configure d abord le registre TitanCSA avec [le guide complet](https://github.com/TitanCSA/sdk-packages/tree/main/projects/Titan-Audio-Link/README.md). Le guide explique aussi Maven/Gradle, les prerequis et le chargement des DLL. Les pilotes/applications du produit sont installes separement. Les commandes marquees prevues ne sont pas encore disponibles.

Les anciens imports Go et versions JitPack gardent leurs tags. Pour les anciennes archives SDK, utilise [les liens de remplacement](https://github.com/TitanCSA/sdk-packages/blob/main/release-assets/URL-MIGRATION.json).
<!-- central-sdk-packages:end -->
