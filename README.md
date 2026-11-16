# th95 — 东方文花帖（TH095）移植

C++17 / Win32 / Direct3D 9 的原生移植：按原版 `th095.exe` 的地址逐函数转写，直接读取原版 DAT，不修改或重新打包原版资源。当前已接入启动、标题、Help、Mission Select、Music Room、Option、只读成绩和六个 LV1 场景的照片、弹幕、碰撞、退场及结算画面。

## 启动

本次生成的程序是 `build/th95.exe`。文件应这样放置：

```text
build/
  th95.exe
  th095.dat     原版主资源归档
  thbgm.dat     原版独立音乐数据，标题 BGM 需要它
```

直接启动 `build/th95.exe`。优先在 EXE 同目录寻找 `th095.dat`，其次在当前工作目录寻找；音乐从所选主归档的同目录读取。`th095c.dat` 是另一个主归档，可用 `--dat` 单独指定，不是补丁归档。

```powershell
.\build\th95.exe
.\build\th95.exe --dat build\th095c.dat
.\build\th95.exe --list build\th095.dat
.\build\th95.exe --verify build\th095.dat
.\build\th95.exe --extract build\audit-local build\th095.dat
```

`log.txt` 写在当前工作目录；其中记录实际读取的 DAT/BGM 路径及资源、音频错误。

Mission Select 读取主 DAT 同目录的 `scoreth095.dat`，缺失时使用空档内存；通关离开关卡时会按原版 `0x435910` 写回该文件（照片 `bs_*.dat` 仍不写，见 `docs/save-file.md`）。任务评语绘制需要 DirectX 运行库中的 `d3dx9_43.dll`。

正常启动显示原版启动图，呈现一帧后立即开始准备标题资源，同时播放 `nowloading.anm` 的文字/旋转花形。资源就绪后直接进入标题并启动 BGM，加载提示按原脚本淡出。普通启动不运行会额外等待数秒的校准，也不附加测试延迟或等待确认键；提示播放时长取决于实际资源加载时间。


## 操作与功能边界

| 按键 | 当前行为 |
| --- | --- |
| ↑ / W、↓ / S | 选择菜单，支持按住重复 |
| Z / Enter / Space | 确认 |
| X / Esc | 先选中 Quit；已在 Quit 时退出 |
| Help 清单 | ↑/↓ 选择九页条目，支持按住重复与环绕；Z/Enter 进入，X/Esc 返回标题 |
| Help 页面 | ←/→ 翻页（0–8，首尾不环绕），Z/Enter 或 X/Esc 回到清单 |
| Mission Select | ←/→ 切等级，↑/↓ 选任务，均环绕；Shift 隐藏角色与评语，X/Esc 返回标题 |
| Music Room | ↑/↓ 选曲环绕，Z/Enter 切歌，X/Esc 返回标题 |
| Option | ↑/↓ 选七行（不连发），←/→ 调显示模式/BGM/SE 音量，仅「終了」行的 Z 或 X 返回 |

Game Start 进入 Mission Select，已接入原始任务行、说明、角色、评语、等级开放条件和记录数字。Music Room 与 Option 已按原版状态机接入；六个 LV1 场景可运行照片、弹幕、碰撞、退场和结果流程；其他任务与 Replay 仍未实现。原版同输入逐帧与音频采样对照仍待完成。

## 构建与验证

当前已验证 Windows / MinGW-w64 / CMake。静态链接 MinGW 运行库；MSVC 构建未验证。

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j 4
ctest --test-dir build --output-on-failure
python tools\archive_manifest.py build\th095.dat
python tools\archive_manifest.py build\th095c.dat
python tools\replay_manifest.py th95\全通关REPLAY --output build\reference-replay-manifest-structured.csv
python tools\replay_payload.py "th95\全通关REPLAY"
python tools\score_container.py th95\scoreth095.dat
python tools\bestshot_inspect.py th95\bestshot\bs_01_1.dat
.\build\th95.exe --smoke-test build\title-fixed.png build\th095.dat
.\build\th95.exe --startup-test build\startup-no-wait build\th095.dat
.\build\th95.exe --startup-test build\startup-slow build\th095c.dat --slow-loading
.\build\th95.exe --help-test build\help-rewrite-ja build\th095.dat
.\build\th95.exe --help-test build\help-rewrite-cn build\th095c.dat
.\build\th95.exe --mission-test build\mission-final-ja build\th095.dat
.\build\th95.exe --option-test build\option-final-ja build\th095.dat
.\build\th95.exe --option-test build\option-final-cn build\th095c.dat
.\build\th95.exe --mission-test build\mission-font-cn build\th095c.dat
.\build\th95.exe --mission-test build\mission-final-profile-ja build\th095.dat --score build\score-fixture-full.dat
python tools\anm_inspect.py build\audit-fixed\title.anm --scripts 145,146
```

CTest 在配置时发现 `build/th095.dat` / `build/th095c.dat` 才添加相应原始资源测试。资源文件不随源码分发。

本次两个归档均为 **262/262 条解码长度匹配、0 条不足**，已生成各自 CSV 内容哈希清单。长度匹配、当前解码输出哈希和标题画面检查不等同于全部资源已与原版加载器逐字节对比。fixture 测试覆盖解密边界、LZSS 截断/终止/重叠复制、标题页表、六项动画位置/中断和 3600 帧背景执行。

`--smoke-test` 运行约 12 秒，保存两张 PNG，要求截图成功且 BGM 设备播放位置推进后才返回成功。它验证提交到音频设备，不代替实际试听，也没有覆盖约 96 秒后的音乐循环边界。

`--startup-test` 默认不加延迟，保存六个启动/加载/标题画面检查点，并检查启动期间静音、进入标题后 BGM 设备位置推进。只有显式传入 `--slow-loading` 才额外模拟 90 tick 慢速读取。日文默认启动和中文显式慢速检查均已通过；记录与截图位于对应输出目录。原版同帧对照、完整启动配置和失败提示尚待完成，详见启动记录。

## 分析与后续工作

- [符卡宣言蓝色墨迹背景还原依据](docs/spell-banner-backdrop.md)
- [AI/开发者项目交接说明](docs/ai-handoff.md)
- [本次标题修复依据与验证记录](docs/title-recovery.md)
- [启动动画逆向依据与验证记录](docs/startup-animation.md)
- [逆向分析记录](docs/th095-reverse-analysis.md)
- [原版运行基准首轮记录](docs/reference-baseline.md)
- [回放容器与载荷解密参数](docs/replay-format.md)
- [录像链路：录制、保存、浏览、播放](docs/replay-pipeline.md)
- [首页挂机演示（attract / demo 回放）](docs/demo-playback.md)
- [分数文件容器](docs/score-file.md)
- [Help 界面依据与实现](docs/help-screen.md)
- [Mission Select 依据与实现](docs/mission-select.md)
- [Music Room 依据与实现](docs/music-room.md)
- [Option 依据与实现](docs/option-screen.md)
- [bestshot 照片格式证据](docs/bestshot-format.md)
- [面向 Emscripten 的平台/核心边界](docs/port-architecture.md)
- [`th095.cfg` 200 字节配置](docs/th095-cfg.md)
- [移植路线图与任务清单](docs/th095-replication-plan.md)

参考 `Ritosa2333/eagler-touhou/ports/th09` 的资源与宿主分层、按功能验证的方法；TH09 是《东方花映塚》，不作为 TH095 界面或游戏规则的证据。归档核心也支持 `Archive::openBytes`，便于未来由浏览器 `fetch` 后挂载 DAT。下一步先建立原版动态基准和可重复对照样本，再依路线图完成共享运行时、菜单、游戏流程与用户数据兼容。
