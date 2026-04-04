#!/bin/bash

# ==============================================================================
# Vanxoak RK3506 SDK 自动化提取与打补丁脚本 (高级交互版)
# 运行前请确保当前处于 vanxoak_rk3506_board_support 根目录！
# ==============================================================================

BSP_DIR=$(pwd)
# 假设 SDK 目录与你的 BSP 备份目录同级，请根据实际情况修改此路径
SDK_DIR=$(realpath "../rk3506_linux6.1_sdk_v1.2.0")

echo "====================================================="
echo "  🚀 RK3506 BSP 高级自动化打包与补丁提取工具"
echo "  目标 SDK 路径: $SDK_DIR"
echo "====================================================="

# 1. 模式选择
echo ""
echo "请选择打包模式："
echo "  [a] 全自动扫描：自动检查所有目录，只有发现修改时才让你输入 Commit。"
echo "  [o] 逐个确认：每次检查目录前，先问你是否要处理该目录。"
read -p "🤔 你的选择 [a/O] (默认 O): " PACK_MODE
PACK_MODE=${PACK_MODE:-o}

echo -e "\n-----------------------------------------------------"

# 2. 定义处理 Git 组件的函数
# 参数 1: 组件名称 (如 U-Boot)
# 参数 2: SDK 中的相对路径
# 参数 3: BSP 备份库中的补丁存放路径
process_git_component() {
    local comp_name=$1
    local sdk_path=$2
    local bsp_patch_path=$3

    echo -e "\n▶️ 正在处理组件: 【 $comp_name 】"

    # 如果是“逐个确认”模式，先询问是否跳过
    if [[ "$PACK_MODE" != "a" && "$PACK_MODE" != "A" ]]; then
        read -p "❓ 是否检查并打包 $comp_name ? [Y/n]: " DO_PACK
        DO_PACK=${DO_PACK:-y}
        if [[ "$DO_PACK" != "y" && "$DO_PACK" != "Y" ]]; then
            echo "   ⏭️ 已跳过 $comp_name。"
            return
        fi
    fi

    if [ ! -d "$SDK_DIR/$sdk_path" ]; then
        echo "   ⚠️  路径不存在: $SDK_DIR/$sdk_path，自动跳过。"
        return
    fi

    cd "$SDK_DIR/$sdk_path" || exit
    
    # 检查是否有未提交的修改 (包含已暂存和未暂存的)
    if [ -n "$(git status --porcelain)" ]; then
        echo "   🚨 侦测到 $comp_name 有未提交的代码修改！"
        
        # 强制要求输入对应的 Commit
        while true; do
            read -p "   📝 请输入 $comp_name 的 Commit 描述: " MSG
            if [ -n "$MSG" ]; then
                break
            else
                echo "   ❌ 描述不能为空，原厂级管理不允许空提交！"
            fi
        done
        
        git add .
        git commit -m "$MSG"
        
        # 确保备份库的 patch 文件夹存在
        mkdir -p "$BSP_DIR/$bsp_patch_path"
        
        # 提取最近一次 commit 为 patch
        git format-patch -1 HEAD -o "$BSP_DIR/$bsp_patch_path/" > /dev/null
        echo "   ✅ $comp_name 补丁已成功保存至: $bsp_patch_path"
    else
        echo "   💤 $comp_name 工作区很干净，无修改，跳过。"
    fi
    
    # 返回 BSP 目录
    cd "$BSP_DIR" || exit
}

# 3. 依次处理各个代码组件
process_git_component "U-Boot / SPL" "u-boot" "u-boot/patches"
process_git_component "Linux Kernel" "kernel" "kernel/patches"
process_git_component "HAL (M0 固件)" "hal" "hal_mcu/patches"
process_git_component "Device (AMP 镜像配置)" "device/rockchip" "device_rockchip/patches"
process_git_component "Buildroot" "buildroot" "buildroot/patches"

# 4. 同步静态配置文件
echo -e "\n-----------------------------------------------------"
echo "📂 正在同步全局静态配置文件 (Configs & DTS)..."

cp -r "$SDK_DIR/kernel/arch/arm/configs/"rk3506* "$BSP_DIR/kernel/configs/" 2>/dev/null
cp -r "$SDK_DIR/kernel/arch/arm/configs/"vanxoak* "$BSP_DIR/kernel/configs/" 2>/dev/null
cp -r "$SDK_DIR/kernel/arch/arm/boot/dts/"vanxoak* "$BSP_DIR/kernel/dts/" 2>/dev/null
cp -r "$SDK_DIR/u-boot/configs/"vanxoak* "$BSP_DIR/u-boot/configs/" 2>/dev/null
cp -r "$SDK_DIR/buildroot/configs/"*vanxoak* "$BSP_DIR/buildroot/configs/" 2>/dev/null
cp -r "$SDK_DIR/output/.config" "$BSP_DIR/device_rockchip/global_config.bak" 2>/dev/null

echo "✅ 静态配置同步完成。"

# 5. 提交你的 BSP 备份仓库
echo -e "\n-----------------------------------------------------"
echo "📦 正在检查总 BSP 备份库状态..."
cd "$BSP_DIR" || exit
git add .

if [ -n "$(git status --porcelain)" ]; then
    echo "🚨 发现 BSP 备份库有新的补丁或配置更新！"
    while true; do
        read -p "📝 请输入整个 BSP 仓库的总 Commit 描述 (例如: sync latest patches): " BSP_MSG
        if [ -n "$BSP_MSG" ]; then
            break
        else
            echo "❌ 总仓库 Commit 不能为空！"
        fi
    done
    
    git commit -m "$BSP_MSG"
    echo -e "\n🎉 打包圆满完成！本地 BSP 库已更新。"
    echo "💡 别忘了执行 'git push' 将你的心血推送到云端！"
else
    echo "🤔 你的 BSP 库没有任何新文件或补丁产生，无需提交。"
fi

echo "====================================================="
