###
 # @Description: 
 # @Author: changfengpro
 # @brief: 
 # @version: 
 # @Date: 2026-04-21 21:26:28
 # @LastEditors:  
 # @LastEditTime: 2026-04-21 21:34:58
### 

#!/bin/bash

SOURCE_FILE="rpmsg_driver"  # 需要编译的文件名称
SOURCE_DIR="/home/rmer/Project/Linux/vanxoak_rk3506_board_support/User"

TOOLCHAIN_PREFIX="arm-none-linux-gnueabihf-"
TOOL_DIR="/home/rmer/usr/local/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin/$TOOLCHAIN_PREFIX"

echo "[Info] 开始编译 $SOURCE_FILE..."


"${TOOL_DIR}gcc" -O2 "$SOURCE_DIR/$SOURCE_FILE.c" -o "$SOURCE_DIR/$SOURCE_FILE"

echo "[Info] 编译结束 $SOURCE_FILE..."


