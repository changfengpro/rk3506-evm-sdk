#!/bin/bash

# ==============================================================================
# Vanxoak RK3506 SDK 自动化提取与打补丁脚本 (增量更新版)
# 运行前请确保当前处于 vanxoak_rk3506_board_support 根目录！
# ==============================================================================

BSP_DIR=$(pwd)
# 假设 SDK 目录与你的 BSP 备份目录同级，请根据实际情况修改此路径
SDK_DIR=$(realpath "../rk3506_linux6.1_sdk_v1.2.0")

echo "====================================================="
echo "  🚀 RK3506 BSP 高级自动化增量打包与补丁提取工具"
echo "  目标 SDK 路径: $SDK_DIR"
echo "====================================================="

# 1. 模式选择
echo ""
echo "请选择打包模式："
echo "  [a] 全自动扫描：自动检查所有目录并更新补丁包。"
echo "  [o] 逐个确认：每次检查目录前，先问你是否要处理该目录。"
read -p "🤔 你的选择 [a/O] (默认 O): " PACK_MODE
PACK_MODE=${PACK_MODE:-o}

echo -e "\n-----------------------------------------------------"

# 2. 定义处理 Git 组件的函数
process_git_component() {
    local comp_name=$1
    local sdk_path=$2
    local bsp_patch_path=$3
    
    local patch_dir="$BSP_DIR/$bsp_patch_path"
    local base_record_file="$patch_dir/.sdk_base_hash"
    local last_record_file="$patch_dir/.sdk_last_extracted_commit"

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
    
    # 步骤 A：检查并处理未暂存/未提交的工作区修改
    if [ -n "$(git status --porcelain)" ]; then
        echo "   🚨 侦测到 $comp_name 工作区有未提交的代码修改！"
        while true; do
            read -p "   📝 请输入 $comp_name 的新 Commit 描述: " MSG
            if [ -n "$MSG" ]; then
                break
            else
                echo "   ❌ 描述不能为空，原厂级管理不允许空提交！"
            fi
        done
        git add .
        git commit -m "$MSG"
    fi

    # 步骤 B：获取 SDK 初始起点 (Base Commit)
    mkdir -p "$patch_dir"
    local current_head
    current_head=$(git rev-parse HEAD)
    local base_hash

    if [ -f "$base_record_file" ]; then
        base_hash=$(cat "$base_record_file")
    else
        # 首次运行，尝试自动寻找 SDK 的原始起点（通常是本地分支与上游 remote 分支的交叉点）
        base_hash=$(git merge-base HEAD @{u} 2>/dev/null)
        
        if [ -z "$base_hash" ]; then
            # 如果没关联上游分支，要求手动输入
            echo "   ⚠️ 无法自动检测到 $comp_name 的 SDK 初始节点 (Base Commit)。"
            echo "   请通过 'git log' 找到你第一次修改前的那条原厂 Commit Hash 并粘贴到下方："
            read -p "   > " base_hash
            
            # 校验输入的 hash 是否有效
            if ! git cat-file -e "$base_hash^{commit}" 2>/dev/null; then
                echo "   ❌ 无效的 Commit Hash，跳过此组件。"
                cd "$BSP_DIR" || exit
                return
            fi
        fi
        
        echo "$base_hash" > "$base_record_file"
        echo "   📌 已永久记录 $comp_name 的起点 Commit: ${base_hash:0:8}"
    fi

    # 步骤 C：增量提取补丁（新逻辑）
    local since_hash
    if [ -f "$last_record_file" ]; then
        since_hash=$(cat "$last_record_file")
        # 检查 since_hash 是不是 current_head 的祖先
        if ! git merge-base --is-ancestor "$since_hash" "$current_head"; then
            echo "   ⚠️ 上次记录的 commit (${since_hash:0:8}) 不是当前 HEAD 的祖先，将回退到 base_hash 重新提取。"
            since_hash="$base_hash"
        fi
    else
        # 没有上次记录，则从 base_hash 开始提取全部
        since_hash="$base_hash"
    fi

    if [ "$since_hash" == "$current_head" ]; then
        echo "   💤 $comp_name 没有新的提交，无需生成补丁。"
    else
        echo "   🔄 正在提取 $since_hash..$current_head 之间的新提交..."
        
        # 生成补丁到临时目录，避免文件名冲突
        local tmp_patch_dir
        tmp_patch_dir=$(mktemp -d)
        git format-patch "$since_hash"..HEAD -o "$tmp_patch_dir/" > /dev/null
        
        local new_count
        new_count=$(ls -1 "$tmp_patch_dir"/*.patch 2>/dev/null | wc -l)
        
        if [ "$new_count" -gt 0 ]; then
            # 移动到正式补丁目录（直接追加，不删除旧补丁）
            mv "$tmp_patch_dir"/*.patch "$patch_dir/"
            echo "   ✅ 已添加 $new_count 个新补丁到 $bsp_patch_path"
        else
            echo "   💤 没有新补丁生成。"
        fi
        rm -rf "$tmp_patch_dir"
    fi

    # 更新最后提取点
    echo "$current_head" > "$last_record_file"
    echo "   📌 已记录当前提取点: ${current_head:0:8}"
    
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
    echo "🚨 发现 BSP 备份库有补丁更新或配置变动！"
    while true; do
        read -p "📝 请输入整个 BSP 仓库的总 Commit 描述: " BSP_MSG
        if [ -n "$BSP_MSG" ]; then
            break
        else
            echo "❌ 总仓库 Commit 不能为空！"
        fi
    done
    
    git commit -m "$BSP_MSG"
    echo -e "\n🎉 打包圆满完成！本地 BSP 库已更新。"
    echo "💡 别忘了执行 'git push'！"
else
    echo "🤔 你的 BSP 库没有任何变动，无需提交。"
fi

echo "====================================================="
