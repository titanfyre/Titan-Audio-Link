cmake_minimum_required(VERSION 3.24)
project(TitanAudioLinkSDK VERSION 0.1.1 LANGUAGES C CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This SDK requires Windows x64. No driver is installed by this build.")
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
add_library(TitanTitanAudioLink STATIC
  "sdk/SDK/AudioLink.cpp"
)
add_library(TitanAudioLinkSDK::sdk ALIAS TitanTitanAudioLink)
set_target_properties(TitanTitanAudioLink PROPERTIES EXPORT_NAME sdk)
target_compile_features(TitanTitanAudioLink PUBLIC cxx_std_17)
target_compile_definitions(TitanTitanAudioLink PUBLIC )
target_compile_definitions(TitanTitanAudioLink PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
if(MSVC)
  target_compile_options(TitanTitanAudioLink PRIVATE /utf-8 /EHsc /W4)
endif()
target_include_directories(TitanTitanAudioLink PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk>"
  "$<INSTALL_INTERFACE:include/>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/SDK>"
  "$<INSTALL_INTERFACE:include/SDK>"
)
target_link_libraries(TitanTitanAudioLink PUBLIC advapi32 avrt cfgmgr32 comctl32 crypt32 gdi32 newdev ole32 propsys setupapi shell32 user32 uuid wintrust)
install(TARGETS TitanTitanAudioLink EXPORT TitanAudioLinkSDKTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY sdk/ DESTINATION include FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl")
install(FILES LICENSE DESTINATION share/titan-audio-link)
install(EXPORT TitanAudioLinkSDKTargets NAMESPACE TitanAudioLinkSDK:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TitanAudioLinkSDK)
configure_file("TitanAudioLinkSDKConfig.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/TitanAudioLinkSDKConfig.cmake" @ONLY)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/TitanAudioLinkSDKConfigVersion.cmake" VERSION 0.1.1 COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/TitanAudioLinkSDKConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/TitanAudioLinkSDKConfigVersion.cmake" DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TitanAudioLinkSDK)
