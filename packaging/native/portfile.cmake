vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/titanfyre/Titan-Audio-Link/releases/download/sdk-v0.1.1/titan-audio-link-sdk-0.1.1.tar.gz"
    FILENAME "titan-audio-link-sdk-0.1.1.tar.gz"
    SHA512 714f5dc638f5428ef67a4aaf230236b55de475c40b7a96aef325c0edebe0c161ba78c8544f3d432f196d6eab8e1316a951e509f8e0487bb1a5698a0bad9312a2
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}" NO_REMOVE_ONE_LEVEL)
vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME TitanAudioLinkSDK CONFIG_PATH lib/cmake/TitanAudioLinkSDK)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
