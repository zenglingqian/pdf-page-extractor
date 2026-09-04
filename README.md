# PDF 页面提取工具（PDF Page Extractor）

一个轻量级的 Windows 桌面工具，用于从 PDF 文件中按页码或页码范围批量提取页面，生成新的 PDF 文件。支持从 Excel 批量导入页面范围。

无需安装任何运行库，单个 exe 即可运行；所有处理均在本地完成，PDF 文件不会上传到任何服务器。

## ✨ 功能特性

- **按页码提取**：输入起始页和结束页，提取指定范围的页面
  - 结束页留空 = 只提取该一页（例如：起始页 `3`、结束页留空 → 只提取第 3 页）
  - 支持多组范围，例如：第 3 页、第 5 页、第 9~18 页
- **Excel 批量粘贴**：在 Excel 中选中两列数据（第一列起始页、第二列结束页）复制，在软件的「起始页」输入框中直接 `Ctrl+V`，即可批量填入
- **Excel 模板导入**：内置「下载导入模板」功能，生成带示例和填写说明的 `.xlsx` 模板；编辑保存后通过「导入 Excel」自动读取并填入
- **拖放支持**：直接把 PDF 或 Excel 文件拖入窗口
- **命令行模式**：支持脚本化批量处理（见下文）
- **纯本地处理**：不联网、不上传，隐私安全
- **零依赖**：静态编译，单个 exe，无需安装 .NET / VC++ 运行库

## 📥 下载与安装

前往 [Releases](../../releases) 页面下载最新的 `PdfPageExtractor.exe`，双击即可运行。

> 要求：Windows 7 / 8 / 10 / 11（64 位）。

## 🚀 使用方法

### 图形界面（GUI）

1. **选择 PDF 文件**：点击「选择 PDF 文件…」按钮，或直接把 PDF 拖入窗口，软件会显示总页数。
2. **设置页面范围**：
   - 手动输入：在底部「起始页 / 结束页」输入框中填写，点击「添加」加入列表；
   - Excel 粘贴：在 Excel 中复制两列数据，在「起始页」输入框中 `Ctrl+V`；
   - 模板导入：点击「下载 Excel 导入模板」→ 编辑 → 点击「导入 Excel…」上传。
3. **执行提取**：点击「提取页面并保存…」，选择保存位置，完成。

**范围输入示例**（对应"提取第 3 页、第 5 页、第 9~18 页"）：

| # | 起始页 | 结束页 |
|---|--------|--------|
| 1 | 3      | （留空）|
| 2 | 5      | （留空）|
| 3 | 9      | 18     |

### 命令行（CLI）

```bat
:: 提取第 3 页、第 5 页、第 9~18 页
PdfPageExtractor.exe input.pdf output.pdf 3 5 9-18

:: 只查看 PDF 总页数
PdfPageExtractor.exe --count input.pdf
```

不带参数直接运行则打开图形界面。

## 🛠 从源码构建

需要 [MinGW-w64](https://www.mingw-w64.org/)（g++ 8.1+，需包含 `windres`），将其 `bin` 目录加入 `PATH` 后：

```bat
build.bat
```

产物位于 `dist\PdfPageExtractor.exe`。

项目唯一的第三方依赖是 [miniz](https://github.com/richgel999/miniz)（已随源码附带于 `src/third_party/miniz`，公共领域许可），用于 PDF 流的 Flate 解压与 xlsx（zip）读写。

## 📁 项目结构

```
pdf-page-extractor/
├── build.bat               # 一键构建脚本（MinGW-w64）
├── src/
│   ├── main.cpp            # Win32 GUI + 命令行入口
│   ├── pdfextract.h/.cpp   # PDF 解析与页面提取核心（手写解析器）
│   ├── xlsx.h/.cpp         # 最小 xlsx 读写（模板下载 / 导入）
│   ├── app.rc / app.manifest / app.ico  # 资源与清单
│   └── third_party/miniz/  # vendored miniz（公共领域）
├── tests/                  # 测试脚本与测试数据
└── dist/                   # 构建产物（exe）
```

## ✅ 测试

`tests/` 目录：

1、从考研政治考点清单.pdf文件中提取目录中章节的总结页为例
生成考研政治背诵手册.pdf

2、此外已验证场景：多范围提取、单页提取、全页提取、FlateDecode 压缩流、xlsx 模板写入/读取往返。

## ⚠️ 已知限制

- 不支持加密（带密码）的 PDF
- 不支持 PDF 1.5 之前的某些特殊压缩过滤器（如 LZWDecode、DCTDecode 图像流会原样保留，不影响页面提取）
- 提取结果保留原始页面内容与质量，不做重新渲染

## 📄 许可证

- 本项目采用 [MIT 许可证](LICENSE)
- 第三方依赖 [miniz](src/third_party/miniz/LICENSE)：公共领域（Public Domain）/ MIT

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！提交前请确保 `build.bat` 构建通过，并运行 `tests/` 中的测试。

## 🐛 反馈问题

遇到问题请在 [Issues](../../issues) 中提交，并尽量附上：

1. 软件版本（窗口标题中显示）
2. 操作步骤与错误提示截图
3. 如有可能，提供可复现的 PDF 样本（注意去除敏感内容）
