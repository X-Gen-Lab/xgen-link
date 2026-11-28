set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m0)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES XGL_ARM_TOOLCHAIN_ROOT)
set(CMAKE_C_COMPILER "${XGL_ARM_TOOLCHAIN_ROOT}/bin/arm-none-eabi-gcc")
set(CMAKE_C_FLAGS_INIT
    "-mcpu=cortex-m0 -mthumb -mfloat-abi=soft -ffreestanding -ffunction-sections -fdata-sections -fstack-usage"
)
