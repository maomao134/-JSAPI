@echo off
rem 构建 .amr 安装包（包含 libs\libjsapi_penfs.so 自定义 JSAPI）
rem 首次使用先运行 native\build-native.bat 生成 libs\libjsapi_penfs.so
rem 使用便携版 Node 18（框架在 Node 22+ 上有兼容问题）
set "NODE18=D:\a\tools\node18\node-v18.20.8-win-x64"
set "PATH=%NODE18%;%PATH%"
if not exist "libs\libjsapi_penfs.so" (
  echo [警告] 未找到 libs\libjsapi_penfs.so，请先运行 native\build-native.bat
  pause
  exit /b 1
)
"%NODE18%\node.exe" "%NODE18%\node_modules\aiot-vue-cli\src\cli.js" -c -q -p
pause
