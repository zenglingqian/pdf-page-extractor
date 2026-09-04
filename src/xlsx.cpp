// xlsx.cpp - 最小 xlsx 读写实现
#include "xlsx.h"
#include "third_party/miniz/miniz.h"

#include <cstring>
#include <cstdio>
#include <map>
#include <sstream>

namespace pdfx {

// ---------------- XML 工具 ----------------
static std::string xmlEscape(const std::string& s){
    std::string o; o.reserve(s.size());
    for(char c: s){
        switch(c){
            case '&': o+="&amp;"; break;
            case '<': o+="&lt;"; break;
            case '>': o+="&gt;"; break;
            case '"': o+="&quot;"; break;
            default: o+=c;
        }
    }
    return o;
}

static std::string xmlUnescape(const std::string& s){
    std::string o; o.reserve(s.size());
    for(size_t i=0;i<s.size();){
        if(s[i]=='&'){
            if(s.compare(i,5,"&amp;")==0){ o+='&'; i+=5; continue; }
            if(s.compare(i,4,"&lt;")==0){ o+='<'; i+=4; continue; }
            if(s.compare(i,4,"&gt;")==0){ o+='>'; i+=4; continue; }
            if(s.compare(i,6,"&quot;")==0){ o+='"'; i+=6; continue; }
            if(s.compare(i,6,"&apos;")==0){ o+='\''; i+=6; continue; }
        }
        o+=s[i++];
    }
    return o;
}

static std::string colLetter(int col){ // 0 基
    std::string s;
    col++;
    while(col>0){ int r=(col-1)%26; s=(char)('A'+r)+s; col=(col-1)/26; }
    return s;
}

// ---------------- 写模板 ----------------
static const char* CONTENT_TYPES =
"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
"<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
"<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
"<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
"<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
"<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
"<Override PartName=\"/xl/worksheets/sheet2.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
"<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
"</Types>";

static const char* ROOT_RELS =
"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
"<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
"<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
"</Relationships>";

static const char* WORKBOOK =
"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
"<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
"xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
"<sheets><sheet name=\"\xE9\xA1\xB5\xE9\x9D\xA2\xE8\x8C\x83\xE5\x9B\xB4\" sheetId=\"1\" r:id=\"rId1\"/>"
"<sheet name=\"\xE5\xA1\xAB\xE5\x86\x99\xE8\xAF\xB4\xE6\x98\x8E\" sheetId=\"2\" r:id=\"rId2\"/></sheets>"
"</workbook>";

static const char* WORKBOOK_RELS =
"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
"<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
"<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
"<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet2.xml\"/>"
"<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
"</Relationships>";

static const char* STYLES =
"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
"<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
"<fonts count=\"1\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>"
"<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill></fills>"
"<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
"<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
"<cellXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/></cellXfs>"
"</styleSheet>";

// 单元格：文本
static void cellStr(std::string& out, int col, int row, const std::string& text){
    out+="<c r=\""+colLetter(col)+std::to_string(row)+"\" t=\"inlineStr\"><is><t xml:space=\"preserve\">";
    out+=xmlEscape(text);
    out+="</t></is></c>";
}
// 单元格：数字
static void cellNum(std::string& out, int col, int row, int v){
    out+="<c r=\""+colLetter(col)+std::to_string(row)+"\"><v>"+std::to_string(v)+"</v></c>";
}

static std::string buildSheet1(){
    std::string s;
    s+="<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    s+="<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">";
    s+="<cols><col min=\"1\" max=\"2\" width=\"14\"/></cols>";
    s+="<sheetData>";
    // 表头
    s+="<row r=\"1\">";
    cellStr(s,0,1,"\xE8\xB5\xB7\xE5\xA7\x8B\xE9\xA1\xB5"); // 起始页
    cellStr(s,1,1,"\xE7\xBB\x93\xE6\x9D\x9F\xE9\xA1\xB5"); // 结束页
    s+="</row>";
    // 示例：3 / 5 / 9-18
    s+="<row r=\"2\">"; cellNum(s,0,2,3); s+="</row>";
    s+="<row r=\"3\">"; cellNum(s,0,3,5); s+="</row>";
    s+="<row r=\"4\">"; cellNum(s,0,4,9); cellNum(s,1,4,18); s+="</row>";
    s+="</sheetData></worksheet>";
    return s;
}

static std::string buildSheet2(){
    std::string s;
    s+="<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    s+="<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">";
    s+="<cols><col min=\"1\" max=\"1\" width=\"72\"/></cols>";
    s+="<sheetData>";
    const char* lines[]={
        "PDF \xE9\xA1\xB5\xE9\x9D\xA2\xE6\x8F\x90\xE5\x8F\x96 - \xE5\xAF\xBC\xE5\x85\xA5\xE6\xA8\xA1\xE6\x9D\xBF\xE5\xA1\xAB\xE5\x86\x99\xE8\xAF\xB4\xE6\x98\x8E",
        "",
        "1. \xE3\x80\x8C\xE8\xB5\xB7\xE5\xA7\x8B\xE9\xA1\xB5\xE3\x80\x8D\xE5\x88\x97\xEF\xBC\x9A\xE5\xA1\xAB\xE5\x86\x99\xE8\xA6\x81\xE6\x8F\x90\xE5\x8F\x96\xE8\x8C\x83\xE5\x9B\xB4\xE7\x9A\x84\xE8\xB5\xB7\xE5\xA7\x8B\xE9\xA1\xB5\xE7\xA0\x81\xEF\xBC\x88\xE5\xBF\x85\xE5\xA1\xAB\xEF\xBC\x8C\xE6\xAD\xA3\xE6\x95\xB4\xE6\x95\xB0\xEF\xBC\x89\xE3\x80\x82",
        "2. \xE3\x80\x8C\xE7\xBB\x93\xE6\x9D\x9F\xE9\xA1\xB5\xE3\x80\x8D\xE5\x88\x97\xEF\xBC\x9A\xE5\xA1\xAB\xE5\x86\x99\xE7\xBB\x93\xE6\x9D\x9F\xE9\xA1\xB5\xE7\xA0\x81\xEF\xBC\x9B\xE7\x95\x99\xE7\xA9\xBA\xE8\xA1\xA8\xE7\xA4\xBA\xE5\x8F\xAA\xE6\x8F\x90\xE5\x8F\x96\xE8\xB5\xB7\xE5\xA7\x8B\xE9\xA1\xB5\xE8\xBF\x99\xE4\xB8\x80\xE9\xA1\xB5\xE3\x80\x82",
        "   \xE4\xBE\x8B\xEF\xBC\x9A\xE8\xB5\xB7\xE5\xA7\x8B\xE9\xA1\xB5 9\xE3\x80\x81\xE7\xBB\x93\xE6\x9D\x9F\xE9\xA1\xB5 18 \xE2\x86\x92 \xE6\x8F\x90\xE5\x8F\x96\xE7\xAC\xAC 9 \xE5\x88\xB0 18 \xE9\xA1\xB5\xE3\x80\x82",
        "   \xE4\xBE\x8B\xEF\xBC\x9A\xE8\xB5\xB7\xE5\xA7\x8B\xE9\xA1\xB5 3\xE3\x80\x81\xE7\xBB\x93\xE6\x9D\x9F\xE9\xA1\xB5\xE7\x95\x99\xE7\xA9\xBA \xE2\x86\x92 \xE5\x8F\xAA\xE6\x8F\x90\xE5\x8F\x96\xE7\xAC\xAC 3 \xE9\xA1\xB5\xE3\x80\x82",
        "3. \xE6\xAF\x8F\xE8\xA1\x8C\xE4\xBB\xA3\xE8\xA1\xA8\xE4\xB8\x80\xE7\xBB\x84\xE6\x8F\x90\xE5\x8F\x96\xE8\x8C\x83\xE5\x9B\xB4\xEF\xBC\x8C\xE5\x8F\xAF\xE5\xA1\xAB\xE5\x86\x99\xE4\xBB\xBB\xE6\x84\x8F\xE5\xA4\x9A\xE8\xA1\x8C\xE3\x80\x82",
        "4. \xE7\xAC\xAC\xE4\xB8\x80\xE8\xA1\x8C\xE4\xB8\xBA\xE8\xA1\xA8\xE5\xA4\xB4\xEF\xBC\x8C\xE8\xAF\xB7\xE5\x8B\xBF\xE4\xBF\xAE\xE6\x94\xB9\xE8\xA1\xA8\xE5\xA4\xB4\xE6\x96\x87\xE5\xAD\x97\xEF\xBC\x88\xE8\xB5\xB7\xE5\xA7\x8B\xE9\xA1\xB5 / \xE7\xBB\x93\xE6\x9D\x9F\xE9\xA1\xB5\xEF\xBC\x89\xE3\x80\x82",
        "5. \xE5\xA1\xAB\xE5\x86\x99\xE5\xAE\x8C\xE6\x88\x90\xE5\x90\x8E\xE4\xBF\x9D\xE5\xAD\x98\xEF\xBC\x8C\xE5\x9B\x9E\xE5\x88\xB0\xE8\xBD\xAF\xE4\xBB\xB6\xE7\x82\xB9\xE5\x87\xBB\xE3\x80\x8C\xE5\xAF\xBC\xE5\x85\xA5 Excel\xE3\x80\x8D\xE4\xB8\x8A\xE4\xBC\xA0\xE6\xAD\xA4\xE6\x96\x87\xE4\xBB\xB6\xE5\x8D\xB3\xE5\x8F\xAF\xE3\x80\x82",
    };
    for(int i=0;i<(int)(sizeof(lines)/sizeof(lines[0]));i++){
        s+="<row r=\""+std::to_string(i+1)+"\">";
        cellStr(s,0,i+1,lines[i]);
        s+="</row>";
    }
    s+="</sheetData></worksheet>";
    return s;
}

bool WriteTemplate(const std::string& outPath, std::string& err){
    mz_zip_archive zip; memset(&zip,0,sizeof(zip));
    if(!mz_zip_writer_init_file(&zip,outPath.c_str(),0)){
        err="创建 xlsx 文件失败"; return false;
    }
    auto add=[&](const char* name, const std::string& content)->bool{
        return mz_zip_writer_add_mem(&zip,name,content.data(),content.size(),MZ_DEFAULT_COMPRESSION)!=0;
    };
    bool ok = add("[Content_Types].xml",CONTENT_TYPES)
        && add("_rels/.rels",ROOT_RELS)
        && add("xl/workbook.xml",WORKBOOK)
        && add("xl/_rels/workbook.xml.rels",WORKBOOK_RELS)
        && add("xl/styles.xml",STYLES)
        && add("xl/worksheets/sheet1.xml",buildSheet1())
        && add("xl/worksheets/sheet2.xml",buildSheet2());
    if(!ok){ mz_zip_writer_end(&zip); err="写入 xlsx 内容失败"; return false; }
    if(!mz_zip_writer_finalize_archive(&zip)){ mz_zip_writer_end(&zip); err="打包 xlsx 失败"; return false; }
    mz_zip_writer_end(&zip);
    return true;
}

// ---------------- 读 ----------------
// 简易 XML 扫描：取 <tag ...>...</tag> 的内容（不处理嵌套同名标签，够用）
static bool findTag(const std::string& xml, const std::string& tag, size_t from,
                    size_t& contentStart, size_t& contentEnd, std::string& attrs){
    std::string open="<"+tag;
    size_t p=xml.find(open,from);
    if(p==std::string::npos) return false;
    size_t gt=xml.find('>',p);
    if(gt==std::string::npos) return false;
    attrs=xml.substr(p+open.size(), gt-(p+open.size()));
    // 自闭合
    if(!attrs.empty() && attrs.back()=='/'){
        attrs.pop_back();
        contentStart=gt+1; contentEnd=gt+1;
        return true;
    }
    std::string close="</"+tag+">";
    size_t ce=xml.find(close,gt+1);
    if(ce==std::string::npos) return false;
    contentStart=gt+1; contentEnd=ce;
    return true;
}

static std::string attrValue(const std::string& attrs, const std::string& name){
    std::string key=name+"=\"";
    size_t p=attrs.find(key);
    if(p==std::string::npos) return "";
    p+=key.size();
    size_t e=attrs.find('"',p);
    if(e==std::string::npos) return "";
    return attrs.substr(p,e-p);
}

static int colFromRef(const std::string& ref){
    int col=0;
    for(char c: ref){
        if(c>='A'&&c<='Z') col=col*26+(c-'A'+1);
        else if(c>='a'&&c<='z') col=col*26+(c-'a'+1);
        else break;
    }
    return col-1; // 0 基
}

static bool readZipEntry(mz_zip_archive& zip, const char* name, std::string& out){
    int idx=mz_zip_reader_locate_file(&zip,name,nullptr,0);
    if(idx<0) return false;
    size_t sz=0;
    void* p=mz_zip_reader_extract_to_heap(&zip,idx,&sz,0);
    if(!p) return false;
    out.assign((char*)p,sz);
    mz_free(p);
    return true;
}

bool ReadRanges(const std::string& inPath, std::vector<XlsxRow>& rows, std::string& err){
    mz_zip_archive zip; memset(&zip,0,sizeof(zip));
    if(!mz_zip_reader_init_file(&zip,inPath.c_str(),0)){
        err="无法打开 Excel 文件（不是有效的 .xlsx）"; return false;
    }
    std::string shared;
    readZipEntry(zip,"xl/sharedStrings.xml",shared);
    // 解析共享字符串
    std::vector<std::string> ss;
    {
        size_t from=0;
        while(true){
            size_t cs,ce; std::string attrs;
            if(!findTag(shared,"si",from,cs,ce,attrs)){ break; }
            std::string si=shared.substr(cs,ce-cs);
            // 拼接所有 <t>
            std::string text;
            size_t tf=0;
            while(true){
                size_t ts,te; std::string ta;
                if(!findTag(si,"t",tf,ts,te,ta)) break;
                text+=si.substr(ts,te-ts);
                tf=te;
            }
            ss.push_back(xmlUnescape(text));
            from=ce;
        }
    }
    // 读取第一个工作表
    std::string sheet;
    if(!readZipEntry(zip,"xl/worksheets/sheet1.xml",sheet)){
        mz_zip_reader_end(&zip);
        err="Excel 文件中找不到工作表数据"; return false;
    }
    mz_zip_reader_end(&zip);

    // 逐行解析
    struct Cell { int col; std::string text; };
    std::vector<std::vector<Cell>> grid;
    size_t from=0;
    while(true){
        size_t cs,ce; std::string attrs;
        if(!findTag(sheet,"row",from,cs,ce,attrs)) break;
        std::string rowXml=sheet.substr(cs,ce-cs);
        std::vector<Cell> row;
        size_t cf=0;
        while(true){
            size_t ccs,cce; std::string cattrs;
            if(!findTag(rowXml,"c",cf,ccs,cce,cattrs)) break;
            std::string cellXml=rowXml.substr(ccs,cce-ccs);
            std::string ref=attrValue(cattrs,"r");
            std::string type=attrValue(cattrs,"t");
            std::string text;
            // 取值：<v> 或 inlineStr 的 <t>
            size_t vs,ve; std::string vattrs;
            if(findTag(cellXml,"v",0,vs,ve,vattrs)){
                text=cellXml.substr(vs,ve-vs);
                if(type=="s"){
                    int idx=atoi(text.c_str());
                    if(idx>=0&&idx<(int)ss.size()) text=ss[idx];
                }
            } else if(type=="inlineStr"){
                size_t ts,te; std::string tattrs;
                if(findTag(cellXml,"t",0,ts,te,tattrs)) text=cellXml.substr(ts,te-ts);
            }
            Cell cell; cell.col=colFromRef(ref); cell.text=xmlUnescape(text);
            row.push_back(cell);
            cf=cce;
        }
        grid.push_back(row);
        from=ce;
    }
    if(grid.empty()){ err="Excel 表格内容为空"; return false; }

    // 定位表头
    int headerRow=-1, startCol=-1, endCol=-1;
    for(size_t i=0;i<grid.size()&&i<10;i++){
        for(auto& c: grid[i]){
            if(c.text.find("\xE8\xB5\xB7\xE5\xA7\x8B")!=std::string::npos) startCol=c.col; // 起始
            if(c.text.find("\xE7\xBB\x93\xE6\x9D\x9F")!=std::string::npos) endCol=c.col;   // 结束
        }
        if(startCol>=0){ headerRow=(int)i; break; }
    }
    int dataStart, sC, eC;
    if(headerRow>=0){ dataStart=headerRow+1; sC=startCol; eC=endCol; }
    else { dataStart=0; sC=0; eC=1; }

    auto getCell=[&](const std::vector<Cell>& row,int col)->std::string{
        for(auto& c: row) if(c.col==col) return c.text;
        return "";
    };
    auto isDigits=[](const std::string& s)->bool{
        if(s.empty()) return false;
        for(char c: s) if(c<'0'||c>'9') return false;
        return true;
    };

    for(size_t i=dataStart;i<grid.size();i++){
        std::string sv=getCell(grid[i],sC);
        std::string ev=(eC>=0)? getCell(grid[i],eC):"";
        // 去空白
        auto trim=[](std::string s){
            while(!s.empty()&&(s.front()==' '||s.front()=='\t')) s.erase(s.begin());
            while(!s.empty()&&(s.back()==' '||s.back()=='\t'||s.back()=='\r'||s.back()=='\n')) s.pop_back();
            return s;
        };
        sv=trim(sv); ev=trim(ev);
        // 数字可能被读成 "3" 或 "3.0"（数字单元格文本化后一般就是整数文本）
        if(sv.empty()) continue;
        if(!isDigits(sv)) continue; // 跳过非数字行（如重复表头）
        if(!ev.empty() && !isDigits(ev)) ev="";
        XlsxRow r; r.start=sv; r.end=ev;
        rows.push_back(r);
    }
    if(rows.empty()){
        err="未读取到有效数据：请确认第一列为「起始页」、第二列为「结束页」的数字";
        return false;
    }
    return true;
}

} // namespace pdfx
