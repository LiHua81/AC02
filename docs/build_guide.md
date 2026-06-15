# AC02 STM32 构建指南

## 环境要求

| 工具 | 说明 |
|------|------|
| CMake | ≥ 3.22，构建系统 |
| Ninja | ≥ 1.10，构建生成器 |
| arm-none-eabi-gcc | ARM 交叉编译器 (Cortex-M3) |
| STM32CubeProgrammer | 用于烧录固件 |

> 推荐安装 **STM32CubeCLT**，一次安装即可获得全部工具链。

### STM32CubeCLT 安装路径

默认安装路径：`H:\ST\STM32CubeCLT_1.21.0\`

| 工具 | 路径 |
|------|------|
| CMake | `<CLT>\CMake\bin\cmake.exe` |
| Ninja | `<CLT>\Ninja\bin\ninja.exe` |
| GCC | `<CLT>\GNU-tools-for-STM32\bin\arm-none-eabi-gcc.exe` |
| Programmer | `<CLT>\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe` |

---

## 快速开始

### 方式 1：使用 CMake Presets（推荐）

项目已配置 `CMakePresets.json`，一键构建：

```powershell
# Debug 构建
cmake --preset Debug
cmake --build build/Debug -j 10

# Release 构建
cmake --preset Release
cmake --build build/Release -j 10
```

### 方式 2：手动指定工具链

```powershell
cmake -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake ^
  -DCMAKE_MAKE_PROGRAM=<STM32CubeCLT>\Ninja\bin\ninja.exe ^
  -DCMAKE_C_COMPILER=<STM32CubeCLT>\GNU-tools-for-STM32\bin\arm-none-eabi-gcc.exe ^
  -DCMAKE_CXX_COMPILER=<STM32CubeCLT>\GNU-tools-for-STM32\bin\arm-none-eabi-g++.exe ^
  -B cmake-build-release-stm32_cubeclt_gnu-1 ^
  -S .

cmake --build cmake-build-release-stm32_cubeclt_gnu-1 -j 10
```

### 方式 3：使用脚本自动构建

```powershell
python <skill_dir>\scripts\build_stm32.py <project_root>
```

---

## CLion 中使用

1. 打开项目，CLion 会自动识别 `CMakePresets.json`
2. 在顶部工具栏选择需要的 Preset（Debug / Release）
3. 点击 **Build** 按钮或按 `Ctrl+F9`

### CLion 构建配置

| 配置名称 | 工具链 | 输出目录 |
|----------|--------|----------|
| `Debug` | CMakePresets (Debug) | `build/Debug/` |
| `Release-STM32_CubeCLT_GNU (1)` | STM32CubeCLT GNU | `cmake-build-release-stm32_cubeclt_gnu-1/` |

---

## 烧录（Flash）

### 命令行烧录

```powershell
<STM32CubeCLT>\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe ^
  -c port=swd freq=4000 mode=UR ^
  -d <elf_path>\AC02.elf ^
  -v -rst
```

| 参数 | 值 | 说明 |
|------|-----|------|
| `-c port` | `swd` | SWD 接口 |
| `-c freq` | `4000` | 4 MHz SWD 时钟 |
| `-c mode` | `UR` | 连接时复位 |
| `-d` | `<elf>` | 固件文件 |
| `-v` | | 烧录后验证 |
| `-rst` | | 烧录后复位 |

### 典型 ELF 路径

```powershell
# CLion Release 配置
cmake-build-release-stm32_cubeclt_gnu-1\AC02.elf

# CMakePresets Debug
build\Debug\AC02.elf
```

### CLion 外部工具配置

`File → Settings → Tools → External Tools → Add`

| 字段 | 值 |
|------|-----|
| Name | `Flash STM32` |
| Program | `$STM32CubeProgrammer$\STM32_Programmer_CLI.exe` |
| Arguments | `-c port=swd freq=4000 mode=UR -d $CMakeOutputDir$\AC02.elf -v -rst` |
| Working directory | `$ProjectFileDir$` |

> `$CMakeOutputDir$` 在 CMake 配置和构建后自动解析。

### 使用脚本烧录

```powershell
python build_stm32.py <project_root> --flash
```

脚本会自动构建（如需要）然后烧录。

---

## 构建输出

| 文件 | 路径 | 说明 |
|------|------|------|
| `AC02.elf` | `build/<preset>/` | 可执行固件（烧录用） |
| `AC02.map` | `build/<preset>/` | 链接器内存映射 |
| `compile_commands.json` | `build/<preset>/` | clangd 索引 |

### 内存使用

```
Memory region    Used Size   Region Size   %age Used
         RAM:     4440 B         48 KB       9.03%
       FLASH:    42256 B        256 KB      16.12%
```

---

## 项目结构

```
AC02/
├── CMakeLists.txt              # 根 CMake
├── CMakePresets.json           # CMake 预设
├── cmake/
│   ├── gcc-arm-none-eabi.cmake  # GCC 工具链
│   ├── starm-clang.cmake       # Clang 工具链（可选）
│   └── stm32cubemx/
│       └── CMakeLists.txt      # CubeMX HAL 驱动
├── Core/                       # STM32CubeMX 代码
├── Drivers/                    # HAL 驱动库
├── port/                       # 硬件抽象层
├── algo/                       # 算法库
├── app/                        # 应用层
├── oled_driver/                # OLED 驱动
├── startup_stm32f103xe.s       # 启动文件
└── STM32F103XX_FLASH.ld        # 链接脚本
```

---

## 常见问题

### Q: CLion 报错 "unrecognized option '--major-image-version'"
A: CLion 检测交叉编译器时可能出错。已在 `CMakeLists.txt` 中加入修复：
```cmake
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
```

### Q: 报错 "Error: File does not exist: AC02.elf"
A: 先执行构建，或检查 ELF 路径是否正确。

### Q: ST-Link 连接失败
A: 检查 USB 连接，尝试重新插拔 ST-Link。也可尝试按住复位键再烧录（使用 `mode=UR`）。

### Q: 需要清理重编
```powershell
rm -rf build/Debug
cmake --preset Debug
cmake --build build/Debug -j 10
```

### Q: 如何添加新的 .c 源文件
在根 `CMakeLists.txt` 的 `target_sources()` 中添加路径。

### Q: 如何切换工具链
修改 `CMakePresets.json` 中的 `toolchainFile`：
```json
"toolchainFile": "${sourceDir}/cmake/starm-clang.cmake"
```

starm-clang 支持三种 LibC 配置（在 `cmake/starm-clang.cmake` 中通过 `STARM_TOOLCHAIN_CONFIG` 切换）：
- `STARM_PICOLIBC`（默认）
- `STARM_NEWLIB`
- `STARM_HYBRID`
