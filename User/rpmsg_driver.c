#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>

#include "ipc_motor_protocol.h" // 务必与 M0 保持一致

/* ==========================================================
 * Linux RPMsg Char 驱动的标准 IOCTL 结构体与宏
 * (自己定义可以避免交叉编译链头文件版本不一致的问题)
 * ========================================================== */
struct rpmsg_endpoint_info {
    char name[32];
    uint32_t src;
    uint32_t dst;
};

#define RPMSG_CREATE_EPT_IOCTL _IOW(0xb5, 0x1, struct rpmsg_endpoint_info)
#define RPMSG_DESTROY_EPT_IOCTL _IOW(0xb5, 0x2, struct rpmsg_endpoint_info)

/* 节点路径定义 */
#define RPMSG_CTRL_DEV "/dev/rpmsg_ctrl0"

static int read_text_file(const char *path, char *buf, size_t buf_sz)
{
    int fd;
    ssize_t n;

    if (buf == NULL || buf_sz == 0U) {
        return -1;
    }

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    n = read(fd, buf, buf_sz - 1U);
    close(fd);
    if (n <= 0) {
        return -1;
    }

    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[n - 1] = '\0';
        n--;
    }

    return 0;
}

static int find_rpmsg_ept_dev(const struct rpmsg_endpoint_info *ept_info,
                              char *dev_path,
                              size_t dev_path_sz)
{
    DIR *dir;
    struct dirent *ent;
    char path[PATH_MAX];
    char name[64];
    char src_text[64];
    char dst_text[64];
    unsigned long src;
    unsigned long dst;
    long idx;
    long best_idx = LONG_MAX;
    char best_path[64] = {0};

    if (ept_info == NULL || dev_path == NULL || dev_path_sz == 0U) {
        return -1;
    }

    dir = opendir("/sys/class/rpmsg");
    if (dir == NULL) {
        return -1;
    }

    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "rpmsg", 5) != 0) {
            continue;
        }
        if (strncmp(ent->d_name, "rpmsg_ctrl", 10) == 0) {
            continue;
        }

        snprintf(path, sizeof(path), "/sys/class/rpmsg/%s/name", ent->d_name);
        if (read_text_file(path, name, sizeof(name)) != 0) {
            continue;
        }
        if (strcmp(name, ept_info->name) != 0) {
            continue;
        }

        snprintf(path, sizeof(path), "/sys/class/rpmsg/%s/src", ent->d_name);
        if (read_text_file(path, src_text, sizeof(src_text)) != 0) {
            continue;
        }

        snprintf(path, sizeof(path), "/sys/class/rpmsg/%s/dst", ent->d_name);
        if (read_text_file(path, dst_text, sizeof(dst_text)) != 0) {
            continue;
        }

        src = strtoul(src_text, NULL, 0);
        dst = strtoul(dst_text, NULL, 0);
        if ((uint32_t)src != ept_info->src || (uint32_t)dst != ept_info->dst) {
            continue;
        }

        idx = strtol(ent->d_name + 5, NULL, 10);
        if (idx < 0) {
            continue;
        }

        if (idx < best_idx) {
            best_idx = idx;
            snprintf(best_path, sizeof(best_path), "/dev/%s", ent->d_name);
        }
    }

    closedir(dir);
    if (best_idx == LONG_MAX) {
        return -1;
    }

    snprintf(dev_path, dev_path_sz, "%s", best_path);
    return 0;
}

static int wait_rpmsg_ept_dev(const struct rpmsg_endpoint_info *ept_info,
                              char *dev_path,
                              size_t dev_path_sz,
                              uint32_t timeout_ms)
{
    uint32_t waited = 0U;

    while (waited < timeout_ms) {
        if (find_rpmsg_ept_dev(ept_info, dev_path, dev_path_sz) == 0) {
            return 0;
        }
        usleep(20000);
        waited += 20U;
    }

    return -1;
}

int main(int argc, char *argv[])
{
    int ctrl_fd, fd;
    ssize_t bytes_read, bytes_written;
    System_Telemetry_s rx_telemetry;
    System_ControlCmd_s tx_control_cmd;
    char ept_dev_path[64] = {0};

    (void)argc;
    (void)argv;

    setvbuf(stdout, NULL, _IONBF, 0);

    printf("[Linux] RPMsg Motor Control App Started.\n");

    // ==========================================================
    // 步骤 1: 通过控制节点动态创建我们专属的 RPMsg 通道
    // ==========================================================
    ctrl_fd = open(RPMSG_CTRL_DEV, O_RDWR);
    if (ctrl_fd < 0) {
        perror("Failed to open " RPMSG_CTRL_DEV);
        printf("Please ensure the rpmsg_char driver is loaded.\n");
        return -1;
    }

    struct rpmsg_endpoint_info ept_info = {
        .name = "motor_ctrl",
        .src = 0x2027, // Linux 端的本地接收端口
        .dst = 0x2023  // M0 端的目标发送端口
    };

    printf("[Linux] Creating Endpoint: SRC=0x%X, DST=0x%X...\n", ept_info.src, ept_info.dst);
    if (find_rpmsg_ept_dev(&ept_info, ept_dev_path, sizeof(ept_dev_path)) == 0) {
        printf("[Linux] Reusing endpoint device: %s\n", ept_dev_path);
    } else {
        int ret = ioctl(ctrl_fd, RPMSG_CREATE_EPT_IOCTL, &ept_info);
        if (ret < 0) {
            if (errno == EEXIST) {
                printf("[Linux] Endpoint already exists. Continuing...\n");
            } else {
                perror("Failed to create RPMsg endpoint");
                close(ctrl_fd);
                return -1;
            }
        } else {
            printf("[Linux] Endpoint created successfully!\n");
        }
    }
    
    // 关闭控制节点，通道创建完毕后系统会自动生成 /dev/rpmsg0
    close(ctrl_fd);

    // 按 name/src/dst 精确匹配对应的 /dev/rpmsgX，避免节点编号漂移
    if (ept_dev_path[0] == '\0') {
        if (wait_rpmsg_ept_dev(&ept_info, ept_dev_path, sizeof(ept_dev_path), 2000U) != 0) {
            perror("Failed to locate rpmsg endpoint device in /sys/class/rpmsg");
            return -1;
        }
    }

    // ==========================================================
    // 步骤 2: 打开刚创建好的实际通信节点，开始收发数据
    // ==========================================================
    fd = open(ept_dev_path, O_RDWR);
    if (fd < 0) {
        perror("Failed to open endpoint device");
        return -1;
    }
    printf("[Linux] Successfully opened %s. Waiting for M0 Data...\n", ept_dev_path);

    // 先发一帧，确保远端能立即学习到回包地址
    memset(&tx_control_cmd, 0, sizeof(System_ControlCmd_s));
    tx_control_cmd.timestamp = 1U;
    tx_control_cmd.cmds[0].motor_id = 0U;
    tx_control_cmd.cmds[0].motor_type = M3508;
    tx_control_cmd.cmds[0].control_mode = MOTOR_CONTROL_MODE_VELOCITY;
    tx_control_cmd.cmds[0].velocity_q16 = (int32_t)(2000.0f * 65536.0f);
    bytes_written = write(fd, &tx_control_cmd, sizeof(System_ControlCmd_s));
    if (bytes_written != sizeof(System_ControlCmd_s)) {
        perror("[Linux] Initial write failed");
    }

    // ==========================================================
    // 步骤 3: 核心主循环
    // ==========================================================
    while (1) 
    {
        // 【接收阶段】: read() 会阻塞，直到 M0 端发来数据
        bytes_read = read(fd, &rx_telemetry, sizeof(System_Telemetry_s));
        
        if (bytes_read < 0) {
            perror("Error reading from RPMsg");
            break;
        } else if (bytes_read != sizeof(System_Telemetry_s)) {
            printf("[Warning] Read length mismatch: expected %lu, got %zd\n", 
                   sizeof(System_Telemetry_s), bytes_read);
            continue; 
        }

        // 成功读到数据！打印验证 M0 发来的 Mock Data
        printf("\n--- Received Telemetry (Timestamp: %u) ---\n", rx_telemetry.timestamp);
        printf("Motor[0](M3508) - Round: %d, ECD: %u, Speed: %d RPM\n",
               rx_telemetry.motors[0].total_round, rx_telemetry.motors[0].ecd, rx_telemetry.motors[0].speed_raw);
        printf("Motor[4](GM6020) - Round: %d, ECD: %u, Speed: %d RPM\n",
               rx_telemetry.motors[4].total_round, rx_telemetry.motors[4].ecd, rx_telemetry.motors[4].speed_raw);

        // 【发送阶段】: 组装假指令发给 M0 测试双向连通性
        memset(&tx_control_cmd, 0, sizeof(System_ControlCmd_s));
        tx_control_cmd.timestamp = rx_telemetry.timestamp; 
        
        tx_control_cmd.cmds[0].motor_id = 0;
        tx_control_cmd.cmds[0].motor_type = M3508;
        tx_control_cmd.cmds[0].control_mode = MOTOR_CONTROL_MODE_VELOCITY;
        tx_control_cmd.cmds[0].velocity_q16 = (int32_t)(2500.5f * 65536.0f); // 测试 Q16 缩放

        bytes_written = write(fd, &tx_control_cmd, sizeof(System_ControlCmd_s));
        if (bytes_written != sizeof(System_ControlCmd_s)) {
            perror("Error writing to RPMsg");
        }
    }

    close(fd);
    return 0;
}