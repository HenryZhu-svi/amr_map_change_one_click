# AMR Map Manager

AMR Map Manager is a Qt 6 desktop application for maintaining a manually configured fleet of SEER-compatible AMRs, downloading and previewing 2D maps, and safely uploading and switching maps across multiple robots.

The application is currently at version **0.2** and is designed for both Ubuntu and Windows.


## Features

### Robot management

- Add robots manually using a display name and IPv4 address.
- Remove selected robots.
- Persist the robot list and language preference with `QSettings`.
- Refresh the current map and connection state for one or more robots.
- Reject duplicate or invalid IPv4 addresses.

### Map download

- Download the current map from exactly one selected robot.
- Use the map name reported by the robot; no custom filename is requested.
- Save the original response bytes as `<map-name>.smap` in the system Downloads directory.
- Add a timestamp automatically when a file with the same name already exists.
- Calculate and display the local MD5 after download.
- Automatically open the downloaded map in the map preview.

### Batch upload and map switching

- Validate that a selected 2D `.smap` file contains a JSON object before upload.
- Calculate the local map MD5.
- Process up to three robots concurrently.
- Query each robot's navigation state before making changes.
- Skip robots in `WAITING`, `RUNNING`, or `SUSPENDED` state.
- Upload a map without switching it.
- Read each robot's actual stored-map list after upload, query those maps, and
  locate the uploaded map by matching its remote MD5 to the local file.
- Use the robot's actual stored filename for subsequent map switching.
- Optionally switch to the uploaded map after MD5 verification.
- Query `current_map` and `current_map_md5` after switching.
- Continue processing other robots when one robot fails.
- Show per-robot progress, results, and an operation log.

The application does **not** cancel active robot tasks automatically.

### SEER map preview

The viewer follows the map structure documented by SEER's `message_map.proto` reference and reads the Protobuf JSON representation directly with Qt JSON APIs. Both lower-camel-case and snake-case field names are accepted.

Displayed layers include:

- laser/occupancy scan points (`normalPosList`);
- normal map lines;
- landmarks, location marks, action points, park points, and charge points;
- station headings;
- Bezier navigation paths;
- advanced and forbidden lines;
- advanced areas.

Viewer controls:

- **Open map** loads a local `.smap` or JSON file.
- The mouse wheel zooms in and out.
- Dragging with the mouse pans the map.
- **Fit map** restores the full-map view.
- A summary shows the map name, type, version, resolution, scan-point count, station count, path count, and area count.

Large scan maps are rendered as one batched graphics item instead of creating one Qt item per scan point.

### Languages

- Simplified Chinese
- English

The language can be changed at runtime from the toolbar. The selection is remembered for the next launch. English is the primary documentation language.

## Implemented robot APIs

| Operation | Request | Response | Default service port |
|---|---:|---:|---:|
| Query navigation status | `1020` | `11020` | `19204` |
| Query current/stored map information | `1300` | `11300` | `19204` |
| Query map MD5 | `1302` | `11302` | `19204` |
| Switch loaded map | `2022` | `12022` | `19205` |
| Upload map | `4010` | `14010` | `19207` |
| Download map | `4011` | `14011` | `19207` |

The protocol implementation uses the 16-byte, big-endian header shown in the supplied API examples:

```text
5A 01 | sequence | payload length | command | original request | reserved
```

TCP responses are buffered until the complete payload length declared by the header has arrived. This is required for large map files that span multiple TCP reads.

## Build on Ubuntu

Ubuntu 22.04 or later:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build qt6-base-dev

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/amr-map-manager
```

## Build on Windows

Install Qt 6 with the MSVC component, Visual Studio C++ Build Tools, CMake, and Ninja. From a Qt-enabled terminal:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\amr-map-manager.exe
```

## Create release packages

### Ubuntu DEB

Install the packaging dependencies, then run the provided script on Ubuntu 22.04 x64:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build qt6-base-dev dpkg-dev \
  libgl1-mesa-dev libglx-dev libopengl-dev
bash scripts/package-ubuntu.sh
```

Output:

```text
dist/ubuntu/amr-map-manager_0.2.0_amd64.deb
```

Install and remove it with:

```bash
sudo apt install ./dist/ubuntu/amr-map-manager_0.2.0_amd64.deb
sudo apt remove amr-map-manager
```

### Windows installer

Run the packaging script from a regular PowerShell terminal after installing Visual Studio 2022 Build Tools, Qt 6 MSVC x64, CMake, and Inno Setup 6. Pass the Microsoft Visual C++ x64 Redistributable downloaded from Microsoft:

```powershell
.\scripts\package-windows.ps1 `
  -QtRoot "C:\Qt\6.8.3\msvc2022_64" `
  -VCRedistPath "C:\Installers\VC_redist.x64.exe" `
  -Version "0.2.0"
```

When Qt is installed under `C:\Qt` and the Visual C++ Redistributable is
available in the Visual Studio installation, both paths are detected
automatically:

```powershell
.\scripts\package-windows.ps1 -Version "0.2.0"
```

Outputs:

```text
dist/windows/AMRMapManager-0.2.0-win64-setup.exe
dist/windows/AMRMapManager-0.2.0-win64-portable.zip
```

The installer deploys the required Microsoft Visual C++ Runtime. The portable
archive requires that runtime to already be installed on the target computer.

### GitHub Actions

The `Build release packages` workflow can be started manually. It also runs when a version tag is pushed:

```bash
git tag v0.2.0
git push origin v0.2.0
```

## Basic workflow

1. Add the robot name and IPv4 address.
2. Select the robot and click **Refresh status**.
3. Verify that the current map and MD5 are displayed.
4. Use **Download current map** to save and preview the robot's current map.
5. Select a local map and use **Upload only** for the first test.
6. Confirm that remote MD5 verification succeeds.
7. Use **Upload, verify and switch** only after validating the workflow on one test robot.

## Project structure

```text
src/
  core/       Robot configuration model
  map/        SEER SMAP parser and graphics viewer
  network/    Asynchronous TCP request handling
  protocol/   RBK/SEER packet encoder and header decoder
  MainWindow  Robot table, workflows, logging, and language switching
```

## Safety and current limitations

- Verify the configured service ports and protocol header against one test robot before production use.
- Confirm that robots are stationary and in a safe operating state before uploading or switching maps.
- A successful `ret_code` is not treated as final success; upload workflows also verify MD5 and, when switching, the current map.
- Busy robots are skipped. Automatic task cancellation is intentionally disabled.
- Only 2D JSON `.smap` upload is currently exposed in the UI.
- 3D ZIP maps are not yet supported.
- The map viewer is read-only and does not edit or export map geometry.
- The application has not yet completed production validation across all robot firmware versions.

---

## 中文简介

AMR 地图批量管理器是一款基于 Qt 6 的桌面软件，目前支持：

- 手动添加和保存 AMR；
- 刷新机器人当前地图及 MD5；
- 直接下载当前地图到系统下载目录；
- 打开并预览 SEER 2D `.smap` 地图；
- 显示扫描点、站点、方向、贝塞尔路径、禁行线和区域；
- 批量上传地图并校验远端 MD5；
- 在校验成功后选择性切换地图；
- 切图前检查导航状态并跳过繁忙机器人；
- 中文与 English 运行时切换。

首次使用时请先在单台测试机器人上验证端口、协议和地图切换流程，再用于生产机器人。
