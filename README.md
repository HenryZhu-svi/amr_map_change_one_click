# AMR 地图批量管理器

Qt 6 桌面应用，用于手动维护 AMR、查询/下载地图，以及向多台机器人上传并切换 2D 地图。

## 当前功能

- 手动添加、删除并持久保存机器人 IP
- 查询当前地图和 MD5（1300）
- 查询导航状态并跳过繁忙机器人（1020）
- 直接下载所选机器人的当前地图到系统“下载”目录，并保留原始字节（4011）
- 上传地图（4010）
- 上传后查询 MD5（1302）
- 可选切换已上传地图（2022）
- 切换后重新查询当前地图和 MD5
- 最多同时处理 3 台机器人
- 中文/English 运行时切换，并记住用户选择

默认不会取消导航任务；`WAITING`、`RUNNING`、`SUSPENDED` 状态均跳过。

## 假定的接口端口

| 服务 | 端口 |
|---|---:|
| 状态 | 19204 |
| 控制 | 19205 |
| 导航/任务 | 19206 |
| 配置 | 19207 |

协议头按 PDF 示例实现为 16 字节、大端序：`5A 01`、序列号、数据长度、消息编号、原请求编号、4 字节保留区。首次连接真机前应抓包或用单台测试机器人核对端口和协议版本。

## Ubuntu 构建

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build qt6-base-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/amr-map-manager
```

## Windows 构建

安装 Qt 6（MSVC 组件）、Visual Studio C++ 工具和 CMake，然后在 Qt 命令行中运行：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## 安全边界

- 先在单台测试机器人验证，再操作生产机器人。
- 上传和切换前由现场人员确认机器人处于安全、静止状态。
- `ret_code == 0` 不是最终成功；软件还会检查远端 MD5 和当前地图。
- 当前版本仅实现 2D JSON `.smap`，尚未开放 3D ZIP 上传。
