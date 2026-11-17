# 双固件开机菜单 · 架构分析与升级指南

适用仓库：`openwrt-1983/si4732-bootloader`
硬件：ESP32-S3 + ST7789 170×320（8 位并口）+ EC11 编码器，16MB flash
当前发布：**v2.4**（修复深度休眠唤醒误判为冷启动；菜单只认短按）

---

## 一、这套系统是怎么搭起来的

两个收音机固件（Bright-Heart、zhang-chong）都是**闭源二进制**，拿不到源码，没法往里面插入"长按弹菜单"之类的功能。所以整体思路是：**不动这两个固件，在它们外面套一层自己的启动器**。

系统由四层组成，各司其职：

| 层 | 烧录地址 | 作用 |
|---|---|---|
| 定制 bootloader | `0x0` | 判断复位原因：真正冷启动进菜单；软件重启和深度休眠唤醒都按 otadata 进原固件 |
| 分区表 | `0x8000` | 把 16MB flash 切成菜单区 + 两个固件槽 + 文件系统 |
| otadata | `0xe000` | 记录"当前该启动哪个 app 分区"，由菜单在启动时写入 |
| factory 菜单 | `0x10000` | 显示两张卡片、EC11 选择、写 otadata、重启 |
| 固件 A / B | `0x110000` / `0x510000` | 两个闭源收音机固件，原样烧录 |

### 为什么"关电再开"每次都能进菜单

这是整套设计的关键，靠**复位原因**区分：

- **冷启动**（断电后重新上电，`CHIP_POWER_ON` 等）→ 定制 bootloader 强制跳到 `factory` 分区的菜单。
- **软件复位**（菜单里 `esp_restart()`，`CORE_SW` / `CPU0_SW`）→ bootloader 按 otadata 进入你选中的固件。
- **深度休眠唤醒**（`CORE_DEEP_SLEEP` / `0x05`，例如 zhang-chong 长按休眠后再唤醒）→ 同样按 otadata 直接回到原固件，不再弹回菜单。

标准 Arduino bootloader 只看 otadata，不会区分复位原因，所以第一版才会"只有第一次有菜单"。换成基于 ESP-IDF `bootloader_override` 示例编译的定制 bootloader 后解决了这个问题。

### 菜单的选择记忆

菜单把选择写进 NVS（`bootsel` 命名空间、`choice2` 键），下次进菜单默认高亮上次那一个。
键名从 `choice` 换成 `choice2` 是因为卡片顺序对调过——旧键里的 0/1 语义会反转，换键让它自然失效一次。

---

## 二、分区布局（16MB flash）

```
0x00000  bootloader        20 KB    定制，按复位原因分流
0x08000  partition table    3 KB     6+4 布局
0x0e000  otadata            8 KB     全 FF，首次进菜单
0x10000  factory           1 MB     开机菜单（当前 331 KB）
0x110000 ota_0             4 MB     zhang-chong   ← 默认高亮/自动启动
0x510000 ota_1             6 MB     Bright-Heart
0xb10000 spiffs          2.25 MB
0xd50000 littlefs        2.25 MB
0xff0000 coredump          64 KB
```

**两个槽加起来 10MB**。zhang-chong 在前面用 4MB 小槽，Bright-Heart 用 6MB 大槽——反过来分配的话 Bright-Heart（4.53MB）塞不进 4MB，所以只有这一种分法可行。

后面的 spiffs / littlefs / coredump 地址从第一版起就没动过，文件系统里已存的数据不受分区调整影响。

---

## 三、字体优化（v2.3）

原来的菜单用 LovyanGFX 默认 **5×7 点阵**字体配合 `setTextSize(2)` 放大 2 倍：

- 点阵放大 = 像素块放大，必然**发虚**；
- "Bright-Heart" 放大后约 **144px**，卡片内宽只有 146px，几乎**贴边**。

v2.3 改用 LovyanGFX **内置 DejaVu 矢量字库**，全部 **1 倍尺寸**（不放大）：

| 用途 | 字体 | 实测宽度 |
|---|---|---|
| 固件名 | DejaVu18 | "Bright-Heart" = 123px，卡片 144px → 两侧各约 10px 边距 |
| 标题 / 槽号 | DejaVu12 | — |
| 倒计时 | DejaVu9 | 文案精简后约 232px，避免超出 320px 屏 |

同时把卡片从 `146px` 调成 `144px`、中间留 12px 缝，图标缩小上移给文字让位。

字体是库内置的（`&fonts::DejaVu18` 等），不增加外部文件，代价是固件从 324KB 增到 331KB——1MB 的 factory 分区完全放得下。

---

## 四、以后升级：只需要刷两个固件 ✅

这是本版的重点结论。因为 **bootloader、分区表、菜单、otadata 全都不变**，日常更新收音机固件时，只需把新固件写到它对应的 app 分区：

| 要升级的固件 | 烧录地址 | Release 里的文件名 | 槽位上限 |
|---|---|---|---|
| zhang-chong | `0x110000` | `fw-zhang-chong_0x110000.bin` | 4 MB = 4,194,304 字节 |
| Bright-Heart | `0x510000` | `fw-bright-heart_0x510000.bin` | 6 MB = 6,291,456 字节 |

### 命令示例（Linux / macOS）

```bash
# 只升级 zhang-chong
python -m esptool --chip esp32s3 --port /dev/ttyUSB0 write_flash 0x110000 fw-zhang-chong_0x110000.bin

# 只升级 Bright-Heart
python -m esptool --chip esp32s3 --port /dev/ttyUSB0 write_flash 0x510000 fw-bright-heart_0x510000.bin
```

Windows 把端口换成 `COMx` 即可。刷完断电再开，仍然进菜单，选择照常。

### ⚠️ 三个硬约束（不满足就会刷坏）

1. **新固件体积不能超槽位**。zhang-chong 必须 ≤ 4MB，Bright-Heart 必须 ≤ 6MB。esptool 是按地址硬写的、不会检查分区边界，超了会显示"写入成功"但启动时 bootloader 校验镜像越界、拒绝启动。之前遇到"新固件刷不进去"，根因就是旧布局的槽只有 5MB。刷之前先 `ls -l` 看一下字节数。

2. **必须是 ESP32-S3 的 app 镜像**（不是整片 merged bin）。用Arduino 编译时选 esp32-s3、FlashMode=qio、FlashSize=16M，取输出的 `.ino.bin`。镜像头前 4 字节应是 `e9 ?? 02 4f`（magic=0xe9、mode=2、sp=0x4f），和现有固件一致。**千万不要**把从 0x0 开始的 `*.merged.bin` 刷到 0x110000/0x510000，那会覆盖 bootloader。

3. **不要动 `0x0`、`0x8000`、`0xe000`、`0x10000`**。这四个区是启动器和分区定义，升级固件时保持原样。只有整套重刷或改布局时才需要碰它们。

### 什么情况下必须整包全刷

- 换了 bootloader（比如以后又调整启动逻辑）
- 改了分区表（比如某个固件长大到需要重新分配槽位）
- 菜单功能本身升级（比如再加新交互）
- 第一次从原厂固件迁移到本方案

只要上面这些没变，日常就只刷那两个固件 bin。

**Releases 里以后升级只下载这两个文件即可：`fw-zhang-chong_0x110000.bin` 和 `fw-bright-heart_0x510000.bin`。**

> v2.4 属于"换了 bootloader"：从 v2.3 及更早版本升级，需要先重刷一次 `bootloader_0x0.bin`（修复深度休眠唤醒误判）；如果也没刷过上一版"短按才启动、长按忽略"的菜单，再刷一次 `boot-selector_0x10000.bin`。这次一次性升级完成后，后续日常更新才只需上面两个固件。

---

## 五、交互规则速查

| 操作 | 行为 |
|---|---|
| 冷启动（关电再开） | 进菜单，高亮上次选过的固件（首次默认 zhang-chong） |
| 顺时针旋转 | 选右边（Bright-Heart） |
| 逆时针旋转 | 选左边（zhang-chong） |
| 转到最左 / 最右 | 停住，不循环 |
| 短按编码器（<2 秒） | 立即启动选中固件 |
| 长按编码器（≥2 秒） | 菜单里忽略，不启动、不重启 |
| 5 秒无操作 | 自动启动选中固件 |
| zhang-chong 运行中长按休眠 | 固件自带深度休眠；唤醒后由 v2.4 bootloader 直接回到该固件，不再弹回菜单 |

---

## 六、源码与编译

仓库结构：

```
custom-bootloader/   定制 bootloader（ESP-IDF 5.3.2 bootloader_override 示例）
radio-bootloader/    菜单 Arduino 工程（ino + LGFX_Config.h + partitions.csv）
flash/               一键烧录脚本 + SHA256 清单（二进制固件在 Releases）
docs/                各版技术分析文档
```

菜单固件编译命令：

```bash
arduino-cli compile \
  --fqbn "esp32:esp32:esp32s3:FlashSize=16M,FlashMode=qio,PartitionScheme=custom,PSRAM=disabled,UploadSpeed=921600" \
  radio-bootloader
```

定制 bootloader 用 ESP-IDF 5.3.2 编译，核心改动在 `bootloader_components/main/bootloader_start.c`：读复位原因，真正冷启动返回 `FACTORY_INDEX`；软件复位（CORE_SW / CPU0_SW）和深度休眠唤醒（CORE_DEEP_SLEEP）都走 `otadata`。

---

## 七、校验

Release 中 `SHA256SUMS.txt` 列出全部 7 个烧录文件的哈希：

```bash
sha256sum -c SHA256SUMS.txt
```

镜像头核对（应与现有固件一致）：

```bash
xxd -l 4 fw-zhang-chong_0x110000.bin   # e9 xx 02 4f
```
