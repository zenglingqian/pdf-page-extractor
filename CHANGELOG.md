# 更新日志（Changelog）

本项目的所有重要变更都将记录在此文件中。

格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [1.0.1] - 2026-09-04

### 修复
- 修复对象流（ObjStm）内对象无法读取的问题：缓存键误用对象号，改为流内索引，解决现代生成器（xref 流 + 对象流）PDF「未能解析出任何页面」的问题
- 支持文件头 `%PDF-` 前存在垃圾字节（BOM/附加数据）的 PDF，引入偏移偏差（bias）修正
- 页面树损坏时回退扫描所有 `/Type /Page` 对象，提升兼容性

## [1.0.0] - 2026-09-04

### 新增
- 首个正式版本
- PDF 页面提取：支持单页与页码范围（起始页/结束页），结束页留空表示单页
- 多组范围批量提取，自动去重
- Excel 批量粘贴：从 Excel 复制两列数据直接 Ctrl+V 填入
- Excel 导入模板：下载模板 → 编辑 → 上传自动读取
- 文件拖放支持（PDF 与 Excel）
- 命令行模式：`PdfPageExtractor.exe in.pdf out.pdf 3 5 9-18` 与 `--count`
- 手写 PDF 解析器：支持经典 xref、PDF 1.5 xref 流、对象流（ObjStm）、FlateDecode
- 静态编译单 exe，无外部运行库依赖
