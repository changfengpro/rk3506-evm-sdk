# 📖 嵌入式开发 Patch 文件备份与使用实战指南

在嵌入式协同开发中，`.patch` 是管理 SDK 源码级修改的唯一正规途径。它避免了粗暴覆盖原厂 `.c` 文件导致的“原厂修复丢失”问题。

本文档详细说明了如何在本 BSP（板级支持包）中生成和使用补丁。

---

## 🛠️ 1. 如何制作一个标准的 Patch (备份修改)

每当你在 SDK 的某个独立 Git 子目录（如 `u-boot`, `kernel`, `hal`）中修改了代码，请遵循以下三步法导出补丁：

### 步骤 1：进入对应组件的 Git 目录
注意：一定要进入包含 `.git` 隐藏文件夹的层级，不能在 SDK 最顶层做。
```bash
cd ~/Project/Linux/rk3506_linux6.1_sdk_v1.2.0/u-boot
```

### 步骤 2：将修改提交为本地 Commit
优秀的 Commit Message 是工程素养的体现，请简明扼要写清意图。
```bash
git add .
git commit -m "rk3506: force IOMUX for M0 SWD J-Link debugging and change debug uart to uart1"
```

### 步骤 3：导出 Patch 文件到本热备仓库
`-1 HEAD` 表示只导出最近的 1 次提交。如果之前连续提交了 3 次未导出的修改，可以用 `-3 HEAD`。
```bash
git format-patch -1 HEAD -o ~/Project/Linux/vanxoak_rk3506_board_support/u-boot/patches/
```
执行完毕后，指定目录下会生成类似 `0001-rk3506-xxx.patch` 的标准文件。

---

## 📥 2. 如何正确应用 Patch (恢复代码)

将补丁打入一份干净的原厂代码时，通常有两种方式：

### 方式 A：带历史记录打入 (`git am`) —— 【强烈推荐】
完美保留你的 Commit 信息、作者和时间，打入后使用 `git log` 可以看到这笔提交。
```bash
git am /path/to/your/patch/0001-xxx.patch
```
*💡 排错技巧：如果出现冲突失败（例如原厂代码大改了），可以输入 `git am --abort` 取消打补丁，然后联系代码作者手动合并。*

### 方式 B：仅应用文件修改 (`git apply`) —— 【备用方案】
仅仅修改代码的加减内容，不会在 Git 仓库生成 Commit 记录。适合只想临时测试一下代码的场景，或者当前仓库本身不支持 `git am` 时。
```bash
git apply /path/to/your/patch/0001-xxx.patch
```

---

## 📏 3. 补丁管理三大原则

1. **一次修改，一个补丁：** 切忌把“修改串口”和“调整显示屏参数”混在一个 Commit 里提交。请拆分开来生成两个独立的补丁，方便日后排查问题或单独撤销某项功能。
2. **纯文本差异：** `git format-patch` 主要用于处理纯文本源码差异（`.c`, `.h`, `.dts` 等）。如果你的开发中新增了二进制文件（如预编译的 `lib.a` 或图片），请将其单独拷贝至 `vanxoak_rk3506_board_support` 中管理。
3. **及时清理无用拷贝：**
