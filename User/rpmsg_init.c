#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/rpmsg.h>

#define RPMSG_SERVICE_NAME "rpmsg-mcu0-test"
#define MCU_EPT_ADDR       0x4003
#define LOCAL_EPT_ADDR     1024

int main() {
    int ctrl_fd, data_fd;
    struct rpmsg_endpoint_info eptinfo;
    char buf[512];
    ssize_t n;

    ctrl_fd = open("/dev/rpmsg_ctrl0", O_RDWR);
    if (ctrl_fd < 0) {
        perror("无法打开 /dev/rpmsg_ctrl0");
        return 1;
    }

    memset(&eptinfo, 0, sizeof(eptinfo));
    strncpy(eptinfo.name, RPMSG_SERVICE_NAME, sizeof(eptinfo.name));
    eptinfo.src = LOCAL_EPT_ADDR;
    eptinfo.dst = MCU_EPT_ADDR;

    // 创建端点
    ioctl(ctrl_fd, RPMSG_CREATE_EPT_IOCTL, &eptinfo);

    usleep(200000); 

    data_fd = open("/dev/rpmsg0", O_RDWR);
    if (data_fd < 0) {
        perror("打开 /dev/rpmsg0 失败");
        return 1;
    }

    // 1. 发送信号给 MCU，建立连接
    write(data_fd, "ready", 6);
    printf("[Linux] 已占领地址 %d，正在监听来自 MCU(%d) 的数据...\n", LOCAL_EPT_ADDR, MCU_EPT_ADDR);

    // 2. 核心修改：在这里死循环读取并打印，不要退出
    // 这样你就不需要单独执行 cat 了
    while (1) {
        memset(buf, 0, sizeof(buf));
        n = read(data_fd, buf, sizeof(buf));
        if (n > 0) {
            // 直接打印到屏幕，效果和 cat 一样
            printf("%s\n", buf); 
            fflush(stdout);
        } else if (n < 0) {
            perror("读取出错");
            break;
        }
    }

    close(data_fd);
    close(ctrl_fd);
    return 0;
}
