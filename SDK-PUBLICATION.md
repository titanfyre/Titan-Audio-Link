# SDK publication status

Current source layout and registry coverage. A GitHub download is distinct from publication on a central registry.

| Target | Status | Package / documentation |
| --- | --- | --- |
| CPP/vcpkg | published_custom_registry | titan-audio-link |
| CPP/conan | published_custom_source_index | [titan-audio-link](https://github.com/TitanCSA/sdk-packages/tree/089bc73095aa8eda89bacbe10d0b9e58394b0207/conan-index) [Source](packaging/native/) |
| CPP/nuget | published_payload_verified | [Titan-Audio-Link.Native](https://www.nuget.org/packages/Titan-Audio-Link.Native/0.1.1) |
| NET/nuget | published_payload_verified | [TitanAudioLink.SDK](https://www.nuget.org/packages/TitanAudioLink.SDK/1.0.0) |
| Python/pypi | built_fixture_and_production_verified | titan-audio-link [Source](Titan-Audio-Link-Python-pypi/) |
| Python/conda-forge | custom_channel_install_verified_requires_conda_forge_review | [titan-audio-link](https://raw.githubusercontent.com/TitanCSA/sdk-packages/089bc73095aa8eda89bacbe10d0b9e58394b0207/conda-channel) [Source](packaging/conda/) |
| Rust/cargo | published_install_verified | [titan-audio-link](https://crates.io/crates/titan-audio-link) [Source](Titan-Audio-Link-Rust-cargo/) |
| JavaScript/npm | stopped_before_submission | titan-audio-link [Source](Titan-Audio-Link-JavaScript-npm/) |
| Java/maven | published_install_verified | [titan-audio-link](https://central.sonatype.com/artifact/io.github.titancsa/titan-audio-link/1.0.0) [Source](Titan-Audio-Link-Java-maven/) |
| Java/jitpack | published_install_verified | [titan-audio-link](https://jitpack.io/#titanfyre/Titan-Audio-Link/java-sdk-v1.0.0) [Source](Titan-Audio-Link-Java-maven/) |
| Go/modules | published_install_verified | [titan-audio-link](https://pkg.go.dev/github.com/titanfyre/Titan-Audio-Link/Titan-Audio-Link-Go-modules@v1.0.0) [Source](Titan-Audio-Link-Go-modules/) |
| Go/pkg-go-dev | indexed_verified | [Documentation](https://pkg.go.dev/github.com/titanfyre/Titan-Audio-Link/Titan-Audio-Link-Go-modules@v1.0.0) |
| PowerShell/gallery | published_install_verified | [TitanAudioLink](https://www.powershellgallery.com/packages/TitanAudioLink/1.0.0) [Source](Titan-Audio-Link-PowerShell-gallery/) |
| Dart/pub-dev | built_fixture_and_production_verified | titan_audio_link [Source](Titan-Audio-Link-Dart-pub-dev/) |
| Ruby/rubygems | published_install_verified | [titan-audio-link](https://rubygems.org/gems/titan-audio-link) [Source](Titan-Audio-Link-Ruby-rubygems/) |
| PHP/packagist | composer_source_and_tag_published | [titancsa/titan-audio-link](https://packagist.org/packages/titancsa/titan-audio-link) |

Native SDK source and CMake/Conan/vcpkg/NuGet recipes share [`packaging/native`](packaging/native/LAYOUT.md). Previous release archives and tags remain unchanged.

Application and driver prerequisites and original licensing remain those documented by each SDK. No driver installer is bundled with these source packages.
