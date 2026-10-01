#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

struct Person {
    std::wstring name;
    std::wstring en;     // 英文名（可选，英语课点名用）
    std::wstring group;
    std::wstring sid;
};

// 显示名：有英文名时拼成 "张三 (Zhang San)"
std::wstring displayName(const Person& p);

struct PickResult {
    bool ok = false;
    bool isGroup = false;
    Person person;
    std::wstring seed;
};

// 一次抽 N 人的结果（v2）
struct MultiResult {
    bool ok = false;
    std::vector<Person> people;
    std::wstring seeds;   // 每一步用到的随机数原值（十六进制，空格分隔）——可复现、可留证
    std::wstring msg;     // 失败时的人话提示
};

class Roster {
public:
    std::vector<Person> people;
    std::vector<std::wstring> groups;  // 固定组序：允许存在空组，便于「一键 6 组」后逐个加人
    std::vector<std::wstring> history;
    std::set<std::wstring> drawn;
    bool noRepeat = false;

    bool importFile(const std::string& path, bool append, std::wstring& msg);
    bool exportCsv(const std::string& path, std::wstring& msg);
    bool exportHistory(const std::string& path, std::wstring& msg);

    std::vector<std::wstring> groupList() const;
    int countInScope(int scope) const;
    int countInGroup(const std::wstring& g) const;
    PickResult pick(int scope);        // scope: -1 全班, >=0 指定组下标
    PickResult pickGroup();            // 随机抽一个组
    MultiResult pickMulti(int scope, int n);  // v2：一次抽 n 人（范围内不重复）
    void resetDrawn();

    void add(const Person& p);
    void removeAt(int i);
    void setGroup(int i, const std::wstring& g);

    // v2 组结构维护
    bool initGroups(int n, std::wstring& msg);   // 补足到 n 个组（只加空组、只安置无组的人）
    bool regroupAll(int n, std::wstring& msg);   // 强制重排成 n 组（全班轮流，覆盖原分组）
    bool renameGroup(const std::wstring& oldName, const std::wstring& nw, std::wstring& msg);
};

std::wstring utf8_to_wide(const std::string& s);
std::string wide_to_utf8(const std::wstring& s);
// 文本解码：自动识别 UTF-8(BOM) / UTF-16(BOM) / UTF-8 / 本地 ANSI(中文 Windows=GBK)
// 返回 UTF-8 字节串，encName 输出识别到的编码名（用于给使用者提示）
std::string decodeText(const std::string& raw, std::string& encName);
std::string readFileBytes(const std::string& path);
bool writeFileBytes(const std::string& path, const std::string& data);
uint64_t csRand64();
int csRandBelow(int n);
std::wstring nowStamp();
std::wstring trim(const std::wstring& s);
