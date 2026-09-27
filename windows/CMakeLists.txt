# Pyaw for Windows: the TSF input method DLL, plus tests and a CLI for the shared C++ engine.
#   cmake -S windows -B build/win-msvc/x64 -A x64   -DPYAW_SUFFIX=64
#   cmake -S windows -B build/win-msvc/x86 -A Win32 -DPYAW_SUFFIX=32
#   cmake --build build/win-msvc/x64 --config Release
cmake_minimum_required(VERSION 3.20)
project(Pyaw LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# The DLL is loaded into every app the user types in: link the C runtime statically so it
# never depends on a separately installed Visual C++ runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(PYAW_SUFFIX "64" CACHE STRING "DLL name suffix: 64, 32 or arm64")

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../cpp)
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS ${CORE_DIR}/pyaw/*.cpp)
add_library(pyaw_core STATIC ${CORE_SOURCES})
target_compile_definitions(pyaw_core PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
  # /fp:precise without contraction keeps float results identical to the macOS engine.
  target_compile_options(pyaw_core PUBLIC /utf-8 /W3 /fp:precise /EHsc /permissive-)
endif()

file(GLOB IME_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(pyaw SHARED ${IME_SOURCES} src/pyaw.rc src/pyaw.def)
target_link_libraries(pyaw PRIVATE pyaw_core ole32 oleaut32 uuid user32 gdi32 advapi32 shell32 dwmapi)
set_target_properties(pyaw PROPERTIES OUTPUT_NAME pyaw${PYAW_SUFFIX})

add_executable(engine_tests ${CORE_DIR}/tests/engine_tests.cpp)
target_link_libraries(engine_tests PRIVATE pyaw_core)

add_executable(pyaw_cli ${CORE_DIR}/tools/pyaw_cli.cpp)
target_link_libraries(pyaw_cli PRIVATE pyaw_core)

# End-to-end typing test against the registered DLL (see .github/workflows/windows.yml).
add_executable(typing_test tests/typing_test.cpp)
target_link_libraries(typing_test PRIVATE ole32 uuid user32)
target_compile_options(typing_test PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
