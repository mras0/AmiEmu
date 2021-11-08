cmake_minimum_required(VERSION 3.7)
project(amiemu CXX)

# Just hack the settings for now

# Note: To use clang-cl on windows: set CXX=clang-cl && cmake -G "NMake Makefiles" <path>

if (MSVC)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Supported build configurations" FORCE)

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest /permissive- /Zi /Zo /sdl /W4")
    #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4324") # C4324: Structure was padded due to alignment specifier
    if (NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /JMC")
    endif()
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG")
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
    add_definitions("-D_CRT_SECURE_NO_WARNINGS")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++2a -Wall -Wextra -pedantic")
endif()


if (WIN32)
    add_definitions("-DWIN32 -D_WIN32 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x600")
endif()

add_library(utils ioutil.cpp ioutil.h color_util.h)

add_executable(mktab mktab.cpp)
target_link_libraries(mktab PRIVATE utils)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/instruction_table.h
  COMMAND mktab ${CMAKE_CURRENT_BINARY_DIR}/instruction_table.h
  DEPENDS mktab
  )

add_library(m68k
    cpu.cpp cpu.h
    disasm.cpp disasm.h
    instruction.cpp instruction.h
    memory.cpp memory.h
    asm.cpp asm.h
    state_file.cpp state_file.h
    ${CMAKE_CURRENT_BINARY_DIR}/instruction_table.h
    )
target_link_libraries(m68k PRIVATE utils)
target_include_directories(m68k PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

add_executable(test68k test68k.cpp test68k.h test68k_timing.cpp test_state_file.cpp)
target_link_libraries(test68k PRIVATE utils m68k)

add_executable(testasm testasm.cpp)
target_link_libraries(testasm PRIVATE utils m68k)

add_executable(m68kasm m68kasm.cpp)
target_link_libraries(m68kasm PRIVATE utils m68k)

add_executable(m68kdisasm m68kdisasm.cpp)
target_link_libraries(m68kdisasm PRIVATE utils m68k)

set(EXPROM_BASE 40)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/exprom.h
  COMMAND m68kasm ${CMAKE_CURRENT_SOURCE_DIR}/exprom.asm -ofmt header -org ${EXPROM_BASE} -o ${CMAKE_CURRENT_BINARY_DIR}/exprom.h
  DEPENDS m68kasm ${CMAKE_CURRENT_SOURCE_DIR}/exprom.asm
  )

add_executable(amiemu
    main.cpp
    cia.cpp cia.h
    custom.cpp custom.h
    disk_drive.cpp disk_drive.h
    gui_win32.cpp gui.h
    debug.cpp debug.h
    wavedev_win32.cpp wavedev.h
    exprom.asm
    rtc.cpp rtc.h
    autoconf.cpp autoconf.h
    harddisk.cpp harddisk.h
    dms.cpp dms.h
    adf.cpp adf.h
    ${CMAKE_CURRENT_BINARY_DIR}/exprom.h
    )
set_source_files_properties(exprom.asm PROPERTIES HEADER_FILE_ONLY TRUE)
target_link_libraries(amiemu PRIVATE utils m68k)
if (WIN32)
    target_link_libraries(amiemu PRIVATE winmm)
endif()
target_include_directories(amiemu PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(amiemu PRIVATE EXPROM_BASE=0x${EXPROM_BASE})
