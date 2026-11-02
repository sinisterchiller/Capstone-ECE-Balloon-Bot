# This is a generic toolchain support file to support GCC target
#
# The arm-none-eabi toolchain must be in PATH.
# On macOS, after downloading from:
#   https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
# extract it and add to PATH, e.g.:
#   export PATH=/path/to/gcc-arm-none-eabi-14.x/bin:$PATH
#
# The linker flags are resolved by the compiler driver automatically — no
# hardcoded library paths are used. Changing toolchains requires reconfiguring
# the CMake build (delete CMakeCache.txt / CMakeFiles/ and re-run cmake ..).
#
# Optionally override TOOLCHAIN_PREFIX if the prefix differs:
#   cmake .. -DTOOLCHAIN_PREFIX=arm-none-eabi-

set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

# Some target device related default GCC settings
if(CPU STREQUAL "Cortex-M0")
  set(CPU_FLAGS -mcpu=cortex-m0)
elseif(CPU STREQUAL "Cortex-M0+")
  set(CPU_FLAGS -mcpu=cortex-m0plus)
elseif(CPU STREQUAL "Cortex-M1")
  set(CPU_FLAGS -mcpu=cortex-m1)
elseif(CPU STREQUAL "Cortex-M3")
  set(CPU_FLAGS -mcpu=cortex-m3)
elseif(CPU STREQUAL "Cortex-M4")
  if(FPU STREQUAL "SP_FPU")
    set(CPU_FLAGS -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard)
  else()
    set(CPU_FLAGS -mcpu=cortex-m4 -mfloat-abi=soft)
  endif()
elseif(CPU STREQUAL "Cortex-M7")
  if(FPU STREQUAL "DP_FPU")
    set(CPU_FLAGS -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard)
  elseif(FPU STREQUAL "SP_FPU")
    set(CPU_FLAGS -mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfloat-abi=hard)
  else()
    set(CPU_FLAGS -mcpu=cortex-m7)
  endif()
elseif(CPU STREQUAL "Cortex-M23")
  set(CPU_FLAGS -mcpu=cortex-m23)
elseif(CPU STREQUAL "Cortex-M33")
  if(FPU STREQUAL "SP_FPU")
    if(DSP STREQUAL "DSP")
      set(CPU_FLAGS -mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard)
    else()
      set(CPU_FLAGS -mcpu=cortex-m33+nodsp -mfpu=fpv5-sp-d16 -mfloat-abi=hard)
    endif()
  else()
    if(DSP STREQUAL "DSP")
      set(CPU_FLAGS -mcpu=cortex-m33)
    else()
      set(CPU_FLAGS -mcpu=cortex-m33+nodsp)
    endif()
  endif()
endif()

if(BYTE_ORDER STREQUAL "Little-endian")
  set(CPU_FLAGS ${CPU_FLAGS} -mlittle-endian)
elseif(BYTE_ORDER STREQUAL "Big-endian")
  set(CPU_FLAGS ${CPU_FLAGS} -mbig-endian)
endif()

set(CPU_FLAGS ${CPU_FLAGS} -mthumb)

if(SECURE STREQUAL "Secure" OR SECURE STREQUAL "Secure-only")
  set(CC_SECURE -mcmse)
endif()

# arm-none-eabi- must be part of path environment
# Use TOOLCHAIN_PREFIX if set, otherwise find in PATH
if(NOT DEFINED TOOLCHAIN_PREFIX)
  find_program(ARM_NONE_EABI_GCC arm-none-eabi-gcc PATHS ENV PATH)
  if(ARM_NONE_EABI_GCC)
    get_filename_component(_toolchain_dir "${ARM_NONE_EABI_GCC}" DIRECTORY)
    get_filename_component(TOOLCHAIN_PREFIX "${_toolchain_dir}" NAME)
    # TOOLCHAIN_PREFIX now holds "arm-none-eabi" — we'll prepend path below
    set(_found_toolchain_dir "${_toolchain_dir}")
  endif()
endif()

# If TOOLCHAIN_PREFIX was explicitly set (no path), use it directly
if(DEFINED TOOLCHAIN_PREFIX AND NOT DEFINED _found_toolchain_dir)
  set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}gcc)
  set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})
  set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
  set(CMAKE_OBJCOPY      ${TOOLCHAIN_PREFIX}objcopy)
  set(CMAKE_SIZE         ${TOOLCHAIN_PREFIX}size)
else()
  # Use full paths from find_program result
  set(CMAKE_C_COMPILER   ${ARM_NONE_EABI_GCC})
  set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})
  set(CMAKE_CXX_COMPILER ${_found_toolchain_dir}/arm-none-eabi-g++)
  set(CMAKE_OBJCOPY      ${_found_toolchain_dir}/arm-none-eabi-objcopy)
  set(CMAKE_SIZE         ${_found_toolchain_dir}/arm-none-eabi-size)
endif()

set(FLAGS                           "-fdata-sections -ffunction-sections -Wl,--gc-sections -Wno-comment")
set(CPP_FLAGS                       "${FLAGS} -fno-rtti -fno-exceptions -fno-threadsafe-statics")

set(CMAKE_C_FLAGS                   ${FLAGS})
set(CMAKE_CXX_FLAGS                 ${CPP_FLAGS})

set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C       ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX     ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_DEBUG "")
set(CMAKE_CXX_FLAGS_DEBUG "")
set(CMAKE_ASM_FLAGS_DEBUG "")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "")

set(CMAKE_C_FLAGS_RELEASE "")
set(CMAKE_CXX_FLAGS_RELEASE "")
set(CMAKE_ASM_FLAGS_RELEASE "")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "")
