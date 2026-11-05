cmake_minimum_required(VERSION 3.22)
project(WeChess VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# The Android NDK does not ship libcurl. Enable WECHESS_WITH_CURL when you have
# cross-compiled it (README.md documents this) so Gemini explanations work.
if(ANDROID)
  set(WECHESS_CURL_DEFAULT OFF)
else()
  set(WECHESS_CURL_DEFAULT ON)
endif()

option(WECHESS_WITH_CURL "Link libcurl for HTTPS (Gemini proxy client)" ${WECHESS_CURL_DEFAULT})
option(WECHESS_BUILD_TESTS "Build the test suite" ON)
option(WECHESS_BUILD_PROXY "Build the server-side Gemini proxy" ON)

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Embedded HTML/CSS: authored files, compiled into the binary.
# ---------------------------------------------------------------------------
set(WECHESS_ASSET_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_assets.cpp")
add_custom_command(
  OUTPUT "${WECHESS_ASSET_SOURCE}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
  COMMAND "${CMAKE_COMMAND}"
          -DSHELL_FILE=${CMAKE_CURRENT_SOURCE_DIR}/frontend/html/shell.html
          -DCSS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/frontend/css/wechess.css
          -DOUTPUT=${WECHESS_ASSET_SOURCE}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_assets.cmake
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/frontend/html/shell.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/frontend/css/wechess.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_assets.cmake"
  COMMENT "Embedding frontend assets"
  VERBATIM
)
add_library(wechess_assets STATIC "${WECHESS_ASSET_SOURCE}")

# ---------------------------------------------------------------------------
# Core library: chess rules, PGN, engine, analysis, Gemini, HTTP server.
# ---------------------------------------------------------------------------
add_library(wechess_core STATIC
  cpp/core/eval.cpp
  cpp/core/game.cpp
  cpp/analysis/opening.cpp
  cpp/analysis/classify.cpp
  cpp/analysis/stats.cpp
  cpp/analysis/review.cpp
  cpp/engine/uci.cpp
  cpp/engine/analyzer.cpp
  cpp/gemini/json.cpp
  cpp/gemini/http_client.cpp
  cpp/gemini/client.cpp
  cpp/gemini/prompt.cpp
  cpp/server/app.cpp
  cpp/server/http.cpp
  cpp/server/store.cpp
  cpp/server/session.cpp
  cpp/server/views.cpp
)

target_include_directories(wechess_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/cpp/third_party"
)
target_link_libraries(wechess_core PUBLIC Threads::Threads wechess_assets)

if(WECHESS_WITH_CURL)
  find_package(CURL REQUIRED)
  target_link_libraries(wechess_core PUBLIC CURL::libcurl)
  target_compile_definitions(wechess_core PUBLIC WECHESS_WITH_CURL)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(wechess_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# Desktop server and proxy (not built for Android).
# ---------------------------------------------------------------------------
if(NOT ANDROID)
  add_executable(wechess_server cpp/main/main.cpp)
  target_link_libraries(wechess_server PRIVATE wechess_core)

  if(WECHESS_BUILD_PROXY)
    add_executable(wechess_proxy cpp/proxy/proxy_main.cpp)
    target_link_libraries(wechess_proxy PRIVATE wechess_core)
    if(NOT WECHESS_WITH_CURL)
      message(WARNING "wechess_proxy needs HTTPS: enable WECHESS_WITH_CURL.")
    endif()
  endif()
endif()

# ---------------------------------------------------------------------------
# Android: one shared library hosting the server, loaded by the WebView wrapper.
# ---------------------------------------------------------------------------
if(ANDROID)
  add_library(wechess SHARED cpp/android/jni_bridge.cpp)
  target_link_libraries(wechess PRIVATE wechess_core log)
  target_compile_options(wechess PRIVATE -Wall -Wextra -Wno-unused-parameter)

  # Optional: ship a prebuilt Stockfish binary as an executable native library.
  set(STOCKFISH_ANDROID_BIN "" CACHE FILEPATH
      "Path to a Stockfish executable for the target ABI (copied to jniLibs)")
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(WECHESS_BUILD_TESTS AND NOT ANDROID)
  enable_testing()
  set(STOCKFISH_HOST_BIN "" CACHE FILEPATH
      "Stockfish binary used by the end-to-end analysis tests")

  add_executable(test_core tests/core_test.cpp)
  target_link_libraries(test_core PRIVATE wechess_core)

  add_executable(test_analysis tests/analysis_test.cpp)
  target_link_libraries(test_analysis PRIVATE wechess_core)

  add_executable(test_server tests/server_test.cpp)
  target_link_libraries(test_server PRIVATE wechess_core)

  add_executable(test_gemini tests/gemini_test.cpp)
  target_link_libraries(test_gemini PRIVATE wechess_core)

  add_test(NAME core COMMAND test_core)
  add_test(NAME analysis COMMAND test_analysis)
  add_test(NAME server COMMAND test_server)
  add_test(NAME gemini COMMAND test_gemini)

  set_tests_properties(analysis PROPERTIES ENVIRONMENT "WECHESS_STOCKFISH=${STOCKFISH_HOST_BIN}")
endif()
