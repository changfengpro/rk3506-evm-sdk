#!/bin/bash

# ================= 配置区 =================
LOCAL_DIR="/home/rmer/Project/Linux/vanxoak_rk3506_board_support/User/rpmsg_init"
REMOTE_USER="root"
REMOTE_HOST="192.168.1.10"
REMOTE_PASS="root"
# 目标板上的存放路径
REMOTE_DIR="/root/" 
# ==========================================

# 提取本地文件夹名（即 rpmsg_frame），方便后续拼接远程路径
DIR_NAME=$(basename "$LOCAL_DIR")

echo "[Info] 开始通过 SCP 同步代码到开发板 ($REMOTE_HOST)..."

# 使用 scp 进行递归复制 (-r)
# -o StrictHostKeyChecking=no 忽略主机指纹确认
sshpass -p "$REMOTE_PASS" scp -r -o StrictHostKeyChecking=no "$LOCAL_DIR" "$REMOTE_USER@$REMOTE_HOST:$REMOTE_DIR"

if [ $? -eq 0 ]; then
    echo "[Success] 同步完成！"
    
    echo "[Info] 正在远程赋予可执行权限..."
    # 通过 ssh 登录板子并远程执行 chmod
    # 这里的路径组合为 /root/rpmsg_frame/rpmsg_frame（假设你的可执行文件与文件夹同名）
    # 如果 rpmsg_frame 只是一个文件而不是文件夹，请将路径改为 "$REMOTE_DIR/$DIR_NAME"
    sshpass -p "$REMOTE_PASS" ssh -o StrictHostKeyChecking=no "$REMOTE_USER@$REMOTE_HOST" "chmod +x ./rpmsg_init"
    
    if [ $? -eq 0 ]; then
        echo "[Success] 权限配置完成！"
    else
        echo "[Error] 权限配置失败，请检查远程路径是否正确。"
    fi
else
    echo "[Error] 同步失败，请检查网络连接或 IP 地址。"
fi
