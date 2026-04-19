#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/rpmsg.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#define RPMSG_SERVICE_NAME      "rpmsg-mcu0-test"
#define MCU_EPT_ADDR            0x4003U
#define LOCAL_EPT_ADDR          1024U

#define RPMSG_DEV_DIR           "/dev"
#define RPMSG_SYSFS_DIR         "/sys/class/rpmsg"
#define MAX_RPMSG_NODES         128
#define MAX_PATH_LEN            PATH_MAX

#define CONNECT_WAIT_MS         10000U
#define SCAN_INTERVAL_US        100000U
#define MAIN_LOOP_US            100000U
#define CAN_TX_PERIOD_TICKS     10U

#define RPMSG_NODE_LEAK_THRESHOLD 32U

#define CAN_DLC_MAX             8U
#define CAN_RPMSG_MAGIC         0x314E4143U
#define CAN_RPMSG_VERSION       1U
#define CAN_DIR_MCU_TO_LINUX    0x01U
#define CAN_DIR_LINUX_TO_MCU    0x02U
#define CAN_DEFAULT_TX_ID       0x200U
#define CAN_RX_LOG_PATH         "/tmp/rpmsg_can_rx.log"

#define VIRTIO_DEV_NAME         "virtio0"

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t direction;
    uint8_t dlc;
    uint8_t reserved;
    uint32_t can_id;
    uint8_t data[CAN_DLC_MAX];
} CanRpmsgFrame;

typedef struct {
    int data_fd;
    char data_path[MAX_PATH_LEN];
} RPMsgSession;

static volatile sig_atomic_t g_stop = 0;
static FILE *g_canRxLogFp = NULL;
static uint8_t g_canRxLogWarned = 0U;

static void force_kernel_rebind(void);

static void close_can_rx_log(void)
{
    if (g_canRxLogFp == NULL) return;

    fclose(g_canRxLogFp);
    g_canRxLogFp = NULL;
}

static void log_can_rx_frame(const CanRpmsgFrame *frame)
{
    struct timespec ts;
    struct tm localTm;
    char timeBuf[32];
    uint8_t i;

    if (frame == NULL) return;

    if (g_canRxLogFp == NULL) {
        g_canRxLogFp = fopen(CAN_RX_LOG_PATH, "a");
        if (g_canRxLogFp == NULL) {
            if (g_canRxLogWarned == 0U) {
                perror("[Warn] 打开 CAN RX 日志文件失败");
                g_canRxLogWarned = 1U;
            }
            return;
        }
        (void)setvbuf(g_canRxLogFp, NULL, _IOLBF, 0);
    }

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        ts.tv_sec = time(NULL);
        ts.tv_nsec = 0;
    }

    if (localtime_r(&ts.tv_sec, &localTm) == NULL) return;

    if (strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &localTm) == 0U) {
        return;
    }

    fprintf(g_canRxLogFp, "[%s.%03ld] id=0x%08X dlc=%u data=", timeBuf,
            ts.tv_nsec / 1000000L, frame->can_id, frame->dlc);
    for (i = 0U; i < frame->dlc; i++) {
        fprintf(g_canRxLogFp, "%s%02X", (i == 0U) ? "" : " ", frame->data[i]);
    }
    fputc('\n', g_canRxLogFp);
}

static void signal_handler(int signo)
{
    (void)signo;
    g_stop = 1;
}

/* --- 工具函数区 (与你原版一致，为了完整性保留) --- */
static int has_numeric_suffix(const char *name, const char *prefix) {
    size_t i, prefix_len = strlen(prefix);
    if (strncmp(name, prefix, prefix_len) != 0 || name[prefix_len] == '\0') return 0;
    for (i = prefix_len; name[i] != '\0'; i++) if (!isdigit((unsigned char)name[i])) return 0;
    return 1;
}
static int read_text_file(const char *path, char *buf, size_t buf_len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, buf_len - 1U);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    buf[strcspn(buf, "\r\n")] = '\0';
    return 0;
}
static int read_u32_file(const char *path, uint32_t *value) {
    char buf[32], *end = NULL;
    if ((value == NULL) || (read_text_file(path, buf, sizeof(buf)) != 0)) return -1;
    errno = 0;
    unsigned long parsed = strtoul(buf, &end, 0);
    if ((errno != 0) || (end == buf)) return -1;
    *value = (uint32_t)parsed;
    return 0;
}
static int scan_ctrl_nodes(char nodes[][MAX_PATH_LEN], int max_nodes) {
    DIR *dir; struct dirent *ent; int count = 0;
    dir = opendir(RPMSG_DEV_DIR);
    if (dir == NULL) return -1;
    while ((ent = readdir(dir)) != NULL) {
        if (!has_numeric_suffix(ent->d_name, "rpmsg_ctrl")) continue;
        if (count < max_nodes) snprintf(nodes[count++], MAX_PATH_LEN, "%s/%s", RPMSG_DEV_DIR, ent->d_name);
    }
    closedir(dir); return count;
}
static int scan_all_data_nodes(char nodes[][MAX_PATH_LEN], int max_nodes) {
    DIR *dir; struct dirent *ent; int count = 0;
    dir = opendir(RPMSG_SYSFS_DIR);
    if (dir == NULL) return -1;
    while ((ent = readdir(dir)) != NULL) {
        if (!has_numeric_suffix(ent->d_name, "rpmsg")) continue;
        if (count < max_nodes) snprintf(nodes[count++], MAX_PATH_LEN, "%s/%s", RPMSG_DEV_DIR, ent->d_name);
    }
    closedir(dir); return count;
}
static int maybe_cleanup_endpoint_leak(void) {
    char nodes[MAX_RPMSG_NODES][MAX_PATH_LEN];
    int count = scan_all_data_nodes(nodes, MAX_RPMSG_NODES);

    if (count < 0) return 0;
    if ((uint32_t)count <= RPMSG_NODE_LEAK_THRESHOLD) return 0;

    fprintf(stderr, "[Recovery] 检测到 RPMsg 节点异常堆积 (%d)，执行总线重绑清理。\n", count);
    force_kernel_rebind();
    return 1;
}
static int scan_matching_data_nodes(const char *service_name, uint32_t src, uint32_t dst, char nodes[][MAX_PATH_LEN], int max_nodes) {
    DIR *dir; struct dirent *ent; int count = 0;
    dir = opendir(RPMSG_SYSFS_DIR);
    if (dir == NULL) return -1;
    while ((ent = readdir(dir)) != NULL) {
        char attr_path[MAX_PATH_LEN], name_buf[64]; uint32_t node_src, node_dst;
        if (!has_numeric_suffix(ent->d_name, "rpmsg")) continue;
        snprintf(attr_path, sizeof(attr_path), "%s/%s/name", RPMSG_SYSFS_DIR, ent->d_name);
        if (read_text_file(attr_path, name_buf, sizeof(name_buf)) != 0 || strcmp(name_buf, service_name) != 0) continue;
        snprintf(attr_path, sizeof(attr_path), "%s/%s/src", RPMSG_SYSFS_DIR, ent->d_name);
        if (read_u32_file(attr_path, &node_src) != 0) continue;
        snprintf(attr_path, sizeof(attr_path), "%s/%s/dst", RPMSG_SYSFS_DIR, ent->d_name);
        if (read_u32_file(attr_path, &node_dst) != 0) continue;
        if ((node_src != src) || (node_dst != dst)) continue;
        if (count < max_nodes) snprintf(nodes[count++], MAX_PATH_LEN, "%s/%s", RPMSG_DEV_DIR, ent->d_name);
    }
    closedir(dir); return count;
}
static int find_new_path(char before[][MAX_PATH_LEN], int before_cnt, char after[][MAX_PATH_LEN], int after_cnt, char *out, size_t out_len) {
    for (int i = 0; i < after_cnt; i++) {
        int found = 0;
        for (int j = 0; j < before_cnt; j++) if (strcmp(after[i], before[j]) == 0) { found = 1; break; }
        if (!found) { size_t copy_len = strnlen(after[i], out_len - 1U); memcpy(out, after[i], copy_len); out[copy_len] = '\0'; return 0; }
    }
    return -1;
}
static int open_data_node(const char *path) { return open(path, O_RDWR | O_NONBLOCK); }
static void close_session(RPMsgSession *session) {
    if ((session == NULL) || (session->data_fd < 0)) return;
    usleep(30000);
    (void)ioctl(session->data_fd, RPMSG_DESTROY_EPT_IOCTL);
    close(session->data_fd); session->data_fd = -1; session->data_path[0] = '\0';
}
static int create_endpoint_on_ctrl(const char *ctrl_path, char *data_path, size_t data_path_len) {
    int ctrl_fd, ret, before_cnt, after_cnt; uint32_t waited_ms = 0U;
    struct rpmsg_endpoint_info eptinfo; char before[MAX_RPMSG_NODES][MAX_PATH_LEN], after[MAX_RPMSG_NODES][MAX_PATH_LEN];

    if (maybe_cleanup_endpoint_leak() != 0) {
        before_cnt = 0;
    }

    before_cnt = scan_matching_data_nodes(RPMSG_SERVICE_NAME, LOCAL_EPT_ADDR, MCU_EPT_ADDR, before, MAX_RPMSG_NODES);

    /* 若同名同端点节点已存在，优先复用，避免重复创建导致 idr_alloc 耗尽。 */
    if (before_cnt > 0) {
        snprintf(data_path, data_path_len, "%s", before[before_cnt - 1]);
        return 0;
    }

    if (before_cnt < 0) before_cnt = 0;
    ctrl_fd = open(ctrl_path, O_RDWR); if (ctrl_fd < 0) return -1;
    memset(&eptinfo, 0, sizeof(eptinfo));
    snprintf(eptinfo.name, sizeof(eptinfo.name), "%s", RPMSG_SERVICE_NAME);
    eptinfo.src = LOCAL_EPT_ADDR; eptinfo.dst = MCU_EPT_ADDR;
    ret = ioctl(ctrl_fd, RPMSG_CREATE_EPT_IOCTL, &eptinfo); close(ctrl_fd);
    if (ret < 0) {
        if ((errno == ENOSPC) || (errno == ENOMEM)) {
            force_kernel_rebind();
        }
        if ((errno != EEXIST) && (errno != EBUSY)) return -1;
    }
    while (waited_ms < CONNECT_WAIT_MS) {
        after_cnt = scan_matching_data_nodes(RPMSG_SERVICE_NAME, LOCAL_EPT_ADDR, MCU_EPT_ADDR, after, MAX_RPMSG_NODES);
        if (after_cnt > 0) {
            if (find_new_path(before, before_cnt, after, after_cnt, data_path, data_path_len) == 0) return 0;
            snprintf(data_path, data_path_len, "%s", after[after_cnt - 1]); return 0;
        }
        usleep(SCAN_INTERVAL_US); waited_ms += (SCAN_INTERVAL_US / 1000U);
    }
    errno = ETIMEDOUT; return -1;
}
static int connect_session(RPMsgSession *session) {
    char ctrl_nodes[MAX_RPMSG_NODES][MAX_PATH_LEN]; int ctrl_cnt, i;

    (void)maybe_cleanup_endpoint_leak();

    ctrl_cnt = scan_ctrl_nodes(ctrl_nodes, MAX_RPMSG_NODES);
    if (ctrl_cnt <= 0) { errno = ENODEV; return -1; }
    for (i = 0; i < ctrl_cnt; i++) {
        char data_path[MAX_PATH_LEN]; int data_fd;
        if (create_endpoint_on_ctrl(ctrl_nodes[i], data_path, sizeof(data_path)) != 0) continue;
        data_fd = open_data_node(data_path);
        if (data_fd < 0) {
            if ((errno == ENOSPC) || (errno == ENOMEM)) {
                force_kernel_rebind();
            }
            continue;
        }
        session->data_fd = data_fd; snprintf(session->data_path, sizeof(session->data_path), "%s", data_path);
        return 0;
    }
    errno = ENODEV; return -1;
}
static int is_disconnect_errno(int err) {
    return (err == EPIPE) || (err == ENODEV) || (err == ENXIO) || (err == EBADF) || (err == ECONNRESET) || (err == ESHUTDOWN);
}
static int send_can_frame(int fd, const CanRpmsgFrame *frame) {
    ssize_t n;

    if (frame == NULL) return -1;

    n = write(fd, frame, sizeof(*frame));
    if (n == (ssize_t)sizeof(*frame)) return 0;
    if ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == ENOMEM)) return 1;

    if ((n >= 0) && (n < (ssize_t)sizeof(*frame))) errno = EIO;

    return -1;
}

static void build_linux_tx_frame(CanRpmsgFrame *frame, uint8_t counter)
{
    if (frame == NULL) return;

    memset(frame, 0, sizeof(*frame));
    frame->magic = CAN_RPMSG_MAGIC;
    frame->version = CAN_RPMSG_VERSION;
    frame->direction = CAN_DIR_LINUX_TO_MCU;
    frame->dlc = CAN_DLC_MAX;
    frame->can_id = CAN_DEFAULT_TX_ID;
    frame->data[0] = 0xA5U;
    frame->data[1] = 0x5AU;
    frame->data[2] = counter;
    frame->data[3] = (uint8_t)(~counter);
    frame->data[4] = 0x01U;
    frame->data[5] = 0x02U;
    frame->data[6] = 0x03U;
    frame->data[7] = 0x04U;
}

static int is_valid_mcu_frame(const CanRpmsgFrame *frame)
{
    if (frame == NULL) return 0;

    if (frame->magic != CAN_RPMSG_MAGIC) return 0;
    if (frame->version != CAN_RPMSG_VERSION) return 0;
    if (frame->direction != CAN_DIR_MCU_TO_LINUX) return 0;
    if (frame->dlc > CAN_DLC_MAX) return 0;

    return 1;
}

static void print_can_frame(const char *tag, const CanRpmsgFrame *frame)
{
    uint8_t i;

    if ((tag == NULL) || (frame == NULL)) return;

    printf("%s id=0x%08X dlc=%u data=", tag, frame->can_id, frame->dlc);
    for (i = 0U; i < frame->dlc; i++) {
        printf("%s%02X", (i == 0U) ? "" : " ", frame->data[i]);
    }
    printf("\n");
    fflush(stdout);
}

/* ===================================================================== */
/* --- 核心修改：/dev/kmsg 监听功能与底层驱动刷新逻辑 --- */

static void force_kernel_rebind(void)
{
    char cmd[256];
    printf("\n[Recovery] 确认 MCU 已软重启，准备刷新 Linux Virtio 驱动...\n");
    
    snprintf(cmd, sizeof(cmd), "echo %s > /sys/bus/virtio/drivers/virtio_rpmsg_bus/unbind 2>/dev/null", VIRTIO_DEV_NAME);
    system(cmd);
    
    usleep(500000); // 冷却 500ms
    
    snprintf(cmd, sizeof(cmd), "echo %s > /sys/bus/virtio/drivers/virtio_rpmsg_bus/bind 2>/dev/null", VIRTIO_DEV_NAME);
    system(cmd);
    
    printf("[Recovery] 驱动已刷新，Linux Vring 指针已重置！\n\n");
    sleep(1); // 等待底层节点重新生成
}

/* 初始化内核日志监听，并排空旧日志 */
static int init_kmsg_listener(void)
{
    int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("[Warn] 无法打开 /dev/kmsg，单边自愈功能可能失效");
        return -1;
    }
    // Linux 内核跳到最新日志的通用方法：直接 SEEK_END 
    lseek(fd, 0, SEEK_END);
    return fd;
}

/* 非阻塞检查内核日志，看 MCU 是否报错 already exist */
static int check_mcu_reboot_event(int kmsg_fd)
{
    if (kmsg_fd < 0) return 0;
    char kmsg_buf[2048];
    int reboot_detected = 0;

    while (1) {
        ssize_t n = read(kmsg_fd, kmsg_buf, sizeof(kmsg_buf) - 1);
        if (n <= 0) break; // EAGAIN，说明读完了最新日志
        
        kmsg_buf[n] = '\0';
        
        // 核心判据：捕捉到 MCU 重新发起 NS 广播被拒的系统印记
        if (strstr(kmsg_buf, "already exist") && strstr(kmsg_buf, "rpmsg")) {
            reboot_detected = 1;
            break; 
        }
    }
    return reboot_detected;
}
/* ===================================================================== */

int main(void)
{
    RPMsgSession session;
    uint32_t tick_cnt = 0U;
    uint8_t tx_counter = 0U;
    uint8_t buf[512];
    
    // 打开内核日志窃听器
    int kmsg_fd = init_kmsg_listener();

    memset(&session, 0, sizeof(session));
    session.data_fd = -1;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    while (!g_stop) {
        // --- 核心改动：监听 MCU 软重启事件 ---
        if (check_mcu_reboot_event(kmsg_fd)) {
            fprintf(stderr, "\n[Event] 捕捉到内核 already exist 报错，判定 MCU 已软重启！\n");
            
            // 立即掐断当前的死锁连接
            close_session(&session);
            
            // 执行单边强刷，把 Linux 这边的 Vring 也清零，与 MCU 对齐
            force_kernel_rebind();
            
            tick_cnt = 0U;
            continue; // 直接跳入下一轮重新建链
        }

        // --- 状态 1：尝试连接 ---
        if (session.data_fd < 0) {
            if (connect_session(&session) != 0) {
                usleep(SCAN_INTERVAL_US);
                tick_cnt++;
                continue;
            }

            printf("[Linux] 已连接到 %s，监听 MCU(%u) -> LOCAL(%u)\n",
                   session.data_path, MCU_EPT_ADDR, LOCAL_EPT_ADDR);
            fflush(stdout);

            {
                CanRpmsgFrame frame;
                build_linux_tx_frame(&frame, tx_counter++);
                if (send_can_frame(session.data_fd, &frame) == 0) {
                    print_can_frame("[Linux TX]", &frame);
                }
            }
            tick_cnt = 0U;
        }

        // --- 状态 2：周期发送 Linux -> MCU CAN 帧 ---
        if ((tick_cnt % CAN_TX_PERIOD_TICKS) == 0U) {
            CanRpmsgFrame frame;
            int ret;

            build_linux_tx_frame(&frame, tx_counter++);
            ret = send_can_frame(session.data_fd, &frame);

            if (ret == 0) {
                print_can_frame("[Linux TX]", &frame);
            } else if (ret < 0) {
                if (is_disconnect_errno(errno)) {
                    fprintf(stderr, "[Linux] TX failed, endpoint closed.\n");
                    close_session(&session);
                    tick_cnt = 0U;
                    continue;
                }
            }
        }

        // --- 状态 3：接收逻辑 ---
        while (!g_stop) {
            ssize_t n;
            n = read(session.data_fd, buf, sizeof(buf));
            
            if (n > 0) {
                if ((n % (ssize_t)sizeof(CanRpmsgFrame)) != 0) {
                    fprintf(stderr, "[Linux RX] unexpected payload len=%zd\n", n);
                    continue;
                }

                for (ssize_t off = 0; off < n; off += (ssize_t)sizeof(CanRpmsgFrame)) {
                    CanRpmsgFrame frame;
                    memcpy(&frame, &buf[off], sizeof(frame));

                    if (!is_valid_mcu_frame(&frame)) {
                        fprintf(stderr, "[Linux RX] drop invalid frame (magic/version/dir/dlc)\n");
                        continue;
                    }

                    print_can_frame("[Linux RX]", &frame);
                    log_can_rx_frame(&frame);
                }
                continue;
            }

            if (n == 0 || is_disconnect_errno(errno)) {
                fprintf(stderr, "[Linux] endpoint closed.\n");
                close_session(&session);
                break;
            }

            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                break; // 无数据，正常退出，不会触发超时强制重连
            }
            break;
        }

        usleep(MAIN_LOOP_US);
        tick_cnt++;
    }

    close_session(&session);
    close_can_rx_log();
    if (kmsg_fd >= 0) close(kmsg_fd);
    return 0;
}

