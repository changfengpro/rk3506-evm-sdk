#!/bin/bash

# 此脚本为SD卡制作镜像，在使用此脚本前首先得先用官方的sd_firmware工具制作一遍

FW_DIR="output/firmware"
PARAMETER_FILE="$FW_DIR/parameter.txt"

echo "======================================================"
echo "               Firmware 烧录工具"
echo "======================================================"

# 1. 目标设备选择逻辑
TARGET_DEV=$1

# 如果没有通过参数传入设备路径，则启动交互式设备扫描
if [ -z "$TARGET_DEV" ]; then
    echo "未提供目标设备参数，正在扫描系统磁盘..."
    
    # 获取磁盘列表：忽略 loop, ram, rom 等虚拟设备，显示路径、大小和型号
    mapfile -t disk_list < <(lsblk -d -p -n -o NAME,SIZE,MODEL | grep -E "/dev/sd|/dev/mmcblk|/dev/vd")

    if [ ${#disk_list[@]} -eq 0 ]; then
        echo "错误: 未检测到任何可用的物理磁盘设备。"
        exit 1
    fi

    echo "------------------------------------------------------"
    echo "请选择要烧录的目标磁盘 (警告：选错磁盘会导致数据丢失！):"
    i=1
    for disk in "${disk_list[@]}"; do
        echo "  [$i] $disk"
        ((i++))
    done
    echo "  [q] 退出脚本 (Quit)"
    echo "------------------------------------------------------"
    
    read -p "请输入磁盘编号: " DISK_SEL

    if [[ "$DISK_SEL" == "q" || "$DISK_SEL" == "Q" ]]; then
        echo "操作取消。"
        exit 0
    fi

    # 验证输入是否为有效数字
    if [[ "$DISK_SEL" =~ ^[0-9]+$ ]] && [ "$DISK_SEL" -ge 1 ] && [ "$DISK_SEL" -le "${#disk_list[@]}" ]; then
        real_idx=$((DISK_SEL-1))
        # 提取用户选择的设备路径 (第一列)
        TARGET_DEV=$(echo "${disk_list[$real_idx]}" | awk '{print $1}')
    else
        echo "错误: 无效的选择 '$DISK_SEL'。"
        exit 1
    fi
fi

# 2. 环境与设备复查
if [ ! -b "$TARGET_DEV" ]; then
    echo "错误: $TARGET_DEV 不是有效的块设备。"
    exit 1
fi

if [ ! -f "$PARAMETER_FILE" ]; then
    echo "错误: 找不到分区表文件 $PARAMETER_FILE"
    exit 1
fi

echo ""
echo ">>> 已锁定目标设备: $TARGET_DEV <<<"
echo ""
echo "--- 正在解析分区表 ---"

# 3. 核心解析逻辑
cmdline=$(grep "CMDLINE" "$PARAMETER_FILE")
if [ -z "$cmdline" ]; then
    echo "错误: 在 parameter.txt 中未找到 CMDLINE 配置。"
    exit 1
fi

# 使用 sed 提取分区信息，存入数组方便编号选择
partitions=$(echo "$cmdline" | grep -oP '[0-9a-fxA-F]+@[0-9a-fxA-F]+\([^)]+\)' | sed 's/)/ /' | sed 's/(/ /' | awk -F'[@ ]' '{print $3".img|"$2}')

# 将解析结果转为数组
mapfile -t part_array <<< "$partitions"

if [ ${#part_array[@]} -eq 0 ]; then
    echo "错误: 未能从 parameter.txt 中解析出任何分区。"
    exit 1
fi

# 4. 打印分区选择菜单
echo "检测到以下待烧录分区:"
i=1
for part in "${part_array[@]}"; do
    # 提取并清理文件名 (去掉可能存在的 :grow)
    img_name=$(echo "$part" | cut -d'|' -f1 | sed 's/:grow//')
    hex_offset=$(echo "$part" | cut -d'|' -f2)
    echo "  [$i] $img_name (偏移: $hex_offset)"
    ((i++))
done
echo "  [a] 烧录全部 (All)"
echo "  [q] 退出脚本 (Quit)"
echo "------------------------------------------------------"

# 5. 获取用户输入
read -p "请输入要烧录的分区编号 (多个编号用空格隔开, 例如: 1 3 4): " SELECTION

if [[ "$SELECTION" == "q" || "$SELECTION" == "Q" ]]; then
    echo "操作取消。"
    exit 0
fi

# 6. 过滤并筛选需要烧录的分区
selected_parts=()
if [[ "$SELECTION" == "a" || "$SELECTION" == "A" ]]; then
    selected_parts=("${part_array[@]}")
else
    # 遍历用户的输入，匹配数组索引
    for idx in $SELECTION; do
        if [[ "$idx" =~ ^[0-9]+$ ]] && [ "$idx" -ge 1 ] && [ "$idx" -le "${#part_array[@]}" ]; then
            real_idx=$((idx-1))
            selected_parts+=("${part_array[$real_idx]}")
        else
            echo "警告: 忽略无效的选项 '$idx'"
        fi
    done
fi

if [ ${#selected_parts[@]} -eq 0 ]; then
    echo "错误: 没有选择任何有效的待烧录分区。"
    exit 1
fi

# 7. 最终确认
echo "------------------------------------------------------"
echo "【危险操作确认】"
echo "目标设备: $TARGET_DEV"
echo "待烧录分区:"
for part in "${selected_parts[@]}"; do
    img_name=$(echo "$part" | cut -d'|' -f1 | sed 's/:grow//')
    echo "  -> $img_name"
done
echo "------------------------------------------------------"

# 优化此处的输入判定逻辑，支持大小写 y/Y
read -p "请确认以上信息准确无误，执行烧录? [y/N]: " CONFIRM
if [[ "$CONFIRM" != "y" && "$CONFIRM" != "Y" ]]; then
    echo "操作取消。"
    exit 1
fi

# 8. 循环烧录选中的分区
for part in "${selected_parts[@]}"; do
    img_name=$(echo "$part" | cut -d'|' -f1 | sed 's/:grow//')
    hex_offset=$(echo "$part" | cut -d'|' -f2)
    
    dec_offset=$((hex_offset))
    img_path="$FW_DIR/$img_name"
    
    if [ -f "$img_path" ]; then
        echo ">>> 正在烧录 $img_name -> 偏移 $hex_offset ..."
        sudo dd if="$img_path" of="$TARGET_DEV" bs=512 seek=$dec_offset conv=notrunc,fsync status=progress
    else
        echo "跳过: 文件 $img_path 不存在。"
    fi
done

sync
echo "------------------------------------------------------"
echo "所选分区已全部烧录完成！"
