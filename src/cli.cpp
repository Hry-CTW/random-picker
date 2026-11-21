// 命令行版：无 GUI 环境可用，也用于 CI/本机回归验证解析与随机逻辑
#include <iostream>
#include <string>

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
              << "  v2.2 人员组织管理 / 工作区：\n"
              << "  picker_cli workspace <路径> [--save|--load]   保存/加载 JSON 工作区\n"
              << "  picker_cli join <人序号> <组织> [--file 文件] 给人加组织(多对多)\n"
              << "  picker_cli leave <人序号> <组织> [--file 文件] 给人退出组织\n"
              << "  picker_cli orgs [--file 文件]                 列出人-组织关系\n"
              << "  v2.3 组织增删 / 拖动排序：\n"
              << "  picker_cli addorg <组织名> --file 文件          新建空组织\n"
              << "  picker_cli delorg <组织名> --file 文件          删除组织(成员同步退出)\n"
              << "  picker_cli movep <从> <到> [--file 文件]      移动人员顺序(1-based)\n"
              << "  picker_cli moveo <从> <到> [--file 文件]      移动组织顺序(1-based)\n"
              << "  默认文件：people.csv\n";
}

// 从 --file 取名单路径，缺省 people.csv
static std::string fileArg(int argc, char** argv) {
    for (int i = 2; i < argc - 1; i++)
        if (std::string(argv[i]) == "--file") return argv[i + 1];
    return "people.csv";
}

static bool isJsonPath(const std::string& p) {
    std::string l = p;
    for (char& c : l) c = (char)tolower((unsigned char)c);
    return l.size() >= 5 && l.substr(l.size() - 5) == ".json";
}

// 通用加载：显式 .json 路径按工作区读，其余按 CSV 读（不改变旧命令行为）
static bool load(Roster& r, int argc, char** argv, std::wstring& msg) {
    std::string src = fileArg(argc, argv);
    if (isJsonPath(src)) return r.loadWorkspace(src, msg);
    return r.importFile(src, false, msg);
}

// v2.2：人员管理命令的工作存储。优先读同名 .json 工作区（保留多对多），
// 没有则从 CSV 新建。这样 join/leave 多次调用之间不会丢组织关系。
static std::string workspaceFor(const std::string& csvPath) {
    size_t dot = csvPath.find_last_of('.');
    return (dot == std::string::npos ? csvPath : csvPath.substr(0, dot)) + ".json";
}
static bool loadStore(Roster& r, int argc, char** argv, std::wstring& msg) {
    std::string src = fileArg(argc, argv);
    if (isJsonPath(src)) return r.loadWorkspace(src, msg);
    std::string ws = workspaceFor(src);
    FILE* f = fopen(ws.c_str(), "rb");
    if (f) {
        fclose(f);
        return r.loadWorkspace(ws, msg);
    }
    return r.importFile(src, false, msg);
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
            std::string src = fileArg(argc, argv);
            std::string lower = src;
            for (char& c : lower) c = (char)tolower((unsigned char)c);
            std::string target = (lower.size() >= 5 && lower.substr(lower.size()-5) == ".json")
                                 ? src : workspaceFor(src);
            std::wstring wmsg;
            if (r.saveWorkspace(target, wmsg)) std::cout << "WS  " << wide_to_utf8(wmsg) << "\n";
        }
        return ok ? 0 : 1;
    }

    // v2.2：JSON 工作区保存/加载
    if (cmd == "workspace" && argc >= 3) {
        std::string mode = "save";
        for (int i = 3; i < argc; i++)
            if (std::string(argv[i]) == "--load") mode = "load";
        std::wstring msg;
        bool ok;
        if (mode == "load") {
            ok = r.loadWorkspace(argv[2], msg);
        } else {
            // --save：从 --file 读名单（若有）再写工作区
            std::string src;
            for (int i = 3; i < argc - 1; i++)
                if (std::string(argv[i]) == "--file") src = argv[i + 1];
            if (!src.empty()) {
                if (!r.loadWorkspace(src, msg)) {  // v2.2：源必须是工作区（多对多无损）
                    std::cout << "FAIL " << wide_to_utf8(msg) << "\n";
                    return 1;
                }
            }
            ok = r.saveWorkspace(argv[2], msg);
        }
        std::cout << (ok ? "OK  " : "FAIL ") << wide_to_utf8(msg) << "\n";
        if (ok && mode == "load") {
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
            if (r.saveWorkspace(workspaceFor(fileArg(argc, argv)), wmsg))
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
            if (r.saveWorkspace(workspaceFor(fileArg(argc, argv)), wmsg))
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
            if (r.saveWorkspace(workspaceFor(fileArg(argc, argv)), wmsg))
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
