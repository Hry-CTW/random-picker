# 班级随机抽人工具 · random picker

C++ 单文件 GUI（Win32 原生，零第三方依赖），深色极客风。支持人 / 组两级关系、名单导入替换编辑、真随机抽取与留证。
构建产物 `picker.exe` 可直接在 Windows 双击运行，无需安装任何环境。

## 名单格式（重要 · 先读这段）

**推荐 .csv（UTF-8 编码）**，首行写表头（中英文都认，顺序随意）：

```
姓名,英文名,组别,学号
张三,Zhang San,一组,20260101
李四,Li Si,二组,20260102
王五,Wang Wu,一组,20260103
```

英文名列可选；填了的话，界面和抽取结果显示为「张三 (Zhang San)」，英语课点名直接用。

| 格式 | 支持情况 | 说明 |
|---|---|---|
| .csv | 完整支持 | 推荐；分隔符自动识别（`,` `\t` `;`），支持引号包裹 |
| .txt | 完整支持 | 同上，按文本解析 |
| .md | 支持 | 识别 markdown 表格（`\| 张三 \| 一组 \|`） |
| .xlsx | 支持 | 内置 zip + inflate 解压直读，**无需任何依赖**；读取第一个工作表 |

**列名关键词（中英都认，大小写不敏感）**

| 用途 | 可写的列名 |
|---|---|
| 姓名 | 姓名 / 中文名 / 名字 / name |
| 英文名 | 英文名 / 英文 / 拼音 / english / name_en |
| 组别 | 组别 / 小组 / 组 / group / team |
| 学号 | 学号 / 编号 / number / no / id |

规则：
- 有表头就按列名匹配；**没有表头则按「姓名、组别、学号」顺序取前三列**。
- 组别为空的人归入「未分组」。
- 保存时自动写 UTF-8 BOM，Excel 双击不乱码。
- 样例见 `samples/`：`名单模板.csv`（推荐照抄）、`roster_en.csv`（纯英文表头）、`名单_GBK编码样例.csv`、`名单_UTF16编码样例.csv`。

### 编码：中文会不会乱码？

程序按 **BOM → 严格 UTF-8 校验 → 本地代码页（中文 Windows = GBK）** 三级自动识别，导入后状态栏会写明识别结果（如 `来源：csv/txt · GBK`）。

| 你手上的文件 | 会不会乱码 | 说明 |
|---|---|---|
| Excel「另存为 → **CSV UTF-8(逗号分隔)**」 | 不会 | 推荐做法，任何环境都稳 |
| Excel「另存为 → CSV(逗号分隔)」默认 | 不会（中文 Windows） | 存出来是 GBK，程序自动识别并转换 |
| 记事本另存为 UTF-8 | 不会 | |
| Excel 导出「Unicode 文本」 | 不会 | UTF-16，程序识别 BOM 后转换 |
| **在 wine 里打开 GBK 文件** | 会乱码 | wine 的 locale 是英文（代码页 1252），不是程序的问题；真机中文 Windows 正常 |

结论：**乱码来自文件编码，不是 wine**——但 wine 会让你误判，所以给老师的名单统一存成「CSV UTF-8」最省事。

## 功能

- **抽 1 人**：全班范围或点选某个组后组内抽
- **抽一组**：随机抽出一个组（先抽组再抽人也方便）
- **防重复**：开启后本轮不重名，抽完自动重置；R 手动重置
- **编辑关系**：加人（A）／改组（M）／删除（Del）
- **导入**：Ctrl+O，可选覆盖或追加
- **保存**：Ctrl+S 导出 csv
- **抽取记录**：每次记录时间、姓名、组和 seed，可导出留证（防"是不是内定"的质疑）

## 快捷键

| 键 | 作用 |
|---|---|
| 空格 | 随机抽 1 人 |
| G | 随机抽 1 个组 |
| N | 开关防重复 |
| R | 重置已抽记录 |
| A / M / Del | 加人 / 改组 / 删除 |
| Ctrl+O / Ctrl+S | 导入 / 保存 |
| H | 帮助 |

## 随机性

- Windows：`BCryptGenRandom`（系统级密码学随机数，非 `rand()`、非时间种子）
- 其他平台：`std::random_device` 兜底
- 取下标用**拒绝采样**，消除 `% n` 的取模偏差
- 每次抽取把 64 位随机种子的十六进制写进记录，可导出复查
- 图标与右上角徽标为 CTW 标（CreateTheWorld），ico 内嵌 256/48/32/16 四档，徽标走 GDI+ 保留透明通道
- 界面字体自动探测：按「存在 + 真有中文字形（GetGlyphIndices 校验 U+4E2D）」挑选（微软雅黑 → 苹方 → 思源黑体 → 宋体…），帮助窗口为自绘可滚动窗口、系统弹窗经 CBT 钩子套用同一字体——不再依赖 Consolas 的缺字形 fallback（旧版中文乱码的根因）

## 构建

```bash
# macOS 上直接产出 Windows exe（你的 mingw-w64 交叉工具链）
./build.sh

# 或用 cmake
cmake -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-mingw.cmake && cmake --build build

# 本机（macOS）用 Wine Staging 也能直接跑 exe 验证 GUI：
# "/Users/han2022/Documents/Hry/wine/Wine Staging.app/Contents/Resources/wine/bin/wine" picker.exe
```

`build.sh` 同时会生成本机 `picker_cli`，用于在本机验证解析逻辑：
```bash
./picker_cli import samples/people.csv
./picker_cli import samples/people.xlsx
```

## 目录

```
random-picker/
├── src/roster.h/.cpp     名单模型、导入导出、随机源
├── src/inflate.h         极简 DEFLATE 解压（为 xlsx 直读，零依赖）
├── src/picker_gui.cpp    Win32 深色 GUI（含 GDI+ 徽标绘制）
├── src/cli.cpp           命令行版（测试用 / 即 macOS 版）
├── resources.rc          图标与徽标资源声明
├── assets/ctw.ico        多尺寸 CTW 图标（256/48/32/16）
├── assets/icon_dark.png  右上角徽标（CreateTheWorld 原图）
├── samples/              样例名单（csv / md / xlsx）
├── CMakeLists.txt + toolchain-mingw.cmake
└── build.sh
```

## 已知限制

- xlsx 只读第一个工作表，不支持合并单元格与公式结果缓存之外的复杂情形（数值会被读成文本，够用）
- 不支持权重（默认绝对均等）；需要再加
- 名单上千人时左侧列表按 26px 行高滚动，暂无搜索框（需要可加）

---

byHry · CTW
