# ESP32-S3 SI4732 收音机 · 双固件开机菜单（每次开机都选）

开机显示两个固件卡片，旋转 EC11 选择，按下去启动，或 5 秒无操作自动启动当前选中项。
**每次断电再上电都会回到这个菜单**，并默认高亮上一次选过的固件。

卡片标签显示 `OTA_0` / `OTA_1`，使用 44px 原生 1bpp 点阵字库（`FontBig.h`，由 `tools/gen_big_font.py` 生成），字号比上一版明显放大：文字宽 122px，在 144px 卡片里左右各留 11px，水平居中。标题、提示等小字仍用 LovyanGFX 内置 DejaVu 矢量字库。

## 一、交互规则

| 操作 | 行为 |
|---|---|
| 顺时针旋转 | 选中右边的卡片 `OTA_1`（6MB 槽，Bright-Heart） |
| 逆时针旋转 | 选中左边的卡片 `OTA_0`（4MB 槽，zhang-chong） |
| 旋转到最左 / 最右 | 停住，**不循环** |
| 短按编码器（小于 2 秒） | 立即启动当前选中的固件 |
| 长按编码器（2 秒及以上） | **忽略，不启动、不重启**（已关闭长按触发） |
| 5 秒无任何操作 | 自动启动当前选中的固件 |

- 左卡片标签 = `OTA_0`，启动 `ota_0` 槽的 zhang-chong（0x110000，4MB 槽）——**默认高亮这一张**
- 右卡片标签 = `OTA_1`，启动 `ota_1` 槽的 Bright-Heart（0x510000，6MB 大槽）
- 卡片下方的小字 `4MB` / `6MB` 标明对应槽位大小；标签只是显示名，底层仍按分区 subtype 找到正确的固件
- 每次进菜单默认高亮**上一次启动过的固件**（记录在 NVS 的 `bootsel/choice2`）；第一次使用默认高亮左卡片（OTA_0）
- 注：卡片顺序对调时把 NVS key 从 `choice` 换成了 `choice2`，因此记忆从 zhang-chong/OTA_0 重新开始

## 二、为什么这次每次开机都会进菜单

这一版换了**定制 bootloader**（`flash/bootloader.bin`），它按复位原因区分两种启动：

| 场景 | 复位原因 | bootloader 行为 |
|---|---|---|
| 断电后重新上电（关开关再开） | 冷启动（CHIP_POWER_ON 等） | 强制进入 `factory` 的开机菜单 |
| 菜单里选中后 `esp_restart()` | 软件复位（CORE_SW / CPU0_SW） | 按 `otadata` 进入选中的 OTA 槽 |
| zhang-chong 长按休眠后唤醒 | 深度休眠唤醒（CORE_DEEP_SLEEP / 0x05） | 按 `otadata` 直接回到原固件，不再弹回菜单 |

流程：

1. 上电 → 定制 bootloader 判定冷启动 → 进菜单
2. 菜单读取 NVS，高亮上次选择；你旋转 / 按下 / 超时 5 秒
3. 菜单写入 `otadata` 指向所选固件，同时把选择写进 NVS，然后 `esp_restart()`
4. 软件重启 → bootloader 按 `otadata` 进所选固件
5. 下次断电再上电 → 回到第 1 步，菜单仍然出现，且高亮上次那一个

> 关键：**必须把 `0x0` 的 `bootloader.bin` 一起重刷成新版**，否则还是旧的 Arduino bootloader，就会退回"只有第一次有菜单"的旧行为。

## 三、烧录文件（都在 `flash/` 目录）

| 文件 | 大小 | 内容 |
|---|---|---|
| `bootloader.bin` | 约 20 KB | **定制 bootloader**（ESP-IDF 编译，冷启动进 factory 菜单） |
| `partitions.bin` | 3 KB | 合并分区表 |
| `boot-selector.bin` | 约 323 KB | 开机菜单启动器（factory 分区），含 NVS 记忆、屏幕修正、DejaVu 清晰字体 |
| `fw-bright-heart.bin` | 4.6 MB | Bright-Heart 固件 |
| `fw-zhang-chong.bin` | 3.6 MB | zhang-chong 固件 |
| `otadata-menu.bin` | 8 KB | 全 0xFF，首次烧录后先进菜单 |
| `otadata-normal.bin` | 8 KB | 备用：从原包提取的 otadata（正常启动用，一般用不到） |

## 四、地址表（16MB flash）

| 地址 | 大小 | 烧录文件 | 说明 |
|---|---|---|---|
| `0x0000` | 20 KB | `bootloader.bin` | **定制** bootloader（必须重刷） |
| `0x8000` | 3 KB | `partitions.bin` | 分区表 |
| `0xe000` | 8 KB | `otadata-menu.bin` | OTA 数据；全 FF = 无有效槽 |
| `0x10000` | 1 MB | `boot-selector.bin` | factory：开机菜单 |
| `0x110000` | 4 MB | `fw-zhang-chong.bin` | ota_0：zhang-chong（放到前面，4MB 槽） |
| `0x510000` | 6 MB | `fw-bright-heart.bin` | ota_1：Bright-Heart（挪到后面，6MB 大槽） |
| `0xb10000` | 2.25 MB | — | SPIFFS（zhang-chong 用，subtype 0x82） |
| `0xd50000` | 2.25 MB | — | LittleFS（Bright-Heart 用，subtype 0x83） |
| `0xff0000` | 64 KB | — | coredump |

两个文件系统分区首次启动会自动格式化，不用烧。

## 五、完整烧录命令

把 `/dev/ttyUSB0` 换成你的实际串口：

```bash
python esptool.py --chip esp32s3 --port /dev/ttyUSB0 erase_flash

python esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 921600 write_flash \
  0x0       bootloader.bin \
  0x8000    partitions.bin \
  0xe000    otadata-menu.bin \
  0x10000   boot-selector.bin \
  0x510000  fw-bright-heart.bin \
  0x110000  fw-zhang-chong.bin
```

Windows / Linux / Mac 可直接用 `flash/` 里的一键脚本。烧完断电重上电，应看到菜单。

## 五之二、日常升级：只刷两个固件即可 ✅

bootloader、分区表、菜单、otadata 都不变的前提下，以后更新收音机固件**只需重刷对应那一个**：

| 升级对象 | 烧录地址 | 槽位上限 |
|---|---|---|
| zhang-chong | `0x110000` | 4 MB（4,194,304 字节） |
| Bright-Heart | `0x510000` | 6 MB（6,291,456 字节） |

**Releases 里以后升级只下载这两个文件即可：`fw-zhang-chong_0x110000.bin` 和 `fw-bright-heart_0x510000.bin`。**

> 从 v2.3 及更早版本升到 v2.4 需要先重刷一次 `bootloader_0x0.bin`（修复深度休眠唤醒被误判为冷启动）；如果上一版"短按才启动、长按忽略"的菜单也没刷过，再刷一次 `boot-selector_0x10000.bin`。完成这次一次性升级后，后续日常更新才只需上面两个固件。

```bash
python -m esptool --chip esp32s3 --port /dev/ttyUSB0 write_flash 0x110000 fw-zhang-chong_0x110000.bin
python -m esptool --chip esp32s3 --port /dev/ttyUSB0 write_flash 0x510000 fw-bright-heart_0x510000.bin
```

⚠️ 硬约束：新固件必须 ≤ 对应槽位（超了写入会"成功"但启动校验失败）；必须是 ESP32-S3 的纯 app bin（镜像头 `e9 ?? 02 4f`），**不要**刷从 0x0 开始的 merged 镜像；不要动 `0x0`/`0x8000`/`0xe000`/`0x10000`。只有换 bootloader、改分区或升级菜单时才需要整包全刷。完整分析见 `docs/firmware-upgrade-guide.md`。

## 六、屏幕花屏 / 乱码的说明

这块屏是 **170×320 ST7789、8 位 8080 并口**（不是 SPI）。本版按原理图与 LilyGo 官方配置做了修正：

- 并口写频率从 20 MHz 降到 **10 MHz**（第一屏乱码主要就是这个）
- `offset_x = 35`（170 宽是 240 宽芯片的裁切）
- `invert = true`（官方 `TFT_INVERSION_ON`）
- `rgb_order = false`（官方 `TFT_RGB`）

如果仍有花屏，优先检查排线连接，再确认 `LGFX_Config.h` 里上述几项没被改过。

## 七、旋转方向不对怎么办

EC11 的 A/B 相序可能焊反。如果顺时针/逆时针效果与描述相反，改源码里的开关后重新编译菜单：

```cpp
#define EC11_REVERSE  1     // 原来是 0，改成 1
```

引脚来自原理图 V1.2：EC11 `A=2 B=1 SW=21`；屏幕 `D0-D7=39/40/41/42/45/46/47/48`，`WR=8 RD=9 DC=7 CS=6 RES=5 BL=38`。

## 八、编译方式（备查）

菜单 app（Arduino）：

```bash
arduino-cli compile \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,FlashMode=qio,PartitionScheme=custom,PSRAM=disabled,UploadSpeed=921600' \
  --output-dir out radio-bootloader.ino
```

定制 bootloader（ESP-IDF，`custom-bootloader/`）：基于官方 `examples/custom_bootloader/bootloader_override`，在 `bootloader_components/main/bootloader_start.c` 里按 `esp_rom_get_reset_reason(0)` 判断：真正冷启动 → `FACTORY_INDEX`；软件复位（CORE_SW / CPU0_SW）和深度休眠唤醒（CORE_DEEP_SLEEP）→ 走 `otadata`，避免 zhang-chong 长按休眠唤醒后弹回菜单。
