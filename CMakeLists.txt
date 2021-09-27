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
endif()


if (WIN32)
    add_definitions("-DWIN32 -D_WIN32 -DUNICODE -D_UNICODE")
endif()

add_library(utils ioutil.cpp ioutil.h)

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
    ${CMAKE_CURRENT_BINARY_DIR}/instruction_table.h
    )
target_link_libraries(m68k PRIVATE utils)
target_include_directories(m68k PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

add_executable(test68k test68k.cpp)
target_link_libraries(test68k PRIVATE utils m68k)

add_executable(amiemu
    main.cpp
    cia.cpp cia.h
    custom.cpp custom.h
    gui_win32.cpp gui.h
    )
target_link_libraries(amiemu PRIVATE utils m68k)
