# SDK publication status

Current source layout and registry coverage. A GitHub download is distinct from publication on a central registry.

| Target | Status | Package / documentation |
| --- | --- | --- |
| CPP/vcpkg | published_custom_registry | titan-audio-link |
| CPP/conan | public_recipe_builds_locally |  [Source](packaging/native/) |
| CPP/nuget | paused_pending_owner_email_confirmation | Titan-Audio-Link.Native |
| NET/nuget | built_verified_publication_on_hold_by_owner | TitanAudioLink.SDK [Source](Titan-Audio-Link-NET-nuget/) |
| Python/pypi | built_fixture_and_production_verified | titan-audio-link [Source](Titan-Audio-Link-Python-pypi/) |
| Python/conda-forge | built_tested_requires_source_publication_and_official_review | titan-audio-link [Source](packaging/conda/) |
| Rust/cargo | published_install_verified | [titan-audio-link](https://crates.io/crates/titan-audio-link) [Source](Titan-Audio-Link-Rust-cargo/) |
| JavaScript/npm | awaiting_browser_publication_authentication | [titan-audio-link](https://www.npmjs.com/package/titan-audio-link) [Source](Titan-Audio-Link-JavaScript-npm/) |
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
