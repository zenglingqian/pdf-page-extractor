# 贡献指南

感谢你对 PDF 页面提取工具的关注！欢迎通过以下方式贡献：

## 提交 Issue

- **Bug 报告**：请使用 Bug 报告模板，附上版本、复现步骤、错误提示，可能的话提供 PDF 样本（去除敏感内容）。
- **功能建议**：请说明使用场景与期望行为。

## 提交 Pull Request

1. Fork 本仓库并创建特性分支：`git checkout -b feature/xxx`
2. 确保构建通过：运行 `build.bat`（需要 MinGW-w64 g++ 8.1+）
3. 运行测试：
   ```bat
   python tests\make_test_pdf.py
   dist\PdfPageExtractor.exe tests\test_10pages.pdf out.pdf 3 5 9-10
   python tests\verify_extract.py
   ```
4. 遵循现有代码风格（4 空格缩进，中文注释）
5. 在 PR 描述中说明改动内容与测试结果

## 代码结构说明

- `src/pdfextract.cpp`：PDF 解析与提取核心，修改后务必用多种 PDF 回归测试
- `src/xlsx.cpp`：xlsx 读写，注意 XML 转义与共享字符串处理
- `src/main.cpp`：Win32 GUI 与 CLI 入口

## 许可证

提交代码即表示你同意以项目的 MIT 许可证发布你的贡献。
