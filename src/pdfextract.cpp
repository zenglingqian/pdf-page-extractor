// pdfextract.cpp - PDF 页面提取核心实现
// 纯手写解析器，无第三方 PDF 库。压缩解码使用 vendored miniz。
#include "pdfextract.h"
#include "third_party/miniz/miniz.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <map>
#include <set>
#include <memory>
#include <deque>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace pdfx
{

    // ---------------- 对象模型 ----------------
    enum class T
    {
        Null,
        Bool,
        Num,
        Str,
        Name,
        Arr,
        Dict,
        Stream,
        Ref
    };

    struct Obj;
    using P = std::shared_ptr<Obj>;

    struct Obj
    {
        T t = T::Null;
        bool b = false;
        double n = 0.0;
        std::string s;                            // Str / Name
        std::vector<P> a;                         // Arr
        std::vector<std::pair<std::string, P>> d; // Dict（保序）
        std::string raw;                          // Stream 原始字节
        int rn = 0, rg = 0;                       // Ref

        P get(const std::string &k) const
        {
            for (auto &kv : d)
                if (kv.first == k)
                    return kv.second;
            return nullptr;
        }
        bool has(const std::string &k) const { return get(k) != nullptr; }
        void set(const std::string &k, P v)
        {
            for (auto &kv : d)
                if (kv.first == k)
                {
                    kv.second = v;
                    return;
                }
            d.push_back({k, v});
        }
        void erase(const std::string &k)
        {
            d.erase(std::remove_if(d.begin(), d.end(),
                                   [&](const std::pair<std::string, P> &kv)
                                   { return kv.first == k; }),
                    d.end());
        }
    };

    static P mkNull()
    {
        auto o = std::make_shared<Obj>();
        o->t = T::Null;
        return o;
    }
    static P mkBool(bool v)
    {
        auto o = std::make_shared<Obj>();
        o->t = T::Bool;
        o->b = v;
        return o;
    }
    static P mkNum(double v)
    {
        auto o = std::make_shared<Obj>();
        o->t = T::Num;
        o->n = v;
        return o;
    }
    static P mkStr(std::string v)
    {
        auto o = std::make_shared<Obj>();
        o->t = T::Str;
        o->s = std::move(v);
        return o;
    }
    static P mkName(std::string v)
    {
        auto o = std::make_shared<Obj>();
        o->t = T::Name;
        o->s = std::move(v);
        return o;
    }
    static P mkArr()
    {
        auto o = std::make_shared<Obj>();
        o->t = T::Arr;
        return o;
    }
    static P mkDict()
    {
        auto o = std::make_shared<Obj>();
        o->t = T::Dict;
        return o;
    }
    static P mkRef(int n, int g)
    {
        auto o = std::make_shared<Obj>();
        o->t = T::Ref;
        o->rn = n;
        o->rg = g;
        return o;
    }

    // ---------------- 字节缓冲 ----------------
    struct Buf
    {
        const unsigned char *p;
        size_t n;
    };

    static bool isWS(unsigned char c) { return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == ' '; }
    static bool isDelim(unsigned char c)
    {
        return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' || c == '%';
    }
    static bool isDigit(unsigned char c) { return c >= '0' && c <= '9'; }

    struct Reader
    {
        Buf b;
        size_t pos = 0;
        Reader(const unsigned char *d, size_t n)
        {
            b.p = d;
            b.n = n;
        }
        bool eof() const { return pos >= b.n; }
        unsigned char cur() const { return pos < b.n ? b.p[pos] : 0; }
        unsigned char at(size_t i) const { return i < b.n ? b.p[i] : 0; }
        void skipWs()
        {
            while (pos < b.n)
            {
                unsigned char c = b.p[pos];
                if (isWS(c))
                {
                    pos++;
                    continue;
                }
                if (c == '%')
                {
                    while (pos < b.n && b.p[pos] != '\n' && b.p[pos] != '\r')
                        pos++;
                    continue;
                }
                break;
            }
        }
        bool match(const char *kw)
        {
            size_t L = strlen(kw);
            if (pos + L > b.n)
                return false;
            if (memcmp(b.p + pos, kw, L) != 0)
                return false;
            // 关键字边界
            if (pos + L < b.n)
            {
                unsigned char c = b.p[pos + L];
                if (!isWS(c) && !isDelim(c))
                    return false;
            }
            pos += L;
            return true;
        }
    };

    static int hexVal(unsigned char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    // 解析数字，返回是否为整数并通过 isInt 输出
    static double parseNumber(Reader &r, bool &isInt)
    {
        r.skipWs();
        std::string tok;
        while (!r.eof())
        {
            unsigned char c = r.cur();
            if (isDigit(c) || c == '+' || c == '-' || c == '.')
            {
                tok += (char)c;
                r.pos++;
            }
            else
                break;
        }
        isInt = (tok.find('.') == std::string::npos);
        if (tok.empty())
            return 0.0;
        return atof(tok.c_str());
    }

    static long long parseInt(Reader &r)
    {
        r.skipWs();
        long long sign = 1, v = 0;
        bool any = false;
        if (r.cur() == '+')
        {
            r.pos++;
        }
        else if (r.cur() == '-')
        {
            sign = -1;
            r.pos++;
        }
        while (!r.eof() && isDigit(r.cur()))
        {
            v = v * 10 + (r.cur() - '0');
            r.pos++;
            any = true;
        }
        return any ? sign * v : 0;
    }

    static P parseObject(Reader &r);

    static P parseName(Reader &r)
    {
        // 当前在 '/'
        r.pos++; // 跳过 /
        std::string s;
        while (!r.eof())
        {
            unsigned char c = r.cur();
            if (isWS(c) || isDelim(c))
                break;
            if (c == '#' && r.pos + 2 < r.b.n)
            {
                int h = hexVal(r.at(r.pos + 1)), l = hexVal(r.at(r.pos + 2));
                if (h >= 0 && l >= 0)
                {
                    s += (char)(h * 16 + l);
                    r.pos += 3;
                    continue;
                }
            }
            s += (char)c;
            r.pos++;
        }
        return mkName(s);
    }

    static P parseLiteralString(Reader &r)
    {
        // 当前在 '('
        r.pos++;
        std::string s;
        int depth = 1;
        while (!r.eof())
        {
            unsigned char c = r.cur();
            if (c == '\\')
            {
                r.pos++;
                if (r.eof())
                    break;
                unsigned char e = r.cur();
                switch (e)
                {
                case 'n':
                    s += '\n';
                    r.pos++;
                    break;
                case 'r':
                    s += '\r';
                    r.pos++;
                    break;
                case 't':
                    s += '\t';
                    r.pos++;
                    break;
                case 'b':
                    s += '\b';
                    r.pos++;
                    break;
                case 'f':
                    s += '\f';
                    r.pos++;
                    break;
                case '(':
                    s += '(';
                    r.pos++;
                    break;
                case ')':
                    s += ')';
                    r.pos++;
                    break;
                case '\\':
                    s += '\\';
                    r.pos++;
                    break;
                case '\r':
                    r.pos++;
                    if (r.cur() == '\n')
                        r.pos++;
                    break;
                case '\n':
                    r.pos++;
                    break;
                default:
                    if (e >= '0' && e <= '7')
                    {
                        int v = 0, cnt = 0;
                        while (cnt < 3 && r.cur() >= '0' && r.cur() <= '7')
                        {
                            v = v * 8 + (r.cur() - '0');
                            r.pos++;
                            cnt++;
                        }
                        s += (char)v;
                    }
                    else
                    {
                        s += (char)e;
                        r.pos++;
                    }
                }
                continue;
            }
            if (c == '(')
            {
                depth++;
                s += (char)c;
                r.pos++;
                continue;
            }
            if (c == ')')
            {
                depth--;
                r.pos++;
                if (depth == 0)
                    break;
                s += (char)c;
                continue;
            }
            s += (char)c;
            r.pos++;
        }
        return mkStr(s);
    }

    static P parseHexString(Reader &r)
    {
        // 当前在 '<'（单个）
        r.pos++;
        std::string hex;
        while (!r.eof() && r.cur() != '>')
        {
            if (!isWS(r.cur()))
                hex += (char)r.cur();
            r.pos++;
        }
        if (!r.eof())
            r.pos++; // '>'
        if (hex.size() % 2)
            hex += '0';
        std::string s;
        s.reserve(hex.size() / 2);
        for (size_t i = 0; i + 1 < hex.size(); i += 2)
        {
            int h = hexVal((unsigned char)hex[i]), l = hexVal((unsigned char)hex[i + 1]);
            if (h < 0)
                h = 0;
            if (l < 0)
                l = 0;
            s += (char)(h * 16 + l);
        }
        return mkStr(s);
    }

    static P parseArray(Reader &r)
    {
        r.pos++; // '['
        auto arr = mkArr();
        while (true)
        {
            r.skipWs();
            if (r.eof())
                break;
            if (r.cur() == ']')
            {
                r.pos++;
                break;
            }
            P v = parseObject(r);
            if (!v)
                break;
            arr->a.push_back(v);
        }
        return arr;
    }

    static P parseDict(Reader &r)
    {
        r.pos += 2; // '<<'
        auto d = mkDict();
        while (true)
        {
            r.skipWs();
            if (r.eof())
                break;
            if (r.cur() == '>' && r.at(r.pos + 1) == '>')
            {
                r.pos += 2;
                break;
            }
            if (r.cur() != '/')
            {
                r.pos++;
                continue;
            } // 容错
            P key = parseName(r);
            r.skipWs();
            P val = parseObject(r);
            if (!val)
                val = mkNull();
            d->set(key->s, val);
        }
        return d;
    }

    static P parseObject(Reader &r)
    {
        r.skipWs();
        if (r.eof())
            return nullptr;
        unsigned char c = r.cur();
        if (c == '<')
        {
            if (r.at(r.pos + 1) == '<')
            {
                P d = parseDict(r);
                // 检查是否为流对象
                r.skipWs();
                if (!r.eof() && r.cur() == 's' && r.match("stream"))
                {
                    // stream 后跟 \r\n 或 \n
                    if (!r.eof() && r.cur() == '\r')
                        r.pos++;
                    if (!r.eof() && r.cur() == '\n')
                        r.pos++;
                    size_t len = 0;
                    P L = d->get("Length");
                    if (L && L->t == T::Num)
                        len = (size_t)L->n;
                    d->t = T::Stream;
                    if (len > 0 && r.pos + len <= r.b.n)
                    {
                        d->raw.assign((const char *)r.b.p + r.pos, len);
                        r.pos += len;
                    }
                    else
                    {
                        // 长度不可靠：扫描 endstream
                        size_t e = r.pos;
                        while (e + 9 <= r.b.n && memcmp(r.b.p + e, "endstream", 9) != 0)
                            e++;
                        d->raw.assign((const char *)r.b.p + r.pos, e - r.pos);
                        // 去掉末尾换行
                        while (!d->raw.empty() && (d->raw.back() == '\n' || d->raw.back() == '\r'))
                            d->raw.pop_back();
                        r.pos = e;
                    }
                    r.match("endstream");
                }
                return d;
            }
            return parseHexString(r);
        }
        if (c == '(')
            return parseLiteralString(r);
        if (c == '/')
            return parseName(r);
        if (c == '[')
            return parseArray(r);
        if (c == 't')
        {
            if (r.match("true"))
                return mkBool(true);
            r.pos++;
            return mkNull();
        }
        if (c == 'f')
        {
            if (r.match("false"))
                return mkBool(false);
            r.pos++;
            return mkNull();
        }
        if (c == 'n')
        {
            if (r.match("null"))
                return mkNull();
            r.pos++;
            return mkNull();
        }
        if (isDigit(c) || c == '+' || c == '-' || c == '.')
        {
            bool isInt = false;
            size_t save = r.pos;
            double n1 = parseNumber(r, isInt);
            if (isInt)
            {
                // 尝试 "N G R"
                size_t p = r.pos;
                // skip ws manually
                while (p < r.b.n && isWS(r.b.p[p]))
                    p++;
                if (p < r.b.n && isDigit(r.b.p[p]))
                {
                    Reader r2(r.b.p, r.b.n);
                    r2.pos = p;
                    long long g = parseInt(r2);
                    size_t q = r2.pos;
                    while (q < r.b.n && isWS(r.b.p[q]))
                        q++;
                    if (q < r.b.n && r.b.p[q] == 'R')
                    {
                        unsigned char nx = (q + 1 < r.b.n) ? r.b.p[q + 1] : 0;
                        if (nx == 0 || isWS(nx) || isDelim(nx))
                        {
                            r.pos = q + 1;
                            return mkRef((int)n1, (int)g);
                        }
                    }
                }
            }
            (void)save;
            return mkNum(n1);
        }
        // 未知，跳过一个字节
        r.pos++;
        return mkNull();
    }

    // ---------------- Flate 解码 + PNG 反过滤 ----------------
    static bool flateDecode(const std::string &in, std::string &out)
    {
        mz_ulong dl = (mz_ulong)(in.size() * 4 + 64);
        std::string tmp;
        tmp.resize(dl);
        mz_ulong actual = dl;
        int st = mz_uncompress((unsigned char *)tmp.data(), &actual,
                               (const unsigned char *)in.data(), (mz_ulong)in.size());
        if (st != MZ_OK)
        {
            // 重试更大缓冲
            dl = (mz_ulong)(in.size() * 20 + 1024);
            tmp.resize(dl);
            actual = dl;
            st = mz_uncompress((unsigned char *)tmp.data(), &actual,
                               (const unsigned char *)in.data(), (mz_ulong)in.size());
            if (st != MZ_OK)
                return false;
        }
        tmp.resize(actual);
        out = tmp;
        return true;
    }

    static void pngUnfilter(std::string &data, size_t bytesPerRow)
    {
        if (bytesPerRow == 0)
            return;
        size_t stride = bytesPerRow + 1;
        size_t rows = data.size() / stride;
        std::vector<unsigned char> prev(bytesPerRow, 0), cur(bytesPerRow, 0);
        std::string out;
        out.reserve(rows * bytesPerRow);
        for (size_t r = 0; r < rows; r++)
        {
            unsigned char ft = (unsigned char)data[r * stride];
            for (size_t i = 0; i < bytesPerRow; i++)
            {
                unsigned char x = (unsigned char)data[r * stride + 1 + i];
                unsigned char a = i > 0 ? cur[i - 1] : 0;
                unsigned char bch = prev[i];
                unsigned char cch = i > 0 ? prev[i - 1] : 0;
                unsigned char v;
                switch (ft)
                {
                case 0:
                    v = x;
                    break;
                case 1:
                    v = x + a;
                    break;
                case 2:
                    v = x + bch;
                    break;
                case 3:
                    v = x + (unsigned char)((a + bch) / 2);
                    break;
                case 4:
                {
                    int p = a + bch - cch, pa = abs(p - a), pb = abs(p - bch), pc = abs(p - cch);
                    unsigned char pr = (pa <= pb && pa <= pc) ? a : (pb <= pc ? bch : cch);
                    v = x + pr;
                    break;
                }
                default:
                    v = x;
                }
                cur[i] = v;
            }
            out.append((char *)cur.data(), bytesPerRow);
            prev = cur;
        }
        data = out;
    }

    // ---------------- 文档解析 ----------------
    struct XrefEntry
    {
        int type;
        size_t f2;
        int f3;
    };

    class Doc
    {
    public:
        std::string data;
        std::map<int, XrefEntry> xref;
        std::map<int, P> cache;
        std::map<int, std::map<int, P>> objStmCache;
        int rootNum = -1;
        size_t bias = 0; // 文件头前垃圾字节导致的偏移偏差
        std::string err;

        bool load(const std::string &bytes)
        {
            data = bytes;
            // 允许 %PDF- 前有少量垃圾字节（真实文件常见）
            size_t hdr = data.find("%PDF-");
            if (hdr == std::string::npos || hdr > 1024)
            {
                err = "不是有效的 PDF 文件（缺少 %PDF- 头）";
                return false;
            }
            // 加密检测
            if (data.find("/Encrypt") != std::string::npos)
            {
                // 粗略：只有 trailer 里的 /Encrypt 才算；这里简单提示
            }
            long long sx = findStartXref();
            if (sx < 0)
            {
                err = "找不到 startxref";
                return false;
            }
            bias = hdr;
            P trailerDict;
            if (!buildXref((size_t)sx + bias, trailerDict))
            {
                return false;
            }
            if (!trailerDict)
            {
                err = "找不到 trailer";
                return false;
            }
            if (trailerDict->has("Encrypt"))
            {
                err = "不支持加密的 PDF 文件";
                return false;
            }
            P root = trailerDict->get("Root");
            if (!root || root->t != T::Ref)
            {
                err = "trailer 缺少 /Root";
                return false;
            }
            rootNum = root->rn;
            return true;
        }

        long long findStartXref()
        {
            // 从末尾向前找 startxref
            size_t n = data.size();
            size_t scan = n > 2048 ? n - 2048 : 0;
            size_t found = data.rfind("startxref");
            if (found == std::string::npos || found < scan)
            {
                found = data.rfind("startxref");
                if (found == std::string::npos)
                    return -1;
            }
            Reader r((const unsigned char *)data.data(), n);
            r.pos = found + 9;
            r.skipWs();
            return parseInt(r);
        }

        P parseObjectAt(size_t offset)
        {
            Reader r((const unsigned char *)data.data(), data.size());
            r.pos = offset;
            r.skipWs();
            // 期望 "N G obj"
            parseInt(r); // obj num
            parseInt(r); // gen
            r.skipWs();
            if (!r.match("obj"))
            {
                // 容错：直接尝试解析
            }
            return parseObject(r);
        }

        bool buildXref(size_t offset, P &mainTrailer)
        {
            std::set<size_t> visited;
            size_t cur = offset;
            bool first = true;
            while (cur != 0)
            {
                if (visited.count(cur))
                    break;
                visited.insert(cur);
                P nextPrev;
                if (!parseXrefSection(cur, mainTrailer, nextPrev, first))
                    return false;
                first = false;
                if (!nextPrev || nextPrev->t != T::Num)
                    break;
                cur = (size_t)nextPrev->n + bias;
            }
            return true;
        }

        bool parseXrefSection(size_t offset, P &mainTrailer, P &prevOut, bool first)
        {
            Reader r((const unsigned char *)data.data(), data.size());
            r.pos = offset;
            r.skipWs();
            if (r.cur() == 'x')
            {
                // 经典 xref 表
                if (!r.match("xref"))
                {
                    err = "xref 解析失败";
                    return false;
                }
                while (true)
                {
                    r.skipWs();
                    if (r.eof())
                        break;
                    if (r.cur() == 't')
                    { // trailer
                        r.match("trailer");
                        r.skipWs();
                        P td = parseObject(r);
                        if (first)
                            mainTrailer = td;
                        prevOut = td ? td->get("Prev") : nullptr;
                        break;
                    }
                    long long start = parseInt(r);
                    long long cnt = parseInt(r);
                    for (long long i = 0; i < cnt; i++)
                    {
                        r.skipWs();
                        // 读取 "0000000000 65535 f"
                        std::string off, gen;
                        while (!r.eof() && isDigit(r.cur()))
                        {
                            off += (char)r.cur();
                            r.pos++;
                        }
                        r.skipWs();
                        while (!r.eof() && isDigit(r.cur()))
                        {
                            gen += (char)r.cur();
                            r.pos++;
                        }
                        r.skipWs();
                        char typ = 'f';
                        if (!r.eof() && (r.cur() == 'f' || r.cur() == 'n'))
                        {
                            typ = (char)r.cur();
                            r.pos++;
                        }
                        int objNum = (int)(start + i);
                        if (typ == 'n' && !xref.count(objNum))
                        {
                            XrefEntry e;
                            e.type = 1;
                            e.f2 = (size_t)atoll(off.c_str());
                            e.f3 = atoi(gen.c_str());
                            if (e.f2 > 0)
                                xref[objNum] = e;
                        }
                    }
                }
                return true;
            }
            else
            {
                // xref 流
                P o = parseObjectAt(offset);
                if (!o || o->t != T::Stream)
                {
                    err = "xref 流解析失败";
                    return false;
                }
                if (first)
                    mainTrailer = o; // 流对象本身就是 trailer 字典
                prevOut = o->get("Prev");
                // 解码流
                std::string dec;
                if (!decodeStream(o, dec))
                {
                    err = "xref 流解码失败";
                    return false;
                }
                P W = o->get("W");
                P Size = o->get("Size");
                if (!W || W->t != T::Arr || W->a.size() < 3)
                {
                    err = "xref 流缺少 /W";
                    return false;
                }
                int w0 = (int)W->a[0]->n, w1 = (int)W->a[1]->n, w2 = (int)W->a[2]->n;
                int size = Size ? (int)Size->n : 0;
                size_t ew = w0 + w1 + w2;
                if (ew == 0)
                {
                    err = "xref 流 /W 无效";
                    return false;
                }
                // Index
                std::vector<std::pair<int, int>> idx;
                P Index = o->get("Index");
                if (Index && Index->t == T::Arr)
                {
                    for (size_t i = 0; i + 1 < Index->a.size(); i += 2)
                        idx.push_back({(int)Index->a[i]->n, (int)Index->a[i + 1]->n});
                }
                else
                {
                    idx.push_back({0, size});
                }
                size_t p = 0;
                auto rd = [&](int w) -> size_t
                {
                    size_t v = 0;
                    for (int k = 0; k < w; k++)
                    {
                        v = (v << 8) | (p < dec.size() ? (unsigned char)dec[p] : 0);
                        p++;
                    }
                    return v;
                };
                for (auto &seg : idx)
                {
                    for (int i = 0; i < seg.second; i++)
                    {
                        int objNum = seg.first + i;
                        int t1 = w0 ? (int)rd(w0) : 1;
                        size_t f2 = rd(w1);
                        int f3 = w2 ? (int)rd(w2) : 0;
                        if (!xref.count(objNum))
                        {
                            XrefEntry e;
                            e.type = t1;
                            e.f2 = f2;
                            e.f3 = f3;
                            xref[objNum] = e;
                        }
                    }
                }
                return true;
            }
        }

        bool decodeStream(P o, std::string &out)
        {
            if (!o || o->t != T::Stream)
                return false;
            P F = o->get("Filter");
            std::string raw = o->raw;
            if (!F || (F->t == T::Name && F->s == "null"))
            {
                out = raw;
                return true;
            }
            std::vector<std::string> filters;
            if (F->t == T::Name)
                filters.push_back(F->s);
            else if (F->t == T::Arr)
                for (auto &x : F->a)
                    if (x->t == T::Name)
                        filters.push_back(x->s);
            std::string cur = raw;
            for (auto &f : filters)
            {
                if (f == "FlateDecode" || f == "Fl")
                {
                    std::string tmp;
                    if (!flateDecode(cur, tmp))
                        return false;
                    cur = tmp;
                }
                else if (f == "ASCIIHexDecode" || f == "AHx")
                {
                    std::string h;
                    bool done = false;
                    for (char ch : cur)
                    {
                        if (ch == '>')
                        {
                            done = true;
                            break;
                        }
                        if (!isWS((unsigned char)ch))
                            h += ch;
                    }
                    (void)done;
                    if (h.size() % 2)
                        h += '0';
                    std::string b;
                    for (size_t i = 0; i + 1 < h.size(); i += 2)
                    {
                        int x = hexVal((unsigned char)h[i]), y = hexVal((unsigned char)h[i + 1]);
                        b += (char)((x < 0 ? 0 : x) * 16 + (y < 0 ? 0 : y));
                    }
                    cur = b;
                }
                else
                {
                    return false; // 不支持的过滤器
                }
            }
            // Predictor
            P DP = o->get("DecodeParms");
            P parms = DP;
            if (DP && DP->t == T::Arr && !DP->a.empty())
                parms = DP->a[0];
            if (parms && parms->t == T::Dict)
            {
                P pred = parms->get("Predictor");
                if (pred && pred->n >= 10)
                {
                    int cols = 1;
                    P C = parms->get("Columns");
                    if (C)
                        cols = (int)C->n;
                    pngUnfilter(cur, (size_t)cols);
                }
            }
            out = cur;
            return true;
        }

        P getObject(int num)
        {
            auto it = cache.find(num);
            if (it != cache.end())
                return it->second;
            auto xe = xref.find(num);
            if (xe == xref.end())
            {
                cache[num] = nullptr;
                return nullptr;
            }
            P result;
            if (xe->second.type == 1)
            {
                result = parseObjectAt(xe->second.f2 + bias);
            }
            else if (xe->second.type == 2)
            {
                int stmNum = (int)xe->second.f2;
                int idx = xe->second.f3;
                auto &m = objStmCache[stmNum];
                if (m.empty())
                {
                    P so = getObjectDirect(stmNum);
                    if (so && so->t == T::Stream)
                    {
                        std::string dec;
                        if (decodeStream(so, dec))
                        {
                            P N = so->get("N"), First = so->get("First");
                            if (N && First)
                            {
                                int n = (int)N->n, firstOff = (int)First->n;
                                Reader r((const unsigned char *)dec.data(), dec.size());
                                r.pos = 0;
                                std::vector<std::pair<int, size_t>> hdr;
                                for (int i = 0; i < n; i++)
                                {
                                    long long on = parseInt(r);
                                    long long off = parseInt(r);
                                    hdr.push_back({(int)on, (size_t)off});
                                }
                                for (int i = 0; i < n; i++)
                                {
                                    Reader rr((const unsigned char *)dec.data(), dec.size());
                                    rr.pos = firstOff + hdr[i].second;
                                    P v = parseObject(rr);
                                    // 按索引存储（xref type 2 的第三字段是流内索引）
                                    m[i] = v;
                                }
                            }
                        }
                    }
                }
                auto mit = m.find(idx);
                result = (mit != m.end()) ? mit->second : nullptr;
            }
            cache[num] = result;
            return result;
        }

        // 不走缓存直接取（避免 ObjStm 递归）
        P getObjectDirect(int num)
        {
            auto xe = xref.find(num);
            if (xe == xref.end())
                return nullptr;
            if (xe->second.type == 1)
                return parseObjectAt(xe->second.f2 + bias);
            return nullptr;
        }

        // 收集页面（按文档顺序）
        void collectPages(int num, std::vector<int> &out, std::set<int> &seen, int depth)
        {
            if (depth > 200)
                return;
            if (seen.count(num))
                return;
            seen.insert(num);
            P o = getObject(num);
            if (!o || o->t != T::Dict)
                return;
            P ty = o->get("Type");
            std::string tname = (ty && ty->t == T::Name) ? ty->s : "";
            if (tname == "Pages")
            {
                P kids = o->get("Kids");
                if (kids && kids->t == T::Arr)
                {
                    for (auto &k : kids->a)
                    {
                        if (k->t == T::Ref)
                            collectPages(k->rn, out, seen, depth + 1);
                    }
                }
            }
            else if (tname == "Page")
            {
                out.push_back(num);
            }
            else
            {
                // 无 /Type：若有 /Kids 视为 Pages，否则视为 Page
                if (o->has("Kids"))
                {
                    P kids = o->get("Kids");
                    if (kids && kids->t == T::Arr)
                        for (auto &k : kids->a)
                            if (k->t == T::Ref)
                                collectPages(k->rn, out, seen, depth + 1);
                }
                else
                {
                    out.push_back(num);
                }
            }
        }

        std::vector<int> getPageList()
        {
            std::vector<int> out;
            if (rootNum < 0)
                return out;
            P root = getObject(rootNum);
            if (!root)
                return out;
            P pages = root->get("Pages");
            int pagesNum = -1;
            if (pages && pages->t == T::Ref)
                pagesNum = pages->rn;
            else if (pages && pages->t == T::Dict)
            {
                // 内联，少见；尝试从 root 直接收集
            }
            if (pagesNum < 0)
                return out;
            std::set<int> seen;
            collectPages(pagesNum, out, seen, 0);
            if (out.empty())
            {
                // 回退：扫描所有对象找 /Type /Page（页面树损坏时）
                for (auto &xe : xref)
                {
                    P o = getObject(xe.first);
                    if (!o || o->t != T::Dict)
                        continue;
                    P ty = o->get("Type");
                    if (ty && ty->t == T::Name && ty->s == "Page")
                        out.push_back(xe.first);
                }
            }
            return out;
        }
    };

    // ---------------- 写出 ----------------
    static void writeName(std::string &out, const std::string &name)
    {
        out += '/';
        for (unsigned char c : name)
        {
            if (c < 33 || c > 126 || isDelim(c) || c == '#')
            {
                char buf[4];
                snprintf(buf, 4, "#%02X", c);
                out += buf;
            }
            else
                out += (char)c;
        }
    }
    static void writeHexStr(std::string &out, const std::string &s)
    {
        out += '<';
        static const char *H = "0123456789ABCDEF";
        for (unsigned char c : s)
        {
            out += H[c >> 4];
            out += H[c & 15];
        }
        out += '>';
    }
    static void writeNum(std::string &out, double n)
    {
        if (std::isfinite(n) && n == floor(n) && fabs(n) < 1e15)
        {
            char buf[32];
            snprintf(buf, 32, "%lld", (long long)n);
            out += buf;
        }
        else
        {
            char buf[40];
            snprintf(buf, 40, "%.6f", n);
            out += buf;
        }
    }

    static void serialize(P o, std::string &out, const std::map<int, int> &remap);

    static void serializeDictEntries(const std::vector<std::pair<std::string, P>> &d,
                                     std::string &out, const std::map<int, int> &remap)
    {
        out += "<<";
        for (auto &kv : d)
        {
            out += ' ';
            writeName(out, kv.first);
            out += ' ';
            serialize(kv.second, out, remap);
        }
        out += " >>";
    }

    static void serialize(P o, std::string &out, const std::map<int, int> &remap)
    {
        if (!o)
        {
            out += "null";
            return;
        }
        switch (o->t)
        {
        case T::Null:
            out += "null";
            break;
        case T::Bool:
            out += o->b ? "true" : "false";
            break;
        case T::Num:
            writeNum(out, o->n);
            break;
        case T::Str:
            writeHexStr(out, o->s);
            break;
        case T::Name:
            writeName(out, o->s);
            break;
        case T::Arr:
            out += '[';
            for (size_t i = 0; i < o->a.size(); i++)
            {
                if (i)
                    out += ' ';
                serialize(o->a[i], out, remap);
            }
            out += ']';
            break;
        case T::Dict:
            serializeDictEntries(o->d, out, remap);
            break;
        case T::Ref:
        {
            auto it = remap.find(o->rn);
            if (it != remap.end())
            {
                char b[32];
                snprintf(b, 32, "%d 0 R", it->second);
                out += b;
            }
            else
                out += "null";
            break;
        }
        case T::Stream:
            serializeDictEntries(o->d, out, remap);
            break;
        }
    }

    static void collectRefs(P o, std::set<int> &out)
    {
        if (!o)
            return;
        if (o->t == T::Ref)
        {
            out.insert(o->rn);
            return;
        }
        if (o->t == T::Arr)
        {
            for (auto &x : o->a)
                collectRefs(x, out);
            return;
        }
        if (o->t == T::Dict || o->t == T::Stream)
        {
            for (auto &kv : o->d)
                collectRefs(kv.second, out);
            return;
        }
    }

    // 将继承属性落到页面对象上
    static void materializeInherited(Doc &doc, int pageNum)
    {
        P page = doc.getObject(pageNum);
        if (!page || page->t != T::Dict)
            return;
        static const char *keys[] = {"MediaBox", "CropBox", "Resources", "Rotate"};
        std::vector<P> anc;
        int cur = pageNum;
        for (int d = 0; d < 100; d++)
        {
            P o = doc.getObject(cur);
            if (!o)
                break;
            P par = o->get("Parent");
            if (!par || par->t != T::Ref)
                break;
            P po = doc.getObject(par->rn);
            if (!po)
                break;
            anc.push_back(po);
            cur = par->rn;
        }
        for (const char *k : keys)
        {
            if (!page->has(k))
            {
                for (auto &a : anc)
                {
                    if (a->has(k))
                    {
                        page->set(k, a->get(k));
                        break;
                    }
                }
            }
        }
    }

    bool GetPageCountFromMemory(const unsigned char *data, size_t size, int &count, std::string &err)
    {
        Doc doc;
        std::string bytes((const char *)data, size);
        if (!doc.load(bytes))
        {
            err = doc.err;
            return false;
        }
        auto pages = doc.getPageList();
        count = (int)pages.size();
        if (count == 0)
        {
            err = "未能解析出任何页面";
            return false;
        }
        return true;
    }

    bool GetPageCount(const std::string &inPdf, int &count, std::string &err)
    {
        std::ifstream f(inPdf, std::ios::binary);
        if (!f)
        {
            err = "无法打开文件: " + inPdf;
            return false;
        }
        std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return GetPageCountFromMemory((const unsigned char *)bytes.data(), bytes.size(), count, err);
    }

    bool ExtractPages(const std::string &inPdf, const std::string &outPdf,
                      const std::vector<PageRange> &ranges, std::string &err)
    {
        std::ifstream f(inPdf, std::ios::binary);
        if (!f)
        {
            err = "无法打开文件: " + inPdf;
            return false;
        }
        std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

        Doc doc;
        if (!doc.load(bytes))
        {
            err = doc.err;
            return false;
        }
        auto pages = doc.getPageList();
        int total = (int)pages.size();
        if (total == 0)
        {
            err = "未能解析出任何页面";
            return false;
        }

        // 展开范围为页面索引列表（去重保序）
        std::vector<int> selected;
        std::set<int> seen;
        for (auto &rg : ranges)
        {
            if (rg.start < 1 || rg.start > total)
            {
                err = "起始页 " + std::to_string(rg.start) + " 超出范围（共 " + std::to_string(total) + " 页）";
                return false;
            }
            int e = rg.end < rg.start ? rg.start : rg.end;
            if (e > total)
            {
                err = "结束页 " + std::to_string(e) + " 超出范围（共 " + std::to_string(total) + " 页）";
                return false;
            }
            for (int p = rg.start; p <= e; p++)
            {
                if (!seen.count(p))
                {
                    seen.insert(p);
                    selected.push_back(p);
                }
            }
        }
        if (selected.empty())
        {
            err = "未选择任何页面";
            return false;
        }

        // 物化继承属性并移除 Parent
        for (int p : selected)
        {
            materializeInherited(doc, pages[p - 1]);
            P page = doc.getObject(pages[p - 1]);
            if (page)
                page->erase("Parent");
        }

        // 收集闭包
        std::set<int> closure;
        std::deque<int> queue;
        for (int p : selected)
        {
            int num = pages[p - 1];
            if (!closure.count(num))
            {
                closure.insert(num);
                queue.push_back(num);
            }
        }
        while (!queue.empty())
        {
            int num = queue.front();
            queue.pop_front();
            P o = doc.getObject(num);
            std::set<int> refs;
            collectRefs(o, refs);
            for (int r : refs)
            {
                if (!closure.count(r) && doc.xref.count(r))
                {
                    closure.insert(r);
                    queue.push_back(r);
                }
            }
        }

        // 分配新编号：1=Catalog 2=Pages
        std::map<int, int> remap; // 旧->新
        int next = 3;
        // 页面优先编号（便于阅读）
        std::vector<int> pageNewNums;
        for (int p : selected)
        {
            int num = pages[p - 1];
            if (!remap.count(num))
                remap[num] = next++;
            pageNewNums.push_back(remap[num]);
        }
        for (int num : closure)
        {
            if (!remap.count(num))
                remap[num] = next++;
        }
        int catalogNum = 1, pagesRootNum = 2;
        int maxObj = next - 1;

        // 序列化
        std::string out;
        out += "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
        std::vector<size_t> offsets(maxObj + 1, 0);

        auto beginObj = [&](int num)
        { offsets[num]=out.size(); char b[32]; snprintf(b,32,"%d 0 obj\n",num); out+=b; };

        // Catalog
        beginObj(catalogNum);
        {
            char b[64];
            snprintf(b, 64, "<< /Type /Catalog /Pages %d 0 R >>", pagesRootNum);
            out += b;
        }
        out += "\nendobj\n";
        // Pages root
        beginObj(pagesRootNum);
        out += "<< /Type /Pages /Kids [";
        for (size_t i = 0; i < pageNewNums.size(); i++)
        {
            if (i)
                out += ' ';
            char b[32];
            snprintf(b, 32, "%d 0 R", pageNewNums[i]);
            out += b;
        }
        {
            char b[64];
            snprintf(b, 64, "] /Count %d >>", (int)pageNewNums.size());
            out += b;
        }
        out += "\nendobj\n";

        // 其余对象
        for (int num : closure)
        {
            int nn = remap[num];
            P o = doc.getObject(num);
            if (!o)
                continue;
            beginObj(nn);
            if (o->t == T::Stream)
            {
                // 页面对象需补 Parent
                P copy = o; // 直接修改（仅序列化一次）
                // 写 dict：覆盖 Length，页面补 Parent
                std::string dictStr;
                std::vector<std::pair<std::string, P>> entries = o->d;
                // 移除 Length
                entries.erase(std::remove_if(entries.begin(), entries.end(),
                                             [](const std::pair<std::string, P> &kv)
                                             { return kv.first == "Length"; }),
                              entries.end());
                dictStr += "<<";
                for (auto &kv : entries)
                {
                    dictStr += ' ';
                    writeName(dictStr, kv.first);
                    dictStr += ' ';
                    serialize(kv.second, dictStr, remap);
                }
                char lb[48];
                snprintf(lb, 48, " /Length %d", (int)o->raw.size());
                dictStr += lb;
                dictStr += " >>";
                out += dictStr;
                out += "\nstream\n";
                out.append(o->raw);
                out += "\nendstream";
            }
            else if (o->t == T::Dict)
            {
                // 若是页面，补 Parent
                P ty = o->get("Type");
                bool isPage = (ty && ty->t == T::Name && ty->s == "Page");
                std::vector<std::pair<std::string, P>> entries = o->d;
                entries.erase(std::remove_if(entries.begin(), entries.end(),
                                             [](const std::pair<std::string, P> &kv)
                                             { return kv.first == "Parent"; }),
                              entries.end());
                std::string dictStr = "<<";
                for (auto &kv : entries)
                {
                    dictStr += ' ';
                    writeName(dictStr, kv.first);
                    dictStr += ' ';
                    serialize(kv.second, dictStr, remap);
                }
                if (isPage)
                {
                    char b[48];
                    snprintf(b, 48, " /Parent %d 0 R", pagesRootNum);
                    dictStr += b;
                }
                dictStr += " >>";
                out += dictStr;
            }
            else
            {
                serialize(o, out, remap);
            }
            out += "\nendobj\n";
        }

        // xref
        size_t xrefPos = out.size();
        out += "xref\n";
        {
            char b[48];
            snprintf(b, 48, "0 %d\n", maxObj + 1);
            out += b;
        }
        out += "0000000000 65535 f \n";
        for (int i = 1; i <= maxObj; i++)
        {
            char b[32];
            snprintf(b, 32, "%010d 00000 n \n", (int)offsets[i]);
            out += b;
        }
        out += "trailer\n";
        {
            char b[96];
            snprintf(b, 96, "<< /Size %d /Root %d 0 R >>", maxObj + 1, catalogNum);
            out += b;
        }
        out += "\nstartxref\n";
        {
            char b[32];
            snprintf(b, 32, "%d\n", (int)xrefPos);
            out += b;
        }
        out += "%%EOF\n";

        std::ofstream of(outPdf, std::ios::binary);
        if (!of)
        {
            err = "无法写入输出文件: " + outPdf;
            return false;
        }
        of.write(out.data(), (std::streamsize)out.size());
        if (!of)
        {
            err = "写入输出文件失败";
            return false;
        }
        return true;
    }

} // namespace pdfx
