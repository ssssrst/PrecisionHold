# PrecisionHold

### 中文

PrecisionHold 是一款 Windows 鼠标指针速度切换工具，主要用于 DaVinci Resolve 调色时精准拖动曲线、色轮和细小控件，也适用于修图、绘图及其他需要精确控制指针的场景。按住快捷键临时切换速度，松开后恢复。程序通过 Windows 系统指针速度工作，不与 DaVinci Resolve 专用集成。

当前版本：0.10.1（Windows x64）。默认快捷键为 Caps Lock，目标速度为 4 档，可设置 1–20 档。下载 [独立 EXE](dist/PrecisionHold.exe)，双击即可运行。无需安装、管理员权限或额外运行库。

构建需要 Windows x64、Visual Studio 2022 C++ 工具链、Windows SDK 和 CMake 3.25 或更新版本：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cmake --install build --config Release --prefix dist
```

本仓库仅保留构建和运行所需的源码、资源与程序，不包含测试套件或开发过程文档。项目尚未选择开源许可证。

### English

PrecisionHold is a Windows pointer-speed utility created primarily for precise control while color grading in DaVinci Resolve, such as adjusting curves, color wheels, and small controls. It also suits photo editing, drawing, and other tasks that require precise pointer movement. Hold a shortcut to switch pointer speed temporarily; release it to restore the previous speed. The app uses Windows system pointer settings and has no DaVinci Resolve-specific integration.

Current version: 0.10.1 for Windows x64. The default shortcut is Caps Lock and the target is level 4, configurable from level 1 to 20. Download the [standalone EXE](dist/PrecisionHold.exe) and run it. No installer, administrator rights, or additional runtime is required.

Build requirements: Windows x64, the Visual Studio 2022 C++ toolchain, the Windows SDK, and CMake 3.25 or newer:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cmake --install build --config Release --prefix dist
```

This repository keeps only the source, resources, and program needed to build and run the application. It does not include the test suite or development-process documents. No open-source license has been selected.
