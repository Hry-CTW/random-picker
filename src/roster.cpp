#include "roster.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <random>
#include <sstream>

#include "inflate.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <iconv.h>
#endif

// ---------- 编码 ----------

std::wstring utf8_to_wide(const std::string& s) {
    std::wstring out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            out.push_back((wchar_t)c);
            i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            out.push_back((wchar_t)(((c & 0x1F) << 6) | (s[i + 1] & 0x3F)));
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            out.push_back((wchar_t)(((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F)));
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
            uint32_t cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(s[i + 1] & 0x3F) << 12) |
                          ((uint32_t)(s[i + 2] & 0x3F) << 6) | (uint32_t)(s[i + 3] & 0x3F);
            cp -= 0x10000;
            out.push_back((wchar_t)(0xD800 + (cp >> 10)));
            out.push_back((wchar_t)(0xDC00 + (cp & 0x3FF)));
            i += 4;
        } else {
            out.push_back((wchar_t)0xFFFD);
            i += 1;
        }
    }
    return out;
}

std::string wide_to_utf8(const std::wstring& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        uint32_t cp = (uint32_t)s[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size()) {
            uint32_t lo = (uint32_t)s[i + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i++;
            }
        }
        if (cp < 0x80) {
            out.push_back((char)cp);
        } else if (cp < 0x800) {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

std::wstring trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n' || s[a] == 0x3000)) a++;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n' || s[b - 1] == 0x3000)) b--;
    return s.substr(a, b - a);
}

// ---------- 随机源 ----------

uint64_t csRand64() {
#ifdef _WIN32
    uint64_t v = 0;
    if (BCryptGenRandom(nullptr, (PUCHAR)&v, (ULONG)sizeof(v), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0)
        return v;
#endif
    static std::random_device rd;
    uint64_t a = rd();
    uint64_t b = rd();
    return (a << 32) ^ b;
}

int csRandBelow(int n) {  // 拒绝采样，消除取模偏差
    if (n <= 0) return -1;
    uint64_t limit = ~0ULL - (~0ULL % (uint64_t)n);
    uint64_t r;
    do {
        r = csRand64();
    } while (r >= limit);
    return (int)(r % (uint64_t)n);
}

std::wstring nowStamp() {
    time_t t = time(nullptr);
    tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d-%02d %02d:%02d:%02d", tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return utf8_to_wide(std::string(buf));
}

// ---------- 文件 IO ----------

// 用 C 文件 IO，不用 std::ifstream/ostringstream：
// ① 静态链接 libstdc++ 时 ifstream 在部分运行环境（含 wine）会崩在 locale 初始化
// ② Windows 下必须走 _wfopen，否则中文路径 / 中文文件名按 ANSI 解析会打不开
std::string readFileBytes(const std::string& path) {
#ifdef _WIN32
    FILE* f = _wfopen(utf8_to_wide(path).c_str(), L"rb");
    if (!f) f = fopen(path.c_str(), "rb");  // 兜底：按 ANSI 再试一次
#else
    FILE* f = fopen(path.c_str(), "rb");
#endif
    if (!f) return std::string();
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        fclose(f);
        return std::string();
    }
    std::string s((size_t)n, '\0');
    size_t got = fread(&s[0], 1, (size_t)n, f);
    fclose(f);
    s.resize(got);
    return s;
}

bool writeFileBytes(const std::string& path, const std::string& data) {
#ifdef _WIN32
    FILE* f = _wfopen(utf8_to_wide(path).c_str(), L"wb");
    if (!f) f = fopen(path.c_str(), "wb");
#else
    FILE* f = fopen(path.c_str(), "wb");
#endif
    if (!f) return false;
    size_t w = data.empty() ? 0 : fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return w == data.size();
}

// ---------- 文本解码（编码自动识别）----------

// 严格 UTF-8 校验：禁 overlong、禁代理区、禁超范围码点
static bool isStrictUtf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        int extra;
        uint32_t cp;
        if (c < 0x80) { i++; continue; }
        else if (c >= 0xC2 && c <= 0xDF) { extra = 1; cp = c & 0x1F; }
        else if (c >= 0xE0 && c <= 0xEF) { extra = 2; cp = c & 0x0F; }
        else if (c >= 0xF0 && c <= 0xF4) { extra = 3; cp = c & 0x07; }
        else return false;                       // 0x80–0xC1、0xF5–0xFF 都不是合法首字节
        if (i + (size_t)extra >= s.size()) return false;
        for (int k = 1; k <= extra; k++) {
            unsigned char cc = (unsigned char)s[i + k];
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (cp > 0x10FFFF) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;
        i += (size_t)extra + 1;
    }
    return true;
}

static std::string utf16ToUtf8(const std::string& raw, bool bigEndian) {
    std::wstring w;
    for (size_t i = 0; i + 1 < raw.size(); i += 2) {
        unsigned char a = (unsigned char)raw[i], b = (unsigned char)raw[i + 1];
        uint32_t u = bigEndian ? ((uint32_t)a << 8 | b) : ((uint32_t)b << 8 | a);
        if (u >= 0xD800 && u <= 0xDBFF && i + 3 < raw.size()) {  // 代理对
            unsigned char c = (unsigned char)raw[i + 2], d = (unsigned char)raw[i + 3];
            uint32_t lo = bigEndian ? ((uint32_t)c << 8 | d) : ((uint32_t)d << 8 | c);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
        }
        if (sizeof(wchar_t) == 2)
            w.push_back((wchar_t)u);
        else
            w.push_back((wchar_t)u);
    }
    return wide_to_utf8(w);
}

std::string decodeText(const std::string& raw, std::string& encName) {
    encName = "UTF-8";
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB &&
        (unsigned char)raw[2] == 0xBF) {
        encName = "UTF-8(BOM)";
        return raw.substr(3);
    }
    if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) {
        encName = "UTF-16LE";
        return utf16ToUtf8(raw.substr(2), false);
    }
    if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFE && (unsigned char)raw[1] == 0xFF) {
        encName = "UTF-16BE";
        return utf16ToUtf8(raw.substr(2), true);
    }
    if (isStrictUtf8(raw)) {
        encName = "UTF-8";
        return raw;
    }
    // 不是合法 UTF-8：按本地代码页（中文 Windows = GBK/936）解码
#ifdef _WIN32
    UINT cp = GetACP();
    int need = MultiByteToWideChar(cp, 0, raw.data(), (int)raw.size(), nullptr, 0);
    if (need > 0) {
        std::wstring w((size_t)need, L'\0');
        MultiByteToWideChar(cp, 0, raw.data(), (int)raw.size(), &w[0], need);
        encName = (cp == 936 || cp == 54936) ? "GBK" : ("ANSI-" + std::to_string(cp));
        return wide_to_utf8(w);
    }
#else
    iconv_t cd = iconv_open("UTF-8", "GB18030");
    if (cd != (iconv_t)-1) {
        size_t inb = raw.size();
        std::vector<char> in(raw.begin(), raw.end());
        char* inp = in.empty() ? nullptr : in.data();
        size_t outcap = raw.size() * 3 + 16;
        std::vector<char> out(outcap);
        char* outp = out.data();
        size_t outb = outcap;
        if (iconv(cd, &inp, &inb, &outp, &outb) != (size_t)-1) {
            iconv_close(cd);
            encName = "GBK";
            return std::string(out.data(), outcap - outb);
        }
        iconv_close(cd);
    }
#endif
    encName = "未知(按原样)";
    return raw;
}

std::wstring displayName(const Person& p) {
    if (p.en.empty()) return p.name;
    return p.name + L" (" + p.en + L")";
}

// ---------- 文本解析 ----------

static std::vector<std::string> splitLines(const std::string& data) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < data.size(); i++) {
        char c = data[i];
        if (c == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        if (cur.back() == '\r') cur.pop_back();
        out.push_back(cur);
    }
    return out;
}

static std::vector<std::wstring> splitFields(const std::string& line, char delim) {
    std::vector<std::wstring> out;
    std::string cur;
    bool inQuote = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (c == '"') {
            if (inQuote && i + 1 < line.size() && line[i + 1] == '"') {
                cur.push_back('"');
                i++;
            } else {
                inQuote = !inQuote;
            }
        } else if (c == delim && !inQuote) {
            out.push_back(trim(utf8_to_wide(cur)));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(trim(utf8_to_wide(cur)));
    return out;
}

static std::vector<std::vector<std::wstring>> parseDelimited(const std::string& data) {
    std::vector<std::vector<std::wstring>> rows;
    std::string body = data;
    if (body.size() >= 3 && (unsigned char)body[0] == 0xEF && (unsigned char)body[1] == 0xBB &&
        (unsigned char)body[2] == 0xBF)
        body.erase(0, 3);
    char delim = ',';
    for (char c : body) {
        if (c == '\n') break;
        if (c == '\t') {
            delim = '\t';
            break;
        }
        if (c == ';') {
            delim = ';';
            break;
        }
    }
    for (const std::string& line : splitLines(body)) {
        if (line.empty()) continue;
        std::vector<std::wstring> f = splitFields(line, delim);
        bool allEmpty = true;
        for (auto& x : f)
            if (!x.empty()) allEmpty = false;
        if (allEmpty) continue;
        if (!f.empty() && f[0].size() >= 2 && f[0][0] == L'#' && f[0][1] == L' ') continue;  // md 注释
        rows.push_back(f);
    }
    return rows;
}

static std::vector<std::vector<std::wstring>> parseMarkdown(const std::string& data) {
    std::vector<std::vector<std::wstring>> rows;
    std::string body = data;
    if (body.size() >= 3 && (unsigned char)body[0] == 0xEF && (unsigned char)body[1] == 0xBB &&
        (unsigned char)body[2] == 0xBF)
        body.erase(0, 3);
    for (const std::string& line : splitLines(body)) {
        if (line.find('|') == std::string::npos) continue;
        std::vector<std::wstring> cells = splitFields(line, '|');
        if (cells.size() >= 2) {
            if (cells.front().empty()) cells.erase(cells.begin());
            if (!cells.empty() && cells.back().empty()) cells.pop_back();
        }
        if (cells.empty()) continue;
        bool sep = true;
        for (auto& c : cells) {
            bool ok = true;
            for (wchar_t ch : c)
                if (ch != L'-' && ch != L':' && ch != L' ') ok = false;
            if (!ok) sep = false;
        }
        if (sep) continue;
        rows.push_back(cells);
    }
    return rows;
}

// ---------- xlsx（zip + xml，自带 inflate）----------

static std::string findBetween(const std::string& s, const std::string& open, const std::string& close, size_t from, size_t& end) {
    size_t a = s.find(open, from);
    if (a == std::string::npos) return std::string();
    a += open.size();
    size_t b = s.find(close, a);
    if (b == std::string::npos) return std::string();
    end = b + close.size();
    return s.substr(a, b - a);
}

static std::vector<std::wstring> parseSharedStrings(const std::string& xml) {
    std::vector<std::wstring> out;
    size_t pos = 0;
    for (;;) {
        size_t a = xml.find("<si>", pos);
        if (a == std::string::npos) break;
        size_t b = xml.find("</si>", a);
        if (b == std::string::npos) break;
        std::string item = xml.substr(a + 4, b - a - 4);
        std::wstring text;
        size_t p = 0;
        for (;;) {
            size_t t1 = item.find("<t", p);
            if (t1 == std::string::npos) break;
            size_t gt = item.find('>', t1);
            if (gt == std::string::npos) break;
            size_t t2 = item.find("</t>", gt);
            if (t2 == std::string::npos) break;
            text += utf8_to_wide(item.substr(gt + 1, t2 - gt - 1));
            p = t2 + 4;
        }
        out.push_back(text);
        pos = b + 5;
    }
    return out;
}

static int colIndex(const std::string& ref) {
    int v = 0;
    for (char c : ref) {
        if (c >= 'A' && c <= 'Z')
            v = v * 26 + (c - 'A' + 1);
        else
            break;
    }
    return v > 0 ? v - 1 : 0;
}

static std::vector<std::vector<std::wstring>> parseSheetXml(const std::string& xml, const std::vector<std::wstring>& shared) {
    std::vector<std::vector<std::wstring>> rows;
    size_t pos = 0;
    for (;;) {
        size_t r1 = xml.find("<row", pos);
        if (r1 == std::string::npos) break;
        size_t gt = xml.find('>', r1);
        if (gt == std::string::npos) break;
        size_t r2 = xml.find("</row>", gt);
        size_t stop = (r2 == std::string::npos) ? xml.size() : r2;
        std::string rowXml = xml.substr(gt + 1, stop - gt - 1);
        std::map<int, std::wstring> cells;
        size_t p = 0;
        for (;;) {
            size_t c1 = rowXml.find("<c ", p);
            size_t c1b = rowXml.find("<c>", p);
            size_t start;
            if (c1 == std::string::npos && c1b == std::string::npos) break;
            if (c1b != std::string::npos && (c1 == std::string::npos || c1b < c1))
                start = c1b;
            else
                start = c1;
            size_t cellEnd = rowXml.find("</c>", start);
            size_t tagEnd = rowXml.find('>', start);
            std::string tag = rowXml.substr(start, tagEnd - start);
            std::string cellXml =
                rowXml.substr(tagEnd + 1, (cellEnd == std::string::npos ? rowXml.size() : cellEnd) - tagEnd - 1);
            std::wstring val;
            size_t tmp;
            std::string ref = findBetween(tag, "r=\"", "\"", 0, tmp);
            std::string type = findBetween(tag, "t=\"", "\"", 0, tmp);
            if (type == "s" || type == "str") {
                std::string v = findBetween(cellXml, "<v>", "</v>", 0, tmp);
                if (!v.empty()) {
                    int idx = atoi(v.c_str());
                    if (type == "s" && idx >= 0 && idx < (int)shared.size())
                        val = shared[idx];
                    else
                        val = utf8_to_wide(v);
                }
            } else if (type == "inlineStr") {
                std::string v = findBetween(cellXml, "<t>", "</t>", 0, tmp);
                size_t t1 = cellXml.find("<t");
                if (t1 != std::string::npos) {
                    size_t g = cellXml.find('>', t1);
                    size_t t2 = cellXml.find("</t>", g);
                    if (g != std::string::npos && t2 != std::string::npos)
                        val = utf8_to_wide(cellXml.substr(g + 1, t2 - g - 1));
                }
            } else {
                std::string v = findBetween(cellXml, "<v>", "</v>", 0, tmp);
                if (!v.empty()) val = utf8_to_wide(v);
            }
            int ci = colIndex(ref);
            if (!val.empty()) cells[ci] = val;
            if (cellEnd == std::string::npos) break;
            p = cellEnd + 4;
        }
        if (!cells.empty()) {
            std::vector<std::wstring> row(cells.rbegin()->first + 1);
            for (auto& kv : cells) row[kv.first] = kv.second;
            rows.push_back(row);
        }
        if (r2 == std::string::npos) break;
        pos = r2 + 6;
    }
    return rows;
}

static bool zipExtract(const std::string& data, const std::string& want, std::string& out) {
    size_t eocd = std::string::npos;
    size_t floorPos = data.size() > 65557 ? data.size() - 65557 : 0;  // 防 size_t 下溢
    for (size_t i = data.size(); i >= 22 && i > floorPos; i--) {
        if (data[i - 4] == 'P' && data[i - 3] == 'K' && data[i - 2] == 5 && data[i - 1] == 6) {
            eocd = i - 4;
            break;
        }
    }
    if (eocd == std::string::npos) return false;
    auto u16 = [&](size_t o) { return (uint32_t)(unsigned char)data[o] | ((uint32_t)(unsigned char)data[o + 1] << 8); };
    auto u32 = [&](size_t o) {
        return (uint32_t)(unsigned char)data[o] | ((uint32_t)(unsigned char)data[o + 1] << 8) |
               ((uint32_t)(unsigned char)data[o + 2] << 16) | ((uint32_t)(unsigned char)data[o + 3] << 24);
    };
    uint32_t count = u16(eocd + 10);
    uint32_t cdOff = u32(eocd + 16);
    size_t p = cdOff;
    for (uint32_t i = 0; i < count; i++) {
        if (p + 46 > data.size()) return false;
        if (!(data[p] == 'P' && data[p + 1] == 'K' && data[p + 2] == 1 && data[p + 3] == 2)) return false;
        uint32_t method = u16(p + 10);
        uint32_t compSize = u32(p + 20);
        uint32_t uncompSize = u32(p + 24);
        uint32_t nameLen = u16(p + 28);
        uint32_t extraLen = u16(p + 30);
        uint32_t commentLen = u16(p + 32);
        uint32_t localOff = u32(p + 42);
        std::string name = data.substr(p + 46, nameLen);
        if (name == want) {
            uint32_t lNameLen = u16(localOff + 26);
            uint32_t lExtraLen = u16(localOff + 28);
            size_t dataOff = localOff + 30 + lNameLen + lExtraLen;
            if (dataOff + compSize > data.size()) return false;
            const uint8_t* src = (const uint8_t*)data.data() + dataOff;
            if (method == 0) {
                out.assign((const char*)src, compSize);
                return true;
            }
            std::vector<uint8_t> raw;
            if (!mini::inflate(src, compSize, raw)) return false;
            out.assign(raw.begin(), raw.end());
            (void)uncompSize;
            return true;
        }
        p += 46 + nameLen + extraLen + commentLen;
    }
    return false;
}

static std::vector<std::vector<std::wstring>> parseXlsx(const std::string& data, std::wstring& msg) {
    std::string sharedXml, sheetXml;
    std::vector<std::wstring> shared;
    zipExtract(data, "xl/sharedStrings.xml", sharedXml);
    if (!sharedXml.empty()) shared = parseSharedStrings(sharedXml);
    if (!zipExtract(data, "xl/worksheets/sheet1.xml", sheetXml)) {
        msg = L"xlsx 解析失败：找不到 sheet1.xml";
        return {};
    }
    return parseSheetXml(sheetXml, shared);
}

// ---------- Roster ----------

// 组序：先按固定组序 groups（允许空组，v2 一键 6 组后可见），
// 再补名单里出现、但不在固定组序里的组（例如导入得到的「三组」「未分组」）
std::vector<std::wstring> Roster::groupList() const {
    std::vector<std::wstring> gs = groups;
    for (const Person& p : people) {
        std::wstring g = p.group.empty() ? L"未分组" : p.group;
        if (std::find(gs.begin(), gs.end(), g) == gs.end()) gs.push_back(g);
    }
    return gs;
}

int Roster::countInGroup(const std::wstring& g) const {
    int n = 0;
    for (const Person& p : people) {
        std::wstring pg = p.group.empty() ? L"未分组" : p.group;
        if (pg == g) n++;
    }
    return n;
}

// 空组别统一按「未分组」参与比较，避免名单里留空的成员在分组页签里被漏掉
static std::wstring normGroup(const std::wstring& g) { return g.empty() ? L"未分组" : g; }

int Roster::countInScope(int scope) const {
    auto gs = groupList();
    int n = 0;
    for (const Person& p : people) {
        if (scope >= 0 && scope < (int)gs.size() && normGroup(p.group) != gs[scope]) continue;
        if (noRepeat && drawn.count(p.name)) continue;
        n++;
    }
    return n;
}

// 收集「当前范围 + 防重复」下的候选下标
static std::vector<int> collectCands(const std::vector<Person>& people, const std::vector<std::wstring>& gs,
                                     int scope, const std::set<std::wstring>& drawn, bool noRepeat) {
    std::vector<int> cand;
    for (int i = 0; i < (int)people.size(); i++) {
        if (scope >= 0 && scope < (int)gs.size() && normGroup(people[i].group) != gs[scope]) continue;
        if (noRepeat && drawn.count(people[i].name)) continue;
        cand.push_back(i);
    }
    return cand;
}

PickResult Roster::pick(int scope) {
    PickResult r;
    auto gs = groupList();
    std::vector<int> cand = collectCands(people, gs, scope, drawn, noRepeat);
    if (cand.empty() && noRepeat && !people.empty()) {
        drawn.clear();
        cand = collectCands(people, gs, scope, drawn, noRepeat);
    }
    if (cand.empty()) return r;
    uint64_t seed = csRand64();
    int k = csRandBelow((int)cand.size());
    int idx = cand[k];
    if (noRepeat) drawn.insert(people[idx].name);
    r.ok = true;
    r.person = people[idx];
    char buf[32];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)seed);
    r.seed = utf8_to_wide(std::string(buf));
    history.push_back(nowStamp() + L"  " + displayName(r.person) + L" [" + r.person.group + L"]  seed=" + r.seed);
    if (history.size() > 200) history.erase(history.begin());
    return r;
}

PickResult Roster::pickGroup() {
    PickResult r;
    auto gs = groupList();
    if (gs.empty()) return r;
    uint64_t seed = csRand64();
    int k = csRandBelow((int)gs.size());
    r.ok = true;
    r.isGroup = true;
    r.person.name = gs[k];
    r.person.group = gs[k];
    char buf[32];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)seed);
    r.seed = utf8_to_wide(std::string(buf));
    history.push_back(nowStamp() + L"  [组] " + gs[k] + L"  seed=" + r.seed);
    if (history.size() > 200) history.erase(history.begin());
    return r;
}

// 一次抽 N 人：
//  · 范围内不重复（不是抽 N 次，是一次抽出 N 个不同的人）
//  · N 大于可抽人数时静默截断是耍流氓，这里直接回人话提示，让使用者自己决定改小还是重置
//  · 洗牌用到的每个随机数原值都记进 seeds，事后可复现
MultiResult Roster::pickMulti(int scope, int n) {
    MultiResult r;
    auto gs = groupList();
    if (n <= 0) {
        r.msg = L"人数填错了，至少填 1";
        return r;
    }
    std::vector<int> cand = collectCands(people, gs, scope, drawn, noRepeat);
    if (cand.empty() && noRepeat && !people.empty()) {
        drawn.clear();
        cand = collectCands(people, gs, scope, drawn, noRepeat);
    }
    if (cand.empty()) {
        r.msg = L"当前范围内没有可抽的人，先导入名单";
        return r;
    }
    if (n > (int)cand.size()) {
        r.msg = L"这个范围现在只能抽 " + std::to_wstring(cand.size()) + L" 人，填的 " +
                std::to_wstring(n) + L" 太多了。把人数改小，或者按 R 重置抽取记录再来。";
        return r;
    }
    // 部分 Fisher–Yates：只洗前 n 个位置，O(n)，且保证等概率
    std::vector<uint64_t> rec;
    for (int i = 0; i < n; i++) {
        uint64_t limit = ~0ULL - (~0ULL % (uint64_t)(cand.size() - i));
        uint64_t v;
        do { v = csRand64(); } while (v >= limit);
        rec.push_back(v);
        int k = i + (int)(v % (uint64_t)(cand.size() - i));
        std::swap(cand[i], cand[k]);
    }
    std::wstring seeds, names;
    for (int i = 0; i < n; i++) {
        Person p = people[cand[i]];
        if (noRepeat) drawn.insert(p.name);
        r.people.push_back(p);
        char buf[32];
        snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)rec[i]);
        if (i) seeds += L" ";
        seeds += utf8_to_wide(std::string(buf));
        if (i) names += L"、";
        names += displayName(p);
    }
    r.seeds = seeds;
    r.ok = true;
    std::wstring where = (scope >= 0 && scope < (int)gs.size()) ? (L"[" + gs[scope] + L"] ") : L"";
    history.push_back(nowStamp() + L"  [抽" + std::to_wstring(n) + L"人] " + where + names +
                      L"  seed=" + seeds);
    if (history.size() > 200) history.erase(history.begin());
    return r;
}

// 补足到 n 个组：只加空组、只安置「还没分组」的人，不动已有分组（幂等，安全）
bool Roster::initGroups(int n, std::wstring& msg) {
    if (n <= 0) return false;
    auto gs = groupList();
    if ((int)gs.size() >= n) {
        msg = L"现在已经有 " + std::to_wstring(gs.size()) + L" 个组了（不少于 " + std::to_wstring(n) +
              L" 个），不用补。要改名双击组名，要重排就选「重排」。";
        return false;
    }
    for (int i = 1; (int)groups.size() < n; i++) {
        std::wstring name = L"第" + std::to_wstring(i) + L"组";
        if (std::find(gs.begin(), gs.end(), name) != gs.end()) continue;
        groups.push_back(name);
        gs.push_back(name);
    }
    // 还没分组的人轮流分进去（按名单顺序，不随机——分组不是抽取）
    int assigned = 0;
    for (Person& p : people) {
        if (!p.group.empty() && p.group != L"未分组") continue;
        p.group = groups[assigned % (int)groups.size()];
        assigned++;
    }
    msg = L"已补到 " + std::to_wstring(groups.size()) + L" 个组" +
          (assigned ? (L"，并把 " + std::to_wstring(assigned) + L" 位没分组的同学分了进去") : L"") +
          L"。空组可以先留着，之后选中人点「移组」往里加人。";
    return true;
}

// 强制重排成 n 组：全班按名单顺序轮流分，原有分组被覆盖（GUI 里会先问一次）
bool Roster::regroupAll(int n, std::wstring& msg) {
    if (n <= 0) return false;
    groups.clear();
    for (int i = 1; i <= n; i++) groups.push_back(L"第" + std::to_wstring(i) + L"组");
    if (people.empty()) {
        msg = L"已经搭好 " + std::to_wstring(n) + L" 个组的空架子，导入名单后再分人";
        return true;
    }
    for (size_t i = 0; i < people.size(); i++) people[i].group = groups[i % (size_t)n];
    int per = (int)people.size() / n;
    int rest = (int)people.size() % n;
    msg = L"已把全班 " + std::to_wstring(people.size()) + L" 人分成 " + std::to_wstring(n) + L" 组：每组 " +
          std::to_wstring(per) + L" 人" + (rest ? (L"，前 " + std::to_wstring(rest) + L" 组各多 1 人") : L"") +
          L"。双击组名可改名，选中人点「移组」可调。";
    return true;
}

bool Roster::renameGroup(const std::wstring& oldName, const std::wstring& nw, std::wstring& msg) {
    if (nw.empty()) {
        msg = L"组名不能为空";
        return false;
    }
    auto gs = groupList();
    if (std::find(gs.begin(), gs.end(), oldName) == gs.end()) {
        msg = L"没找到叫「" + oldName + L"」的组";
        return false;
    }
    if (nw != oldName && std::find(gs.begin(), gs.end(), nw) != gs.end()) {
        msg = L"已经有叫「" + nw + L"」的组了，换个名字";
        return false;
    }
    for (std::wstring& g : groups)
        if (g == oldName) g = nw;
    int moved = 0;
    for (Person& p : people)
        if (normGroup(p.group) == oldName) {
            p.group = nw;
            moved++;
        }
    msg = L"组名已改为「" + nw + L"」" + (moved ? (L"（" + std::to_wstring(moved) + L" 人跟着改）") : L"");
    return true;
}

void Roster::resetDrawn() { drawn.clear(); }

void Roster::add(const Person& p) { people.push_back(p); }

void Roster::removeAt(int i) {
    if (i >= 0 && i < (int)people.size()) people.erase(people.begin() + i);
}

void Roster::setGroup(int i, const std::wstring& g) {
    if (i >= 0 && i < (int)people.size()) people[i].group = g;
}

static bool hasAny(const std::wstring& s, const std::vector<std::wstring>& keys) {
    for (const std::wstring& k : keys)
        if (s.find(k) != std::wstring::npos) return true;
    return false;
}

bool Roster::importFile(const std::string& path, bool append, std::wstring& msg) {
    std::string data = readFileBytes(path);
    if (data.empty()) {
        msg = L"读取失败或文件为空";
        return false;
    }
    std::string lower = path;
    for (char& c : lower) c = (char)tolower((unsigned char)c);
    bool isXlsx = lower.size() > 5 && lower.substr(lower.size() - 5) == ".xlsx";
    bool isMd = lower.size() > 3 && lower.substr(lower.size() - 3) == ".md";

    // 编码：xlsx 内部就是 UTF-8 XML，不参与解码；txt/csv/md 走自动识别
    std::string encName = "UTF-8";
    std::string text = isXlsx ? data : decodeText(data, encName);

    std::vector<std::vector<std::wstring>> rows;
    if (isXlsx)
        rows = parseXlsx(text, msg);
    else if (isMd)
        rows = parseMarkdown(text);
    else
        rows = parseDelimited(text);

    if (rows.empty()) {
        msg = msg.empty() ? L"没有解析到任何数据行" : msg;
        return false;
    }

    // 列名候选（中英并存，顺序 = 优先级）
    static const std::vector<std::wstring> kNameKey = {L"中文名", L"姓名", L"名字", L"name", L"Name", L"NAME"};
    static const std::vector<std::wstring> kEnKey = {L"英文名", L"英文", L"拼音", L"english", L"English",
                                                     L"ENGLISH", L"name_en", L"EnglishName"};
    static const std::vector<std::wstring> kGroupKey = {L"组别", L"小组", L"组", L"group", L"Group",
                                                        L"GROUP", L"team", L"Team"};
    static const std::vector<std::wstring> kSidKey = {L"学号", L"编号", L"number", L"Number", L"no",
                                                      L"No", L"id", L"ID", L"Id"};

    int nameCol = 0, enCol = -1, groupCol = 1, sidCol = 2;
    size_t start = 0;
    std::wstring h0 = rows[0].size() > 0 ? rows[0][0] : L"";
    std::wstring h1 = rows[0].size() > 1 ? rows[0][1] : L"";
    std::wstring h2 = rows[0].size() > 2 ? rows[0][2] : L"";
    bool isHeader = hasAny(h0, kNameKey) || hasAny(h1, kGroupKey) || hasAny(h2, kSidKey) ||
                    hasAny(h1, kEnKey);
    if (isHeader) {
        start = 1;
        nameCol = -1;
        groupCol = -1;
        sidCol = -1;
        for (size_t c = 0; c < rows[0].size(); c++) {
            std::wstring h = rows[0][c];
            // 英文名先判，避免被中文名吃掉
            if (enCol < 0 && hasAny(h, kEnKey)) { enCol = (int)c; continue; }
            if (nameCol < 0 && hasAny(h, kNameKey)) { nameCol = (int)c; continue; }
            if (groupCol < 0 && hasAny(h, kGroupKey)) { groupCol = (int)c; continue; }
            if (sidCol < 0 && hasAny(h, kSidKey)) { sidCol = (int)c; continue; }
        }
        if (nameCol < 0) nameCol = 0;
    }

    std::vector<Person> loaded;
    for (size_t i = start; i < rows.size(); i++) {
        const std::vector<std::wstring>& r = rows[i];
        std::wstring nm = nameCol >= 0 && nameCol < (int)r.size() ? trim(r[nameCol]) : L"";
        if (nm.empty()) continue;
        Person p;
        p.name = nm;
        p.en = enCol >= 0 && enCol < (int)r.size() ? trim(r[enCol]) : L"";
        p.group = groupCol >= 0 && groupCol < (int)r.size() ? trim(r[groupCol]) : L"";
        p.sid = sidCol >= 0 && sidCol < (int)r.size() ? trim(r[sidCol]) : L"";
        loaded.push_back(p);
    }
    if (loaded.empty()) {
        msg = L"没有解析到有效姓名";
        return false;
    }
    if (!append) {
        people.clear();
        drawn.clear();
        groups.clear();  // 覆盖导入：组结构跟着新名单走，旧的一键 6 组不留残影
    }
    people.insert(people.end(), loaded.begin(), loaded.end());
    std::wstring src = isXlsx ? L"xlsx" : (isMd ? L"markdown" : L"csv/txt");
    msg = L"导入 " + std::to_wstring(loaded.size()) + L" 人，共 " + std::to_wstring(groupList().size()) +
          L" 组（来源：" + src + (isXlsx ? L"" : L" · " + utf8_to_wide(encName)) + L"）";
    return true;
}

bool Roster::exportCsv(const std::string& path, std::wstring& msg) {
    std::string out = "\xEF\xBB\xBF";  // BOM，Excel 直接双击不乱码
    out += "姓名,英文名,组别,学号\n";
    for (const Person& p : people) {
        out += wide_to_utf8(p.name) + "," + wide_to_utf8(p.en) + "," + wide_to_utf8(p.group) + "," +
               wide_to_utf8(p.sid) + "\n";
    }
    if (!writeFileBytes(path, out)) {
        msg = L"保存失败";
        return false;
    }
    msg = L"已保存 " + std::to_wstring(people.size()) + L" 人到名单";
    return true;
}

bool Roster::exportHistory(const std::string& path, std::wstring& msg) {
    std::string out = "\xEF\xBB\xBF";
    out += "时间,结果,seed\n";
    for (const std::wstring& h : history) out += wide_to_utf8(h) + "\n";
    if (!writeFileBytes(path, out)) {
        msg = L"导出失败";
        return false;
    }
    msg = L"已导出 " + std::to_wstring(history.size()) + L" 条抽取记录";
    return true;
}
