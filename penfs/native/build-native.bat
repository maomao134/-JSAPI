@echo off
rem 交叉编译 penjsapi.c -> ..\libs\libjsapi_penfs.so
rem 使用 Arm GNU Toolchain（arm-none-eabi，Windows 版）：
rem 工具链位置：D:\a\tools\arm-gnu\arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi
rem 编译为 ARM 状态(与设备 EABI 一致)、cortex-a7 硬浮点、-nostdlib 零链接依赖
rem （所有符号运行时由设备宿主解析，见 penjsapi.c 顶部注释）
setlocal
set "TC=D:\a\tools\arm-gnu\arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi\bin"
if not exist "%TC%\arm-none-eabi-gcc.exe" (
  echo [错误] 找不到交叉编译器 %TC%\arm-none-eabi-gcc.exe
  echo        请先解压 Arm GNU Toolchain 到 D:\a\tools\arm-gnu\
  pause
  exit /b 1
)
cd /d %~dp0
"%TC%\arm-none-eabi-gcc.exe" -marm -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -fPIC -fno-unwind-tables -fno-asynchronous-unwind-tables -O2 -Wall -I . -c penjsapi.c -o penjsapi.o
if errorlevel 1 ( echo 编译失败 & pause & exit /b 1 )
"%TC%\arm-none-eabi-gcc.exe" -shared -nostdlib -o ..\libs\libjsapi_penfs.so penjsapi.o
if errorlevel 1 ( echo 链接失败 & pause & exit /b 1 )
del penjsapi.o
echo 完成：..\libs\libjsapi_penfs.so
pause
