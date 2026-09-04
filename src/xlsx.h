#pragma once
// xlsx.h - 最小 xlsx 读写（基于 vendored miniz 的 zip 能力）
// 写：生成含两个工作表（页面范围 / 填写说明）的模板
// 读：解析第一个工作表，自动定位「起始页/结束页」列

#include <string>
#include <vector>

namespace pdfx {

struct XlsxRow {
    std::string start; // 起始页文本
    std::string end;   // 结束页文本（可空）
};

// 生成导入模板并写入 outPath（xlsx）。成功返回 true。
bool WriteTemplate(const std::string& outPath, std::string& err);

// 从 xlsx 读取页面范围。成功返回 true 并填充 rows。
bool ReadRanges(const std::string& inPath, std::vector<XlsxRow>& rows, std::string& err);

} // namespace pdfx
