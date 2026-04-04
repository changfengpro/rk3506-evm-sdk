# Vanxoak HD-RK3506G EVM AMP Board Support Package

## 📌 1. 版本与时间信息
* **备份日期：** 2026年4月
* **目标 SDK 版本：** Rockchip RK3506 Linux 6.1 SDK v1.2.0
* **核心硬件：** Vanxoak HD-RK3506G-EVM (包含 Cortex-A7 Linux + Cortex-M0 裸机 AMP 架构)
* **引导方式：** SD 卡启动 / FIT 镜像引导 (amp_mcu.its)

## 🛠️ 2. 本次备份包含的核心修改 (Changelog)

针对原厂 SDK，本项目为了支持 **M0 核心独立硬件仿真 (J-Link SWD 调试) | 修改默认调试串口为 UART1** 以及特定的板级功能，进行了以下深度定制：

### 2.1 引导层 (U-Boot / SPL)
* **修改动作：**
  1. 彻底清除了 `SGRF` (安全寄存器) 对 M0 核心调试接口的锁定 (`writel(0x00220000, 0xff960000)`)。
  2. 修改内部路由矩阵，将 M0 调试总线强行路由至 `JTAG_M1` 组 (`writel(0x00300020, 0xff288000)`)。
  3. **暴力复用 IOMUX**：将物理引脚 `GPIO0_C6` 和 `GPIO0_C7` 强制复用为 `SWCLK` 和 `SWDIO`。
  4. 修改默认 Debug UART 为 UART1。
* **资产形态：** 转化为 `.patch` 补丁存放在 `u-boot/patches/`。

### 2.2 内核层 (Kernel & DTS)
* **修改动作：**
  1. 彻底禁用 `uart0` (`status = "disabled";`)，以让出物理引脚。
  2. 在根节点 `/ {}` 中新增 `m0_debug` 虚拟设备节点，防止 Linux 内核启动阶段重新初始化 Pinctrl 导致 J-Link 被挤占断开。
* **资产形态：** 独立 `.dts/.dtsi` 文件及 `defconfig` 存放在 `kernel/`。代码修改转化为补丁存放在 `kernel/patches/`。

### 2.3 MCU 固件层 (HAL / M0) 与 异构打包 (Device)
* **修改动作：**
  1. 添加了 M0 的自定义业务逻辑源码。
  2. 提取了构建双核 FIT Image 必须的打包描述文件 `amp_mcu.its` 及环境变量。
* **资产形态：** 转化为补丁分别存放在 `hal_mcu/patches/` 和 `device_rockchip/patches/`。

---

## 🔄 3. 如何在新环境中恢复本工程？

当解压了一份全新的 Rockchip 原厂 40GB SDK 时，按以下顺序“注入”本项目的修改：

### 步骤 1：恢复静态配置文件 (Config & DTS)
```bash
# 1. 恢复全局配置
cp device_rockchip/global_config.bak ../rk3506_linux6.1_sdk_v1.2.0/output/.config
cp device_rockchip/BoardConfig*.mk ../rk3506_linux6.1_sdk_v1.2.0/device/rockchip/rk3506/ 2>/dev/null || true

# 2. 恢复内核与 U-Boot 的设备树及 defconfig
cp -r kernel/configs/* ../rk3506_linux6.1_sdk_v1.2.0/kernel/arch/arm/configs/
cp -r kernel/dts/* ../rk3506_linux6.1_sdk_v1.2.0/kernel/arch/arm/boot/dts/
cp -r u-boot/configs/* ../rk3506_linux6.1_sdk_v1.2.0/u-boot/configs/

# 3. 恢复 Buildroot 配置
cp -r buildroot/configs/* ../rk3506_linux6.1_sdk_v1.2.0/buildroot/configs/
```

### 步骤 2：打入源码修改补丁 (Patch)
*(注：如果对打补丁命令不熟悉，请参阅本项目目录下的 README_PATCH_GUIDE.md)*

```bash
# 1. 注入 U-Boot 补丁
cd ../rk3506_linux6.1_sdk_v1.2.0/u-boot
git am ../../vanxoak_rk3506_board_support/u-boot/patches/*.patch

# 2. 注入 Kernel 补丁
cd ../kernel
git am ../../vanxoak_rk3506_board_support/kernel/patches/*.patch

# 3. 注入 HAL (M0 固件) 补丁
cd ../hal
git am ../../vanxoak_rk3506_board_support/hal_mcu/patches/*.patch

# 4. 注入 Device (AMP 打包) 补丁
cd ../device/rockchip
git am ../../vanxoak_rk3506_board_support/device_rockchip/patches/*.patch
```

### 步骤 3：严格的编译顺序
1. **优先编译 M0 固件：** 进入 `hal/project/rk3506-mcu/GCC`，执行 `make` 编译出 `mcu.bin`。
2. **全局打包构建：** 返回 SDK 顶层目录，执行 `./build.sh` (或相关构建命令) 开始编译 Linux 系统
