// 命令行版：无 GUI 环境可用，也用于 CI/本机回归验证解析与随机逻辑
#include <iostream>
#include <string>
#include <cstring>

#include "roster.h"

static void help() {
    std::cout << "用法:\n"
              << "  picker_cli import <文件> [--append]        导入名单(csv/txt/md/xlsx)\n"
              << "  picker_cli list [--file 文件]              列出名单与分组\n"
              << "  picker_cli pick [次数] [--file 文件] [--group 组名] [--no-repeat]\n"
              << "  picker_cli pickn <人数> [--file 文件] [--group 组名] [--no-repeat]\n"
              << "  picker_cli pickgroup [--file 文件]          随机抽一个组\n"
              << "  picker_cli initgroups <组数> [--file 文件]  一键初始化 n 个组\n"
              << "  picker_cli rename <旧组名> <新组名> [--file 文件]\n"
              << "  v2.4 单一工作区（CSV/JSON 已合并，默认存 testenv/workspace.json）：\n"
              << "  picker_cli workspace --load                 读取默认工作区\n"
              << "  picker_cli workspace --save                 写回默认工作区\n"
              << "  picker_cli workspace <路径>.json --save|--load   读写指定工作区\n"
              << "  picker_cli import <名单.csv/xlsx>            导入名单（自动写入默认工作区）\n"
              << "  picker_cli list / pick / pickn / pickgroup  读默认工作区\n"
              << "  picker_cli join <人序号> <组织>              加组织（多对多）\n"
              << "  picker_cli leave <人序号> <组织>             退出组织\n"
              << "  picker_cli orgs                             列出人-组织关系\n"
              << "  picker_cli addorg <组织名> / delorg <组织名> 新建/删除组织\n"
              << "  picker_cli movep <从> <到> / moveo <从> <到> 拖动排序(1-based)\n"
              << "  默认存储：testenv/workspace.json（含真实名单，已 gitignore）；\n"
              << "  CSV/xlsx 仅作导入来源与导出目标，不再双文件存储。\n";
}

// 唯一默认存储：testenv/workspace.json（含真实名单，已 gitignore）
// CSV 只是可选的「导入/导出」交换格式，不再承担持久化职责。
static const char* kDefaultStore = "testenv/workspace.json";
static const wchar_t* kDefaultStoreW = L"testenv/workspace.json";

// 从 --file 取名单/工作区路径，缺省 testenv/workspace.json
static std::string fileArg(int argc, char** argv) {
    for (int i = 2; i < argc - 1; i++)
        if (std::string(argv[i]) == "--file") return argv[i + 1];
    return kDefaultStore;
}

// 工作区路径：--file 若指向 .json 则原样；否则落到 testenv/workspace.json。
// 这样「--file 名单.csv」表示从 CSV 导入进同一个工作区，不再生成同名 .json。
static bool isJsonPath(const std::string& p);
static std::string storePath(const std::string& arg) {
    if (isJsonPath(arg)) return arg;
    return kDefaultStore;
}

static bool isJsonPath(const std::string& p) {
    std::string l = p;
    for (char& c : l) c = (char)tolower((unsigned char)c);
    return l.size() >= 5 && l.substr(l.size() - 5) == ".json";
}

// 通用加载：.json 按工作区读；其余（CSV/xlsx）导入后写回默认工作区，
// 保证后续所有操作都在同一份数据上。
static bool load(Roster& r, int argc, char** argv, std::wstring& msg) {
    std::string src = fileArg(argc, argv);
    if (isJsonPath(src)) return r.loadWorkspace(src, msg);
    if (!r.importFile(src, false, msg)) return false;
    std::wstring wmsg;
    bool ok = r.saveWorkspace(kDefaultStore, wmsg);
    msg += ok ? (L" · 已写入工作区 " + std::wstring(kDefaultStoreW))
              : (L" · 警告：" + wmsg);
    return true;
}

// 人员管理命令统一读写默认工作区（testenv/workspace.json）。
static std::string storeFor(int argc, char** argv) {
    return storePath(fileArg(argc, argv));
}
static bool loadStore(Roster& r, int argc, char** argv, std::wstring& msg) {
    std::string src = fileArg(argc, argv);
    if (isJsonPath(src)) return r.loadWorkspace(src, msg);
    // 工作区已存在 → 直接读工作区（多对多无损）
    FILE* f = fopen(kDefaultStore, "rb");
    if (f) {
        fclose(f);
        return r.loadWorkspace(kDefaultStore, msg);
    }
    // 否则从 CSV 导入并落盘，之后所有命令共享这一份
    if (!r.importFile(src, false, msg)) return false;
    std::wstring wmsg;
    bool ok = r.saveWorkspace(kDefaultStore, wmsg);
    msg += ok ? (L" · 已写入工作区 " + std::wstring(kDefaultStoreW))
              : (L" · 警告：" + wmsg);
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        help();
        return 0;
    }
    std::string cmd = argv[1];
    Roster r;

    if (cmd == "import" && argc >= 3) {
        bool append = false;
        for (int i = 3; i < argc; i++)
            if (std::string(argv[i]) == "--append") append = true;
        std::wstring msg;
        bool ok = r.importFile(argv[2], append, msg);
        std::cout << (ok ? "OK  " : "FAIL ") << wide_to_utf8(msg) << "\n";
        if (!ok) return 1;
        // v2.4：导入即落盘到工作区。
        //  --file 指向 .json → 写到该工作区；否则写默认 testenv/workspace.json。
        std::string target = kDefaultStore;
        for (int i = 3; i < argc - 1; i++)
            if (std::string(argv[i]) == "--file") {
                std::string t = argv[i + 1];
                if (isJsonPath(t)) target = t;
            }
        std::wstring wmsg;
        if (r.saveWorkspace(target, wmsg))
            std::cout << "WS  已写入工作区 " << target << "\n";
        std::cout << "--- 名单 ---\n";
        for (size_t i = 0; i < r.people.size(); i++)
            std::cout << i + 1 << ". " << wide_to_utf8(displayName(r.people[i])) << " ["
                      << wide_to_utf8(r.people[i].group) << "] " << wide_to_utf8(r.people[i].sid) << "\n";
        std::cout << "--- 分组 ---\n";
        for (auto& g : r.groupList()) std::cout << "  " << wide_to_utf8(g) << "\n";
        return 0;
    }

    if (cmd == "list") {
        std::wstring msg;
        if (!load(r, argc, argv, msg)) {
            std::cout << "FAIL " << wide_to_utf8(msg) << "\n";
            return 1;
        }
        std::cout << "OK  " << wide_to_utf8(msg) << "\n";
        for (size_t i = 0; i < r.people.size(); i++)
            std::cout << i + 1 << ". " << wide_to_utf8(displayName(r.people[i]))
                      << " [" << wide_to_utf8(groupsText(r.people[i])) << "]\n";
        auto gs = r.groupList();
        for (size_t i = 0; i < gs.size(); i++)
            std::cout << "组 " << i << ": " << wide_to_utf8(gs[i]) << " (" << r.countInGroup(gs[i]) << " 人)\n";
        return 0;
    }

    if (cmd == "pick") {
        int n = (argc >= 3 && argv[2][0] >= '0' && argv[2][0] <= '9') ? atoi(argv[2]) : 1;
        std::string wantGroup;
        for (int i = 2; i < argc; i++) {
            if (std::string(argv[i]) == "--group" && i + 1 < argc) wantGroup = argv[i + 1];
            if (std::string(argv[i]) == "--no-repeat") r.noRepeat = true;
        }
        std::wstring msg;
        if (!load(r, argc, argv, msg)) {
            std::cout << "FAIL " << wide_to_utf8(msg) << "\n";
            return 1;
        }
        int scope = -1;
        if (!wantGroup.empty()) {
            auto gs = r.groupList();
            auto w = utf8_to_wide(wantGroup);
            for (size_t i = 0; i < gs.size(); i++)
                if (gs[i] == w) scope = (int)i;
            if (scope < 0) {  // 组名没匹配上就直说，别偷偷按全班抽
                std::cout << "FAIL 名单里没有叫「" << wantGroup << "」的组。现有分组：";
                for (auto& g : gs) std::cout << wide_to_utf8(g) << " ";
                std::cout << "\n";
                return 1;
            }
        }
        for (int i = 0; i < n; i++) {
            PickResult p = r.pick(scope);
            if (!p.ok) {
                std::cout << "无可抽取对象\n";
                break;
            }
            std::cout << wide_to_utf8(displayName(p.person)) << " [" << wide_to_utf8(p.person.group) << "] seed="
                      << wide_to_utf8(p.seed) << "\n";
        }
        return 0;
    }

    // v2：一次抽 N 人
    if (cmd == "pickn" && argc >= 3) {
        int n = atoi(argv[2]);
        std::string wantGroup;
        for (int i = 3; i < argc; i++) {
            if (std::string(argv[i]) == "--group" && i + 1 < argc) wantGroup = argv[i + 1];
            if (std::string(argv[i]) == "--no-repeat") r.noRepeat = true;
        }
        std::wstring msg;
        if (!load(r, argc, argv, msg)) {
            std::cout << "FAIL " << wide_to_utf8(msg) << "\n";
            return 1;
        }
        int scope = -1;
        if (!wantGroup.empty()) {
            auto gs = r.groupList();
            auto w = utf8_to_wide(wantGroup);
            for (size_t i = 0; i < gs.size(); i++)
                if (gs[i] == w) scope = (int)i;
            if (scope < 0) {  // 组名没匹配上就直说，别偷偷按全班抽
                std::cout << "FAIL 名单里没有叫「" << wantGroup << "」的组。现有分组：";
                for (auto& g : gs) std::cout << wide_to_utf8(g) << " ";
                std::cout << "\n";
                return 1;
            }
        }
        MultiResult mr = r.pickMulti(scope, n);
        if (!mr.ok) {
            std::cout << "FAIL " << wide_to_utf8(mr.msg) << "\n";
            return 1;
        }
        std::cout << "OK  抽出 " << mr.people.size() << " 人\n";
        for (auto& p : mr.people)
            std::cout << "  " << wide_to_utf8(displayName(p)) << " [" << wide_to_utf8(p.group) << "]\n";
        std::cout << "seed=" << wide_to_utf8(mr.seeds) << "\n";
        return 0;
    }

    if (cmd == "pickgroup") {
        std::wstring msg;
        if (!load(r, argc, argv, msg)) {
            std::cout << "FAIL " << wide_to_utf8(msg) << "\n";
            return 1;
        }
        PickResult p = r.pickGroup();
        std::cout << (p.ok ? wide_to_utf8(displayName(p.person)) : std::string("无分组")) << "\n";
        return 0;
    }

    // v2：一键 n 组（--force = 强制重排）
    if (cmd == "initgroups" && argc >= 3) {
        int n = atoi(argv[2]);
        bool force = false;
        for (int i = 3; i < argc; i++)
            if (std::string(argv[i]) == "--force") force = true;
        std::wstring msg;
        if (!load(r, argc, argv, msg)) {
            std::cout << "FAIL " << wide_to_utf8(msg) << "\n";
            return 1;
        }
        bool ok = force ? r.regroupAll(n, msg) : r.initGroups(n, msg);
        std::cout << (ok ? "OK  " : "SKIP ") << wide_to_utf8(msg) << "\n";
        auto gs = r.groupList();
        std::cout << "组数=" << gs.size() << "\n";
        for (auto& g : gs) std::cout << "  " << wide_to_utf8(g) << " (" << r.countInGroup(g) << " 人)\n";
        return ok ? 0 : 1;  // 没做变更（跳过）也用非零退出码，便于脚本断言
    }

    // v2：改组名（v2.2：支持工作区，多组织内旧名同步）
    if (cmd == "rename" && argc >= 4) {
        std::wstring msg;
        if (!loadStore(r, argc, argv, msg)) {
            std::cout << "FAIL " << wide_to_utf8(msg) << "\n";
            return 1;
        }
        bool ok = r.renameGroup(utf8_to_wide(argv[2]), utf8_to_wide(argv[3]), msg);
        std::cout << (ok ? "OK  " : "FAIL ") << wide_to_utf8(msg) << "\n";
        for (auto& g : r.groupList()) std::cout << "  " << wide_to_utf8(g) << "\n";
        if (ok) {
            std::wstring wmsg;
            if (r.saveWorkspace(storeFor(argc, argv), wmsg)) std::cout << "WS  " << wide_to_utf8(wmsg) << "\n";
        }
        return ok ? 0 : 1;
    }

    // v2.4：工作区统一在 testenv/workspace.json。
    //   workspace --load                读默认工作区
    //   workspace --save               从当前内存状态写入默认工作区
    //   workspace <路径>.json          读写指定工作区（备份/多套名单用）
    if (cmd == "workspace") {
        std::string mode = "save";
        for (int i = 2; i < argc; i++)
            if (std::string(argv[i]) == "--load") mode = "load";
        std::wstring msg;
        bool ok;
        if (argc >= 3 && isJsonPath(argv[2])) {
            if (mode == "load") {
                ok = r.loadWorkspace(argv[2], msg);
            } else {
                // 另存为：先读源工作区（--file 指向 .json 则用它，否则默认工作区）
                // 进内存，再写到目标路径。这样 fresh 进程里也能拿到完整数据。
                std::string srcArg = fileArg(argc, argv);
                std::wstring lmsg;
                if (isJsonPath(srcArg)) (void)r.loadWorkspace(srcArg, lmsg);
                else (void)r.loadWorkspace(kDefaultStore, lmsg);
                ok = r.saveWorkspace(argv[2], msg);
            }
        } else {
            ok = (mode == "load") ? r.loadWorkspace(kDefaultStore, msg)
                                  : r.saveWorkspace(kDefaultStore, msg);
        }
        std::cout << (ok ? "OK  " : "FAIL ") << wide_to_utf8(msg) << "\n";
        if (ok) {
            for (size_t i = 0; i < r.people.size(); i++)
                std::cout << i + 1 << ". " << wide_to_utf8(displayName(r.people[i]))
                          << " [" << wide_to_utf8(groupsText(r.people[i])) << "]\n";
            std::cout << "组织数=" << r.groupList().size() << "\n";
        }
        return ok ? 0 : 1;
    }

    // v2.2：加入/退出组织
    if ((cmd == "join" || cmd == "leave") && argc >= 4) {
        std::wstring msg;
        if (!loadStore(r, argc, argv, msg)) {
            std::cout << "FAIL " << wide_to_utf8(msg) << "\n";
            return 1;
        }
        int idx = atoi(argv[2]) - 1;  // 1-based 给人看
        bool ok = r.toggleMembership(idx, utf8_to_wide(argv[3]), msg);
        std::cout << (ok ? "OK  " : "FAIL ") << wide_to_utf8(msg) << "\n";
        if (ok) {
            std::cout << wide_to_utf8(displayName(r.people[idx])) << " 现属 ["
                      << wide_to_utf8(groupsText(r.people[idx])) << "]\n";
            std::wstring wmsg;
            if (r.saveWorkspace(storeFor(argc, argv), wmsg))
                std::cout << "WS  " << wide_to_utf8(wmsg) << "\n";
        }
        return ok ? 0 : 1;
    }

    // v2.3：组织增删（与 GUI 加组织/删组织共用逻辑）
    if ((cmd == "addorg" || cmd == "delorg") && argc >= 4) {
        std::wstring msg;
        if (!loadStore(r, argc, argv, msg)) {
            std::cout << "FAIL " << wide_to_utf8(msg) << "\n";
            return 1;
        }
        // argv[1]=命令 argv[2]=组织名
        std::wstring gname = utf8_to_wide(argv[2]);
        bool ok = (cmd == "addorg") ? r.addOrganization(gname, msg)
                                    : r.removeOrganization(gname, msg);
        std::cout << (ok ? "OK  " : "FAIL ") << wide_to_utf8(msg) << "\n";
        if (ok) {
            std::wstring wmsg;
            if (r.saveWorkspace(storeFor(argc, argv), wmsg))
                std::cout << "WS  " << wide_to_utf8(wmsg) << "\n";
        }
        return ok ? 0 : 1;
    }

    // v2.3：拖动排序（人 / 组织），供 GUI 共用逻辑的验收
    if ((cmd == "movep" || cmd == "moveo") && argc >= 4) {
        std::wstring msg;
        if (!loadStore(r, argc, argv, msg)) {
            std::cout << "FAIL " << wide_to_utf8(msg) << "\n";
            return 1;
        }
        int from = atoi(argv[2]) - 1, to = atoi(argv[3]) - 1;  // 1-based
        bool ok = (cmd == "movep") ? r.movePerson(from, to, msg)
                                   : r.moveOrganization(from, to, msg);
        std::cout << (ok ? "OK  " : "FAIL ") << wide_to_utf8(msg) << "\n";
        if (ok) {
            std::wstring wmsg;
            if (r.saveWorkspace(storeFor(argc, argv), wmsg))
                std::cout << "WS  " << wide_to_utf8(wmsg) << "\n";
        }
        return ok ? 0 : 1;
    }

    // v2.2：列出人-组织关系（多对多）
    if (cmd == "orgs") {
        std::wstring msg;
        if (!loadStore(r, argc, argv, msg)) {
            std::cout << "FAIL " << wide_to_utf8(msg) << "\n";
            return 1;
        }
        for (size_t i = 0; i < r.people.size(); i++)
            std::cout << i + 1 << ". " << wide_to_utf8(displayName(r.people[i]))
                      << " [" << wide_to_utf8(groupsText(r.people[i])) << "]\n";
        std::cout << "--- 组织 ↔ 人数 ---\n";
        for (const std::wstring& g : r.groupList())
            std::cout << wide_to_utf8(g) << ": " << r.countInGroup(g) << "\n";
        return 0;
    }

    help();
    return 0;
}
