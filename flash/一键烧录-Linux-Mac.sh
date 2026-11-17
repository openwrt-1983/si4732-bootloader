#!/usr/bin/env bash
# ESP32-S3 SI4732 收音机 - 双固件开机菜单 一键烧录
# 用法: ./一键烧录-Linux-Mac.sh [串口设备]，默认 /dev/ttyUSB0
set -e

PORT="${1:-/dev/ttyUSB0}"
cd "$(dirname "$0")"

echo "=================================================="
echo " ESP32-S3 SI4732 收音机 - 双固件开机菜单 一键烧录"
echo "=================================================="
echo "使用端口: $PORT"

if ! command -v python3 >/dev/null 2>&1 && ! command -v python >/dev/null 2>&1; then
  echo "[错误] 未找到 python3，请先安装 Python 3 与 esptool: pip install esptool"
  exit 1
fi
PY=$(command -v python3 || command -v python)

echo "正在擦除 flash ..."
"$PY" -m esptool --chip esp32s3 --port "$PORT" erase_flash

echo "正在烧录 ..."
"$PY" -m esptool --chip esp32s3 --port "$PORT" --baud 921600 write_flash \
  0x0       bootloader.bin \
  0x8000    partitions.bin \
  0xe000    otadata-menu.bin \
  0x10000   boot-selector.bin \
  0x510000  fw-bright-heart.bin \
  0x110000  fw-zhang-chong.bin

echo "=================================================="
echo "烧录完成！断电重上电，应看到两个固件的选择菜单。"
echo "=================================================="
