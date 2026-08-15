@echo off
rem 安装到词典笔：先 adb push，再 miniapp_cli install 并启动
set "AMR=8002048151526021.1_0_0.amr"
adb push %AMR% /userdisk/Favorite/%AMR%
adb shell "miniapp_cli install /userdisk/Favorite/%AMR%"
adb shell "miniapp_cli start 8002048151526021"
pause
