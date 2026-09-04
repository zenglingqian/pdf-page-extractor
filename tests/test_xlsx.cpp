// test_xlsx.cpp - xlsx 模板写入/读取往返测试
#include <cstdio>
#include <string>
#include <vector>
#include "../src/xlsx.h"

int main(){
    std::string err;
    if(!pdfx::WriteTemplate("test_tpl.xlsx", err)){
        printf("WriteTemplate FAIL: %s\n", err.c_str());
        return 1;
    }
    printf("WriteTemplate OK\n");
    std::vector<pdfx::XlsxRow> rows;
    if(!pdfx::ReadRanges("test_tpl.xlsx", rows, err)){
        printf("ReadRanges FAIL: %s\n", err.c_str());
        return 1;
    }
    printf("ReadRanges OK, %d rows:\n", (int)rows.size());
    for(auto& r: rows) printf("  start=%s end=%s\n", r.start.c_str(), r.end.c_str());
    // 期望：3/空, 5/空, 9/18
    bool ok = rows.size()==3
        && rows[0].start=="3" && rows[0].end==""
        && rows[1].start=="5" && rows[1].end==""
        && rows[2].start=="9" && rows[2].end=="18";
    printf(ok? "ROUND-TRIP PASS\n":"ROUND-TRIP FAIL\n");
    return ok?0:1;
}
