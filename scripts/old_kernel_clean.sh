#!/bin/bash

# ==============================================================================
# RK3506 根文件系统无损清理与打包脚本
# ==============================================================================

KERNEL_DIR="kernel-6.1"
TARGET_MODULES_DIR="output/rootfs/target/lib/modules"

echo "====================================================="
echo " 🧹 正在执行 Rootfs 模块精准清理..."
echo "====================================================="

# 1. 从内核源码树中获取当前最新的内核版本号 (如 6.1.118-rt45)
if [ ! -f "${KERNEL_DIR}/include/config/kernel.release" ]; then
    echo "❌ 找不到 kernel.release 文件，请先执行 ./build.sh kernel 编译内核！"
    exit 1
fi
CURRENT_VER=$(cat ${KERNEL_DIR}/include/config/kernel.release)
echo "✅ 当前最新内核版本为: ${CURRENT_VER}"

# 2. 扫描并精准删除旧的内核模块文件夹
if [ -d "${TARGET_MODULES_DIR}" ]; then
    # 遍历 modules 目录下的所有子目录
    for dir in "${TARGET_MODULES_DIR}"/*/; do
        # 确保它是目录，且目录存在
        if [ -d "$dir" ]; then
            dir_name=$(basename "$dir")
            # 如果目录名不等于当前内核版本号，就是旧垃圾，直接删掉！
            if [ "$dir_name" != "${CURRENT_VER}" ]; then
                echo "🗑️  发现并删除旧版内核模块目录: ${dir_name}"
                rm -rf "$dir"
            fi
        fi
    done
    echo "🛡️  清理完毕，已完美保留外置第三方驱动 (如 Wi-Fi/BT)！"
else
    echo "⚠️  目标模块目录不存在，跳过清理。"
fi


echo "🎉 清理完成！"
