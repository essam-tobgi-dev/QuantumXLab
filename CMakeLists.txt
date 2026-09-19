cmake_minimum_required(VERSION 3.28)
project(QuantumXLab VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(QXL_DEV "Developer mode (asserts, GL debug, dev-only features)" ON)
option(QXL_BUILD_TESTS "Build tests" ON)
option(QXL_USE_BLAS "Use Accelerate/OpenBLAS for dense ops >= 64" ON)
option(QXL_WERROR "Warnings as errors" OFF)

# ---------------------------------------------------------------- dependencies
include(FetchContent)
set(FETCHCONTENT_QUIET ON)
set(FETCHCONTENT_BASE_DIR ${CMAKE_SOURCE_DIR}/.deps CACHE PATH "shared dependency cache")

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz)
FetchContent_Declare(glm URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.tar.gz)
FetchContent_Declare(imgui URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b-docking.tar.gz)
FetchContent_Declare(implot URL https://github.com/epezent/implot/archive/refs/tags/v0.17.tar.gz)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(spdlog URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.3.tar.gz)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
FetchContent_Declare(freetype URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-3.tar.gz)
if(QXL_BUILD_TESTS)
  FetchContent_Declare(catch2 URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.8.0.tar.gz)
endif()
FetchContent_MakeAvailable(glfw glm spdlog freetype imgui implot)
if(QXL_BUILD_TESTS)
  FetchContent_MakeAvailable(catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(CTest)
  include(Catch)
endif()

add_library(imgui_lib STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui_lib PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/freetype)
target_link_libraries(imgui_lib PUBLIC glfw freetype)
target_compile_definitions(imgui_lib PUBLIC IMGUI_DEFINE_MATH_OPERATORS GL_SILENCE_DEPRECATION IMGUI_ENABLE_FREETYPE
  IMGUI_USE_WCHAR32) # the mathematical alphanumerics (U+1D400–1D7FF) of the LaTeX face, spec 20 §1

add_library(implot_lib STATIC ${implot_SOURCE_DIR}/implot.cpp ${implot_SOURCE_DIR}/implot_items.cpp)
target_include_directories(implot_lib PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot_lib PUBLIC imgui_lib)

# ---------------------------------------------------------------- common flags
add_library(qxl_options INTERFACE)
if(MSVC)
  target_compile_options(qxl_options INTERFACE /W4 /permissive- /Zc:__cplusplus /utf-8 /wd4100 /wd4244 /wd4267)
  if(QXL_WERROR)
    target_compile_options(qxl_options INTERFACE /WX)
  endif()
else()
  target_compile_options(qxl_options INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter)
  if(QXL_WERROR)
    target_compile_options(qxl_options INTERFACE -Werror)
  endif()
endif()
if(QXL_DEV)
  target_compile_definitions(qxl_options INTERFACE QXL_DEV=1)
endif()
target_compile_definitions(qxl_options INTERFACE
  QXL_ASSET_DIR="${CMAKE_SOURCE_DIR}/Assets"
  QXL_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  QXL_VERSION="${PROJECT_VERSION}"
  GLFW_INCLUDE_NONE)
target_include_directories(qxl_options INTERFACE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/third_party)
if(APPLE AND QXL_USE_BLAS)
  target_compile_definitions(qxl_options INTERFACE QXL_HAVE_ACCELERATE=1)
  target_link_libraries(qxl_options INTERFACE "-framework Accelerate")
endif()

# ---------------------------------------------------------------- module helper
# qxl_module(<Name> <Dir> [DEPS ...])  -> target qxl_<lowercase name>
# Sources are globbed from src/<Dir>/ (CONFIGURE_DEPENDS) so writers can add files freely.
# Tests are globbed from tests/<Dir>/ into one Catch2 executable per module.
set(QXL_ALL_MODULES "" CACHE INTERNAL "")
function(qxl_module NAME DIR)
  cmake_parse_arguments(M "" "" "DEPS;LIBS" ${ARGN})
  string(TOLOWER ${NAME} lname)
  set(target qxl_${lname})
  file(GLOB_RECURSE srcs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/${DIR}/*.cpp)
  if(srcs)
    add_library(${target} STATIC ${srcs})
    target_link_libraries(${target} PUBLIC qxl_options ${M_DEPS} ${M_LIBS})
  else()
    add_library(${target} INTERFACE)
    target_link_libraries(${target} INTERFACE qxl_options ${M_DEPS} ${M_LIBS})
  endif()
  set(QXL_ALL_MODULES "${QXL_ALL_MODULES};${target}" CACHE INTERNAL "")
  if(QXL_BUILD_TESTS)
    file(GLOB_RECURSE tsrcs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/${DIR}/*.cpp)
    if(tsrcs)
      add_executable(test_${lname} ${tsrcs})
      target_link_libraries(test_${lname} PRIVATE ${target} Catch2::Catch2WithMain)
      catch_discover_tests(test_${lname} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    endif()
  endif()
endfunction()

# Layer 0
qxl_module(Core       Core        LIBS spdlog::spdlog)
qxl_module(Units      Units       DEPS qxl_core)
# Layer 1
qxl_module(Numerics   Numerics    DEPS qxl_core qxl_units)
qxl_module(QSim       QSim        DEPS qxl_numerics)
qxl_module(Data       Data        DEPS qxl_units)
# Layer 2
qxl_module(Hardware   Hardware    DEPS qxl_numerics qxl_units)
qxl_module(Noise      Noise       DEPS qxl_qsim qxl_hardware)
qxl_module(Pulse      Pulse       DEPS qxl_hardware)
qxl_module(Cryo       Cryo        DEPS qxl_hardware qxl_data)
qxl_module(Lang       Lang        DEPS qxl_core)
qxl_module(IR         IR          DEPS qxl_numerics qxl_lang)
qxl_module(Compiler   Compiler    DEPS qxl_ir qxl_hardware qxl_pulse)
qxl_module(QEC        QEC         DEPS qxl_ir qxl_qsim)
# Layer 3
qxl_module(Runtime    Runtime     DEPS qxl_compiler qxl_qsim qxl_noise qxl_cryo qxl_data qxl_qec)
qxl_module(Instr      Instruments DEPS qxl_pulse qxl_cryo qxl_data qxl_qsim qxl_noise)
# Layer 4
if(NOT APPLE)
  add_library(glad STATIC ${CMAKE_SOURCE_DIR}/third_party/glad/src/gl.c)
  target_include_directories(glad PUBLIC ${CMAKE_SOURCE_DIR}/third_party/glad/include)
  set(QXL_GL_LOADER glad)
else()
  set(QXL_GL_LOADER "")
endif()
qxl_module(Gfx        Graphics    DEPS qxl_core LIBS glfw glm::glm freetype ${QXL_GL_LOADER})
qxl_module(Lab        Lab         DEPS qxl_gfx qxl_hardware qxl_cryo qxl_data)
# Layer 5
qxl_module(Viz        Viz         DEPS qxl_gfx qxl_lab qxl_qsim qxl_ir qxl_pulse qxl_hardware qxl_qec qxl_data LIBS implot_lib)
qxl_module(UI         UI          DEPS qxl_viz qxl_lab qxl_runtime qxl_instr qxl_lang LIBS imgui_lib implot_lib)
qxl_module(Report     Report      DEPS qxl_runtime qxl_data qxl_gfx)
# Layer 6
qxl_module(App        App         DEPS qxl_ui qxl_report qxl_lab)

if(APPLE)
  get_target_property(_gfx_type qxl_gfx TYPE)
  if(_gfx_type STREQUAL "INTERFACE_LIBRARY")
    target_link_libraries(qxl_gfx INTERFACE "-framework OpenGL" "-framework Cocoa" "-framework IOKit" "-framework CoreFoundation")
  else()
    target_link_libraries(qxl_gfx PUBLIC "-framework OpenGL" "-framework Cocoa" "-framework IOKit" "-framework CoreFoundation")
  endif()
endif()

file(GLOB app_main CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/main.cpp)
if(app_main)
  add_executable(quantumxlab ${app_main})
  target_link_libraries(quantumxlab PRIVATE qxl_app)
endif()
