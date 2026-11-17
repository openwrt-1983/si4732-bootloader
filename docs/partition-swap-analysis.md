# 分区槽位对调技术分析

本次改动：把 `zhang-chong` 从后面的槽挪到前面的槽（`ota_0`），`Bright-Heart` 挪到后面的大槽（`ota_1`）。本文解释为什么只能这么分、哪些固件必须重编、哪些不用，以及刷机地址怎么变。

## 一、背景与约束

ESP32-S3 的 app 分区通过 subtype（`ota_0` / `ota_1`）标识，bootloader 依赖分区表 + otadata 决定启动哪一个。本项目的物理约束是 **16MB flash**，需要同时容纳：

- 定制 bootloader（0x0）+ 分区表（0x8000）+ otadata（0xe000）
- factory 启动菜单（1MB @ 0x10000）
- 两个固件槽（`ota_0`、`ota_1`）
- 两套文件系统：SPIFFS（`zhang-chong` 用）、LittleFS（`Bright-Heart` 用），各 2.25MB
- coredump（64KB）

两个文件系统分区和 factory 菜单加起来已经占了 5.5MB，留给两个固件槽的总空间固定为 **10MB**。这是硬上限，怎么切分都只能在这 10MB 里腾挪。

## 二、为什么分区大小必须跟着固件走

两个固件的实际大小：

| 固件 | 大小 | 需求 |
|---|---|---|
| Bright-Heart | 4747440 B ≈ 4.53 MB | 槽必须 ≥ 4.53MB |
| zhang-chong | 3672432 B ≈ 3.50 MB | 槽 4MB 足够 |

用户要求把 `zhang-chong` 放到前面（`ota_0`）。这里有个关键陷阱：**不能简单地把原来的槽大小留在原位、只换固件**。

如果强行让前面的 `ota_0` 保持 6MB 给 `zhang-chong`、后面的 `ota_1` 压到 4MB 给 `Bright-Heart`，那么 Bright-Heart 的 4.53MB 装不进 4MB 槽，启动会被 bootloader 拒绝。所以技术上只有一种可行解：

- **前面的 `ota_0` = 4MB → zhang-chong**（3.5MB 塞得进）
- **后面的 `ota_1` = 6MB → Bright-Heart**（4.53MB 必须要有 6MB）

即分区大小必须随固件一起搬，而不是留在原地址。

## 三、最终分区布局

| 地址 | 大小 | 分区 | 说明 |
|---|---|---|---|
| 0x9000 | 20KB | nvs | 不变 |
| 0xe000 | 8KB | otadata | 不变 |
| 0x10000 | 1MB | factory | 启动菜单，不变 |
| **0x110000** | **4MB** | **ota_0** | **zhang-chong（放到前面）** |
| **0x510000** | **6MB** | **ota_1** | **Bright-Heart（挪到后面大槽）** |
| 0xb10000 | 2.25MB | spiffs | 不变 |
| 0xd50000 | 2.25MB | littlefs | 不变 |
| 0xff0000 | 64KB | coredump | 不变 |

两个 app 分区起始地址（0x110000、0x510000）都是 0x10000 整数倍，满足 ESP32 分区对齐要求。ota_1 结束于 0xb10000，正好接上 spiffs 起点，无重叠、无空洞。

## 四、为什么只有菜单固件需要重新编译

这是本次改动最容易被误解的地方，分三类分别说明。

### 4.1 两个闭源固件不用重编

ESP32 的 app 镜像是**位置无关**的。编译生成的 bin 里记录的是段数据和相对地址，真正映射到哪一物理地址，由 bootloader 读取分区表的 offset 后配置 MMU 决定。同一个 app bin 放到 ota_0 或 ota_1 都能正常启动——这正是 OTA 双槽机制的基础（同一个固件先写进非活动槽，再切换启动）。

因此把 `fw-zhang-chong.bin` 从 0x710000 搬到 0x110000、`fw-bright-heart.bin` 从 0x110000 搬到 0x510000，二进制内容一个字节都不用动。

### 4.2 菜单固件必须重编

菜单源码里用了一张写死的映射表，把卡片 index 对应到 subtype：

```c
static const esp_partition_subtype_t FW_SUB[2] = {
    ESP_PARTITION_SUBTYPE_APP_OTA_1,   // Bright-Heart -> ota_1 @ 0x510000 (6MB)
    ESP_PARTITION_SUBTYPE_APP_OTA_0    // zhang-chong  -> ota_0 @ 0x110000 (4MB)
};
```

index 0 = 左卡片（Bright-Heart），index 1 = 右卡片（zhang-chong）。槽位对调后，如果这张表不改，点左卡片会通过 `esp_partition_find_first(..., OTA_0, ...)` 找到 zhang-chong，出现「选 A 启动 B」的错乱。所以映射必须改成 `{OTA_1, OTA_0}` 并重新编译。

注意卡片的**左右顺序没有变**（左仍是 Bright-Heart、右是 zhang-chong），变的只是每个卡片背后指向的 subtype。用户的交互习惯保持一致。

### 4.3 定制 bootloader 不用重编

定制 bootloader 的核心逻辑是：

```c
soc_reset_reason_t rst = (soc_reset_reason_t)esp_rom_get_reset_reason(0);
bool follow_otadata = (rst == RESET_REASON_CORE_SW) ||
                      (rst == RESET_REASON_CPU0_SW) ||
                      (rst == RESET_REASON_CORE_DEEP_SLEEP);

if (!follow_otadata) {
    if (bs->factory.offset != 0 && bs->factory.size != 0) {
        return FACTORY_INDEX;   // 真正冷启动 -> 进 factory 菜单
    }
}
return bootloader_utility_get_selected_boot_partition(bs);  // 软件重启/深睡唤醒 -> 按 otadata 选槽
```

> 上面是 v2.4 当前逻辑。本次槽位对调发生时还是旧版（`software_restart` 只含 CORE_SW / CPU0_SW），结论“槽位对调无需重编 bootloader”依然成立；v2.4 额外把深度休眠唤醒纳入 `follow_otadata`，与槽位绑定无关。

它只关心两件事：真正冷启动进 `factory`（按 `bs->factory` 动态定位）、软件重启/深睡唤醒按 `otadata` 选槽（由 `esp_ota_set_boot_partition()` 写入的分区指针决定）。两者都不写死具体地址或 subtype 到固件的绑定，所以槽位对调对它透明，无需重编。

## 五、烧录地址变化

本次两个固件的烧录地址**都变了**（不只是 zhang-chong）：

| 文件 | 旧地址 | 新地址 | 是否变化 |
|---|---|---|---|
| bootloader.bin | 0x0 | 0x0 | 否 |
| partitions.bin | 0x8000 | 0x8000 | 内容变（必须重刷） |
| otadata-menu.bin | 0xe000 | 0xe000 | 否 |
| boot-selector.bin | 0x10000 | 0x10000 | 内容变（重编，必须重刷） |
| fw-zhang-chong.bin | 0x710000 | **0x110000** | **变** |
| fw-bright-heart.bin | 0x110000 | **0x510000** | **变** |

建议整包全刷，避免新旧固件残留在错误地址。

## 六、风险与待确认项

1. **新固件的真实大小仍未确认**。本次调整假设新的 Bright-Heart 固件 ≤ 6MB（6291456 字节）。若超过 6MB，6MB 槽仍然装不下，需要重新评估分区策略（例如压缩文件系统分区）。
2. **zhang-chong 槽只剩 4MB**，当前固件 3.5MB，余量 0.5MB。后续若 zhang-chong 也更新变大，会撞到同样的墙。
3. **刷机报错的原始信息未提供**。此前「换固件刷不进去」的具体报错（写入阶段报错 / 网页 OTA 提示空间不足 / 启动失败）未拿到，本文的容量分析基于分区约束推断，不能替代对报错原文的定位。

## 七、验证结论

- 分区表二进制解码确认：`ota_0,app,ota_0,0x110000,4M` / `ota_1,app,ota_1,0x510000,6M`，后续分区地址不变。
- 两个 app 分区 0x10000 对齐，ota_1 结束地址与 spiffs 起点无缝衔接。
- 菜单固件重编产物镜像头 `mode=2 sp=0x4f`，与两个原厂固件一致。
- 一键烧录脚本、README 地址表、源码映射表三处地址已同步。
- zip 包解包后 7 个 bin 的 SHA256 逐项复核通过。
