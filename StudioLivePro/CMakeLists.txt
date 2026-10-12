cmake_minimum_required(VERSION 3.22)
project(StudioLivePro VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # static CRT: no VC++ redistributable needed
endif()

option(SLP_BUILD_TESTS "Build automated tests" ON)
option(SLP_BUILD_APP   "Build the JUCE application" ON)
set(SLP_JUCE_DIR "" CACHE PATH "Path to a JUCE checkout (leave empty to download JUCE 8 with FetchContent)")
set(SLP_ASIO_SDK_DIR "" CACHE PATH "Path to the Steinberg ASIO SDK (optional; enables the ASIO driver in the app)")

# ---- header-only DSP/mixer core ----
add_library(slp_core INTERFACE)
target_include_directories(slp_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
if(MSVC)
  target_compile_options(slp_core INTERFACE /W4 /permissive- /fp:precise /utf-8 /wd4100)
else()
  target_compile_options(slp_core INTERFACE -Wall -Wextra -Wno-misleading-indentation)
endif()

# ---- tests ----
if(SLP_BUILD_TESTS)
  enable_testing()
  add_executable(slp_tests Tests/tests_main.cpp Tests/tests_dsp.cpp Tests/tests_mixer.cpp Tests/tests_core.cpp Tests/tests_stress.cpp)
  target_link_libraries(slp_tests PRIVATE slp_core)
  if(MSVC)
    target_compile_options(slp_tests PRIVATE /O2)
  endif()
  add_test(NAME dsp_mixer_core COMMAND slp_tests)
  set_tests_properties(dsp_mixer_core PROPERTIES ENVIRONMENT "SLP_STRESS_SECONDS=60" WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

# ---- offline render tool (WAV in -> processed WAV out) ----
add_executable(slp_render Tools/render.cpp)
target_link_libraries(slp_render PRIVATE slp_core)

# ---- JUCE application ----
if(SLP_BUILD_APP)
  if(SLP_JUCE_DIR)
    add_subdirectory(${SLP_JUCE_DIR} juce_build EXCLUDE_FROM_ALL)
  else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.8 GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
  endif()
  juce_add_gui_app(StudioLivePro PRODUCT_NAME "StudioLivePro" COMPANY_NAME "Studio Live Pro" VERSION ${PROJECT_VERSION})
  target_sources(StudioLivePro PRIVATE Source/App/Main.cpp)
  target_compile_definitions(StudioLivePro PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="Studio Live Pro" JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
      JUCE_WASAPI=1 JUCE_DIRECTSOUND=0 JUCE_USE_MP3AUDIOFORMAT=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
  if(SLP_ASIO_SDK_DIR AND WIN32)
    target_compile_definitions(StudioLivePro PRIVATE JUCE_ASIO=1)
    target_include_directories(StudioLivePro PRIVATE ${SLP_ASIO_SDK_DIR}/common)
  endif()
  target_link_libraries(StudioLivePro PRIVATE slp_core juce::juce_audio_utils juce::juce_audio_devices juce::juce_gui_basics juce::juce_events
      PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  set_target_properties(StudioLivePro PROPERTIES OUTPUT_NAME "StudioLivePro")
endif()
