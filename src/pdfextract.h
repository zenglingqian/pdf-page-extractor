#pragma once
// pdfextract.h - PDF 页面提取核心模块（无第三方 PDF 库依赖）
// 支持：经典 xref 表、PDF 1.5 xref 流与对象流（ObjStm）、FlateDecode。
// 不支持：加密 PDF（会返回明确错误信息）。

#include <string>
#include <vector>

namespace pdfx {

struct PageRange {
    int start = 0; // 起始页（1 基，含）
    int end = 0;   // 结束页（1 基，含）；等于 start 表示单页
};

// 从 inPdf 提取 ranges 指定的页面，写入 outPdf。
// 成功返回 true；失败返回 false 并通过 err 输出错误信息。
bool ExtractPages(const std::string& inPdf, const std::string& outPdf,
                  const std::vector<PageRange>& ranges, std::string& err);

// 获取 PDF 页数。
bool GetPageCount(const std::string& inPdf, int& count, std::string& err);

// 从内存数据获取页数（GUI 上传后复用已读入的数据）。
bool GetPageCountFromMemory(const unsigned char* data, size_t size, int& count, std::string& err);

} // namespace pdfx
