@echo off
chcp 65001 >nul
title ESP32-S3 双固件开机菜单 - 一键烧录
echo.
echo  ==================================================
echo   ESP32-S3 SI4732 收音机 - 双固件开机菜单 一键烧录
echo  ==================================================
echo.
echo  当前目录: %cd%
echo.
set "PORT=COM3"
set /p "PORT=请输入串口号 (直接回车 = COM3): "
echo.
echo  使用端口: %PORT%
echo  正在擦除 flash ...
python -m esptool --chip esp32s3 --port %PORT% erase_flash
if errorlevel 1 goto :err
echo.
echo  正在烧录 ...
python -m esptool --chip esp32s3 --port %PORT% --baud 921600 write_flash ^
  0x0       bootloader.bin ^
  0x8000    partitions.bin ^
  0xe000    otadata-menu.bin ^
  0x10000   boot-selector.bin ^
  0x510000  fw-bright-heart.bin ^
  0x110000  fw-zhang-chong.bin
if errorlevel 1 goto :err
echo.
echo  ==================================================
echo   烧录完成！断电重上电，应看到两个固件的选择菜单
echo  ==================================================
echo.
pause
exit /b 0

:err
echo.
echo  [错误] 烧录失败。请依次检查：
echo    1) 已安装 esptool:  pip install esptool
echo    2) 串口号填写正确（设备管理器里查看）
echo    3) 若进下载模式失败，按住板上 BOOT 键再上电
echo.
pause
exit /b 1
