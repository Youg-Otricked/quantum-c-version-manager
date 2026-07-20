#include <iostream>
#include <string>
#include <functional>
#include <vector>
#include <fstream> 
#include <cstdlib>
#include <filesystem>
#include <cstdio>
#include <algorithm>
#include <regex>
#include "yaml.hpp"
#include "libcurl.hpp"
#include "json.hpp"
#include <sys/stat.h>
#include <ranges>
const std::string QCVM_VERSION = "2.1.1";
#include <unordered_set>
std::unordered_set<std::string> load_tagged_versions() {
    std::unordered_set<std::string> versions;
    httplib::Client client("https://raw.githubusercontent.com");
    auto res = client.Get(
        "/Youg-Otricked/quantum-c-version-manager/main/tagged_versions.txt"
    );
    if (!res || res->status != 200) {
        return versions;
    }
    std::stringstream stream(res->body);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty()) {
            versions.insert(line);
        }
    }
    return versions;
}
std::string getExecutablePath() {
#ifdef __linux__
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);

    if (count == -1) {
        throw "Failed to get executable path\n";
    }

    return std::string(result, count);

#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);

    std::vector<char> buffer(size);

    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        throw "Failed to get executable path\n";
    }

    return std::string(buffer.data());

#else
    throw "Unsupported platform\n";
#endif
}
auto TAGGED_VERSIONS = load_tagged_versions();
struct RegistryEntry {
    std::string version;
    std::string repo;
    std::string description;
};

std::unordered_map<std::string, RegistryEntry> parse_registry(const std::string& text) {
    std::unordered_map<std::string, RegistryEntry> registry;
    std::stringstream file(text);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line.starts_with("#")) {
            continue;
        }
        std::stringstream ss(line);
        std::string name;
        std::string version;
        std::string repo;
        std::string description;
        std::getline(ss, name, '|');
        std::getline(ss, version, '|');
        std::getline(ss, repo, '|');
        std::getline(ss, description);
        registry[name] = {
            version,
            repo,
            description
        };
    }
    return registry;
}
std::unordered_map<std::string, RegistryEntry> load_registry() {
    httplib::Client client("https://raw.githubusercontent.com");
    client.set_default_headers({
        {"User-Agent", "qcm/1.0"}
    });
    auto res = client.Get(
        "/Youg-Otricked/quantum-c-version-manager/main/registry.txt"
    );
    if (!res || res->status != 200) {
        throw "Failed to download package registry\n";
    }
    return parse_registry(res->body);
}
std::unordered_map<std::string, RegistryEntry> REGISTRY = load_registry();
/*
root structure = 
**~/.qcm/qcm.yaml**
installed:
    - V1.....
    .........
current: CuurVer...*/
struct Command {
    std::string name;
    std::function<void(char** args, int argc)> callback;
};
void notAdded(char** args, int argc) {
    std::cout << "Command not added yet" << '\n';
}
auto getConfigFileNode() {
    const char* home_raw = getenv("HOME");
    if (!home_raw) {
        throw "HOME not set\n";
    }
    std::string home(home_raw);
    if (!std::filesystem::exists(home + "/.qcm/config.yaml")) {
        throw "Config file doesn't exist. Please run `qcm setup`\n";
    }
    FILE* config = std::fopen((home + "/.qcm/config.yaml").c_str(), "r");
    if (config == nullptr) {
        throw "Config file doesn't exist. Please run `qcm setup`\n";
    } 
    auto node = fkyaml::node::deserialize(config);
    std::fclose(config);
    return node;
}
void saveConfigFileNode(auto node) {
    const char* home_raw = getenv("HOME");
    if (!home_raw) {
        throw "HOME not set\n";
    }
    std::string home(home_raw);
    std::ofstream file(home + "/.qcm/config.yaml");
    file << node;
    file.close();
}
std::string getCurrentVersion() {
    auto node = getConfigFileNode();
    if (node["current"].is_null()) {
        return "none";
    }
    return node["current"].get_value<std::string>();
}
std::string getOS() {
#ifdef __APPLE__
    return "macos";
#elif __linux__
    return "linux";
#else
    return "unknown";
#endif
}
std::string parseVersion(const std::string& output) {
    std::regex pattern(R"([xv]\d+\.\d+\.\d+)");
    std::smatch match;
    if (std::regex_search(output, match, pattern)) {
        return match[0];
    }
    return "";
}
std::string getCurrentQCVersion() {
    FILE* pipe = popen("qc -sv 2>&1", "r");
    char buf[256];
    std::string output;
    while (fgets(buf, sizeof(buf), pipe)) {
        output += buf;
    }
    pclose(pipe);
    output.erase(output.find_last_not_of(" \n\r\t") + 1);
    if (!output.empty() && (output[0] == 'x' || output[0] == 'v')) {
        return output;
    }
    pipe = popen("qc -v 2>&1", "r");
    output = "";
    while (fgets(buf, sizeof(buf), pipe)) {
        output += buf;
    }
    pclose(pipe);
    return parseVersion(output);
}
void list(char** args, int argc) {
    auto node = getConfigFileNode();
    for (auto& ver_node : node["installed"]) {
        std::string ver = ver_node.get_value<std::string>();
        std::cout << ver << (TAGGED_VERSIONS.contains(ver) ? " [QCVM tagged]" : "")
          << (node["current"].get_value<std::string>() == ver ? " *" : "") << '\n';
    }
}
void setup(char** args, int argc) {
    bool exportPath = true;
    for (int i = 2; i < argc; i++) {
        if (std::string(args[i]) == "--no-export") {
            exportPath = false;
        }
    }
    const char* home_raw = getenv("HOME");
    if (!home_raw) {
        throw "HOME not set\n";
    }
    std::string home(home_raw);
    std::filesystem::create_directories(home + "/.qcm/packages");
    std::filesystem::create_directories(home + "/.qcm/versions");
    std::filesystem::create_directories(home + "/.qcm/bin");
    std::filesystem::create_directories(home + "/.qc/bin");
    std::filesystem::create_directories(home + "/.qc/lib");
    if (!std::filesystem::exists(home + "/.qcm/config.yaml")) {
        std::ofstream file(home + "/.qcm/config.yaml");
        auto node = fkyaml::node::deserialize(R"(
installed: []
current: null
)");
        file << node;
    }
    if (!exportPath) {
        std::cout << "Skipping PATH export\n";
        return;
    }
    std::string shell = getenv("SHELL") ? getenv("SHELL") : "";
    std::string rcFile;
    if (shell.ends_with("zsh")) {
        rcFile = home + "/.zshrc";
    } else if (shell.ends_with("fish")) {
        rcFile = home + "/.config/fish/config.fish";
    } else {
        rcFile = home + "/.bashrc";
    }
    std::string qcPath;
    std::string qcmPath;
    if (shell.ends_with("fish")) {
        qcPath = "fish_add_path $HOME/.qc/bin";
        qcmPath = "fish_add_path $HOME/.qcm/bin";
    } else {
        qcPath = "export PATH=\"$HOME/.qc/bin:$PATH\"";
        qcmPath = "export PATH=\"$HOME/.qcm/bin:$PATH\"";
    }
    std::ifstream in(rcFile);
    std::string content(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>()
    );
    std::ofstream out(rcFile, std::ios::app);
    if (content.find(".qc/bin") == std::string::npos) {
        out << "\n# Added by qcm\n";
        out << qcPath << "\n";
        std::cout << "Added ~/.qc/bin to PATH\n";
    }
    if (content.find(".qcm/bin") == std::string::npos) {
        out << "\n# Added by qcm\n";
        out << qcmPath << "\n";
        std::cout << "Added ~/.qcm/bin to PATH\n";
    }
    std::cout << "Run: source " << rcFile << "\n";
}
void help(char** args, int argc) {
    std::cout << R"(QCVM )" << QCVM_VERSION << R"(
Commands:
use: qcm tooling use <version> - changes currently used version to other currently installed version.
install: qcm tooling install <version> - installs version <version> and changed currently used version to it.
uninstall: qcm tooling uninstall <version> - uninstalls <version>.
list: qcm tooling list - lists installed versions of qc, wih a `*` next to the current version.
list-remote: qcm tooling list-remote - lists all remote versions of qc.
)";
}
void init(char** args, int argc) {
    std::cout << "\033[?1049h\033[2J\033[H";
    std::cout << "\nThis command will create your basic qc setup, along with the projects scope.yaml";
    std::cout << "\n\n";
    std::cout << "scope name: (" << std::filesystem::current_path().filename() << ") ";
    std::string scopeName = "";
    std::getline(std::cin, scopeName);
    if (scopeName.empty() || std::all_of(scopeName.begin(), scopeName.end(), isspace)) {
        scopeName = std::filesystem::current_path().filename();
    }
    std::cout << "version: (1.0.0) ";
    std::string version = "";
    std::getline(std::cin, version);
    if (version.empty() || std::all_of(version.begin(), version.end(), isspace)) {
        version = "1.0.0";
    }
    std::cout << "description: ";
    std::string desc = "";
    std::getline(std::cin, desc);
    std::cout << "entrypoint function: (main) ";
    std::string entry = "";
    std::getline(std::cin, entry);
    static const std::regex pattern(R"(^[a-zA-Z_][a-zA-Z0-9_]*$)");
    if (entry.empty() || !std::regex_match(entry, pattern)) {
        entry = "main";
    }
    std::cout << "test command: ";
    std::string testCmd = "";
    std::getline(std::cin, testCmd);
    std::cout << "git repo: ";
    std::string gitRepo = "";
    std::getline(std::cin, gitRepo);
    const char* username = std::getenv("USER");
    if (!username) username = "";
    std::cout << "author: " << (username[0] ? "(" + std::string(username) + ") " : "");
    std::string author = "";
    std::getline(std::cin, author);
    if (author.empty() || std::all_of(author.begin(), author.end(), isspace)) {
        author = username;
    }
    std::ofstream scope("scope.yaml");
    scope << "project:\n"
        << "  name: " << scopeName << "\n"
        << "  version: " << version << "\n"
        << "  description: " << desc << "\n"
        << "  author: " << author << "\n"
        << "  repo: " << gitRepo << "\n"
        << "  qc_version: " << getCurrentVersion() << "\n"
        << "\ndependencies: {}\n"
        << "\ncommands:\n"
        << "  test: " << testCmd << "\n"
        << "  run: qc -c main.qc -o " << scopeName << " && ./" << scopeName << "\n"
        << "  build: qc -c main.qc -o " << scopeName << "\n";
    scope.close();
    std::cout << "code options:\n\n";
    std::cout << "would you like the compiler to automatically print interpret/compile time? (Y/n) ";
    std::string choice = "";
    std::string inlineDirs = "";
    std::getline(std::cin, choice);
    if (choice == "Y" || choice == "y") {
        inlineDirs = "// @show-time\n";
    }
    std::string entrypointLine = "";
    if (entry != "main") {
        entrypointLine = "#entrypoint=" + entry + "\n";
    }
    std::cout << "will this project be a one file or multi file project? (Y/n) ";
    std::getline(std::cin, choice);
    bool multiFile = false;
    if (choice == "Y" || choice == "y") {
        multiFile = true;
    }
    std::cout << "will this project have a api/lib for others to use? (Y/n) ";
    std::getline(std::cin, choice);
    bool api = false;
    if (choice == "Y" || choice == "y") {
        api = true;
    }
    if (api) {
        std::filesystem::create_directory("lib");
        std::ofstream("lib/lib.qc", std::ios::app).close();
    }
    for (const auto& dir : {"dependencies", "tests", "docs"}) {
        std::filesystem::create_directory(dir);
    }
    std::ofstream("docs/index.md", std::ios::app).close();
    std::ofstream mainFile("main.qc");
    mainFile << inlineDirs << entrypointLine <<
    (multiFile ? "namespace Exported {\n    \n}\n" : "")
    << "int " << entry << "() {\n" << "    return 0;" 
    << '\n' << '}' << '\n';
    mainFile.close();
    std::cout.flush();
    std::cout << "\033[?1049l";
    std::cout << "project setup complete!" << '\n';
}
std::string getVersions() {
    httplib::Client client("https://api.github.com");
    client.set_default_headers({{"User-Agent", "qcm/1.0"}});
    
    auto res = client.Get("/repos/Youg-Otricked/QuantumC/releases");
    if (!res || res->status != 200) {
        throw "Failed to fetch releases. Are you connected to Wi-Fi?\n";
    }
    return res->body;
}
void listRemote(char** args, int argc) {
    auto json = nlohmann::json::parse(getVersions());
    for (auto& release : json) {
        std::string tag = release["tag_name"];
        bool deprecated = tag[0] == 'v';
        if (tag[0] != 'x' && tag[0] != 'v') continue;
        std::cout << tag  << (TAGGED_VERSIONS.contains(tag) ? " [QCVM tagged]" : "") << (deprecated ? " [deprecated]" : "") << '\n';
    }
}
bool isValidVersion(char* version) {
    auto json = nlohmann::json::parse(getVersions());
    std::string out = "";
    for (auto& release : json) {
        std::string tag = release["tag_name"];
        bool deprecated = tag[0] == 'v';
        if (tag[0] != 'x' && tag[0] != 'v') continue;
        if (version == tag) return true;
    }
    return false;
}
std::string getLatestQCTag() {
    httplib::Client client("https://api.github.com");
    client.set_default_headers({{"User-Agent", "qcm/1.0"}});
    client.set_follow_location(true);
    auto res = client.Get("/repos/Youg-Otricked/QuantumC/releases/latest");
    if (!res || res->status != 200) {
        throw "Failed to check for latest QuantumC version. Are you connected to Wi-Fi?\n";
    }
    auto json = nlohmann::json::parse(res->body);
    return json["tag_name"].get<std::string>();
}
std::string resolveVersion(std::string version) {
    if (version == "latest") {
        return getLatestQCTag();
    }
    return version;
}
// https://github.com/Youg-Otricked/QuantumC/releases/download/<tag>/<filename>
void install(char** args, int argc) {
    if (argc < 4) {
        throw "Usage: `qcm tooling install <version>`";
    }
    std::string resolvedVersion = resolveVersion(args[2]);
    if (!isValidVersion(args[2])) {
        throw "Please pass a valid version.";
    }
    std::string currentQCVersion = getCurrentQCVersion();
    if (getConfigFileNode()["installed"].contains(resolvedVersion)) {
        throw "Version " + resolvedVersion + "already installed. Did you mean to run `qcm use " + args[2] + "`?";
    }
    auto node = getConfigFileNode();
    auto& installed = node["installed"];
    bool currentTracked = false;
    for (auto& ver : installed) {
        if (ver.get_value<std::string>() == currentQCVersion) {
            currentTracked = true;
            break;
        }
    }
    if (!currentTracked && !currentQCVersion.empty()) {
        installed.as_seq().emplace_back(currentQCVersion);
        node["current"] = currentQCVersion;
        saveConfigFileNode(node);
    }
    if (currentQCVersion == resolvedVersion) {
        throw "Version " + resolvedVersion + " is already installed. Did you mean to run `qcm use " + args[2] + "`? (JK it wasnt stored becuase you installed it but not via QCVM)";
    }
    httplib::Client client("https://github.com");
    client.set_default_headers({{"User-Agent", "qcm/1.0"}});
    client.set_follow_location(true);
    const char* home_raw = getenv("HOME");
    if (!home_raw) {
        throw "HOME not set\n";
    }
    std::string home(home_raw);
    std::string versionDir = home + "/.qcm/versions/";
    std::string binPath = versionDir + "qc-" + args[2];
    std::ofstream binFile(binPath, std::ios::binary);
    size_t total = 0;
    size_t downloaded = 0;

    auto res = client.Get(
        "/Youg-Otricked/QuantumC/releases/download/" + std::string(args[2]) + "/" + (getOS() == "linux" ? "qc-linux-amd64" : "qc-macos-arm64"),
        [&](const httplib::Response& response) {
            total = std::stoull(response.get_header_value("Content-Length", "0"));
            return true;
        },
        [&](const char* data, size_t len) {
            binFile.write(data, len);
            downloaded += len;
            int pct = total ? (downloaded * 100 / total) : 0;
            int bars = pct / 5;
            std::cout << "\rInstalling QC - [" << std::string(bars, '=') << std::string(20 - bars, ' ') << "] " << pct << "%" << std::flush;
            return true;
        }
    );
    std::cout << '\n';
    binFile.close();
    if (!res || res->status != 200) {
        std::filesystem::remove(binPath);
        throw "Failed to fetch qc. Are you connected to Wi-Fi?\n";
    }
    std::string stdlibPath = versionDir + "stdlib-" + args[2] + ".qc";
    std::ofstream stdlibFile(stdlibPath, std::ios::binary);
    total = 0;
    downloaded = 0;
    auto stdres = client.Get(
        "/Youg-Otricked/QuantumC/releases/download/" + std::string(args[2]) + "/stdlib.qc",
        [&](const httplib::Response& response) {
            total = std::stoull(response.get_header_value("Content-Length", "0"));
            return true;
        },
        [&](const char* data, size_t len) {
            stdlibFile.write(data, len);
            downloaded += len;
            int pct = total ? (downloaded * 100 / total) : 0;
            int bars = pct / 5;
            std::cout << "\rInstalling QC Stdlib - [" << std::string(bars, '=') << std::string(20 - bars, ' ') << "] " << pct << "%" << std::flush;
            return true;
        }
    );
    std::cout << '\n';
    stdlibFile.close();
    if (!stdres || stdres->status != 200) {
        std::filesystem::remove(stdlibPath);
        throw "Failed to fetch qc stdlib. Are you connected to Wi-Fi?\n";
    }
    std::string qcBin = home + "/.qc" + "/bin";
    std::string qcStdlib = home + "/.qc" + "/lib";
    if (std::filesystem::exists(qcBin + "/qc")) {
        std::filesystem::rename(qcBin + "/qc", versionDir + "qc-" + currentQCVersion);
    }
    if (std::filesystem::exists(qcStdlib + "/stdlib.qc")) {
        std::filesystem::rename(qcStdlib + "/stdlib.qc", versionDir + "stdlib-" + currentQCVersion + ".qc");
    }
    std::filesystem::rename(binPath, qcBin + "/qc");
    std::filesystem::rename(stdlibPath, qcStdlib + "/stdlib.qc");
    auto snode = getConfigFileNode();
    snode["installed"].as_seq().emplace_back(args[2]);
    snode["current"] = args[2];
    chmod((qcBin + "/qc").c_str(), 0755);
    saveConfigFileNode(snode);
    if (std::filesystem::exists("scope.yaml")) {
        std::ifstream in("scope.yaml");
        auto scnode = fkyaml::node::deserialize(in);
        scnode["project"]["qc_version"] = std::string(args[2]);
        std::ofstream out("scope.yaml");
        out << fkyaml::node::serialize(scnode);
    }
    std::cout << "QuantumC version " << args[2] << " successfuly installed\n";
}
// https://github.com/Youg-Otricked/quantum-c-version-manager/releases/download/<tag>/<filename>
std::string getLatestQCVMTag() {
    httplib::Client client("https://api.github.com");
    client.set_default_headers({{"User-Agent", "qcm/1.0"}});
    client.set_follow_location(true);
    auto res = client.Get("/repos/Youg-Otricked/quantum-c-version-manager/releases/latest");
    if (!res || res->status != 200) {
        throw "Failed to check for updates. Are you connected to Wi-Fi?\n";
    }
    auto json = nlohmann::json::parse(res->body);
    return json["tag_name"].get<std::string>();
}
bool installSelfFallback(const std::string& tempPath) {
    const char* home_raw = getenv("HOME");
    if (!home_raw) {
        return false;
    }
    std::string home(home_raw);
    std::string qcmDir = home + "/.qcm/bin";
    std::string fallback = qcmDir + "/qcm";
    std::filesystem::create_directories(qcmDir);
    try {
        std::filesystem::rename(tempPath, fallback);
        std::filesystem::permissions(
            fallback,
            std::filesystem::perms::owner_all |
            std::filesystem::perms::group_exec |
            std::filesystem::perms::others_exec
        );
        return true;
    } catch (...) {
        return false;
    }
}
void ensureQcmPath() {
    const char* home_raw = getenv("HOME");
    if (!home_raw) return;
    std::string home(home_raw);
    std::string path = getenv("PATH") ? getenv("PATH") : "";
    if (path.find(home + "/.qcm/bin") != std::string::npos) {
        return;
    }
    std::string shell = getenv("SHELL") ? getenv("SHELL") : "";
    std::string rcFile;
    if (shell.ends_with("zsh"))
        rcFile = home + "/.zshrc";
    else if (shell.ends_with("fish"))
        rcFile = home + "/.config/fish/config.fish";
    else
        rcFile = home + "/.bashrc";
    std::ofstream out(rcFile, std::ios::app);
    if (shell.ends_with("fish")) {
        out << "\n# Added by qcm\n";
        out << "fish_add_path $HOME/.qcm/bin\n";
    } else {
        out << "\n# Added by qcm\n";
        out << "export PATH=\"$HOME/.qcm/bin:$PATH\"\n";
    }
    std::cout << "Added ~/.qcm/bin to PATH in " << rcFile << "\n";
}
void upgrade(char** args, int argc) {
    std::string latest_tag = getLatestQCVMTag();
    if (latest_tag == QCVM_VERSION) {
        throw "QCM is already up to date (" + QCVM_VERSION + ")";
    }
    std::string binPath = getExecutablePath();
    std::string tempPath = binPath + ".new";
    httplib::Client client("https://github.com");
    client.set_default_headers({
        {"User-Agent", "qcm/" + std::string(QCVM_VERSION)}
    });
    client.set_follow_location(true);
    std::ofstream binFile(tempPath, std::ios::binary);
    size_t total = 0;
    size_t downloaded = 0;
    auto res = client.Get(
        "/Youg-Otricked/quantum-c-version-manager/releases/download/" 
        + latest_tag + "/" 
        + (getOS() == "linux" ? "qcm-linux-amd64" : "qcm-macos-arm64"),
        [&](const httplib::Response& response) {
            total = std::stoull(
                response.get_header_value("Content-Length", "0")
            );
            return true;
        },
        [&](const char* data, size_t len) {
            binFile.write(data, len);
            downloaded += len;
            int pct = total ? downloaded * 100 / total : 0;
            std::cout << "\rUpdating QCM ["
                << std::string(pct / 5, '=')
                << std::string(20 - pct / 5, ' ')
                << "] "
                << pct << "%"
                << std::flush;
            return true;
        }
    );
    binFile.close();
    std::cout << "\n";
    if (!res || res->status != 200) {
        std::filesystem::remove(tempPath);
        throw "Failed to download QCM\n";
    }
    try {
        std::filesystem::rename(tempPath, binPath);
    }
    catch (...) {
        std::cout << "Cannot replace current executable.\n";
        std::cout << "Installing to ~/.qcm/bin/qcm instead...\n";

        if (!installSelfFallback(tempPath)) {
            throw "Failed to install updated QCM\n";
        }

        ensureQcmPath();
    }
}
void use(char** args, int argc) {
    if (argc < 3) {
        throw "Usage: `qcm tooling use <version>`";
    }
    std::string version = resolveVersion(args[2]);
    const char* home_raw = getenv("HOME");
    if (!home_raw) throw "HOME not set\n";
    std::string home(home_raw);
    std::string versionDir = home + "/.qcm/versions/";
    std::string binPath = versionDir + "qc-" + version;
    std::string stdlibPath = versionDir + "stdlib-" + version + ".qc";
    if (!std::filesystem::exists(binPath) || !std::filesystem::exists(stdlibPath)) {
        throw "Version " + version + " not installed. Run `qcm install " + version + "`";
    }
    std::string qcBin = home + "/.qc/bin";
    std::string qcLib = home + "/.qc/lib";
    std::string currentVersion = getCurrentQCVersion();
    if (std::filesystem::exists(qcBin + "/qc")) {
        std::filesystem::rename(qcBin + "/qc", versionDir + "qc-" + currentVersion);
    }
    if (std::filesystem::exists(qcLib + "/stdlib.qc")) {
        std::filesystem::rename(qcLib + "/stdlib.qc", versionDir + "stdlib-" + currentVersion + ".qc");
    }
    std::filesystem::rename(binPath, qcBin + "/qc");
    std::filesystem::rename(stdlibPath, qcLib + "/stdlib.qc");
    chmod((qcBin + "/qc").c_str(), 0755);
    auto node = getConfigFileNode();
    node["current"] = version;
    saveConfigFileNode(node);
    if (std::filesystem::exists("scope.yaml")) {
        std::ifstream in("scope.yaml");
        auto scnode = fkyaml::node::deserialize(in);
        scnode["project"]["qc_version"] = version;

        std::ofstream out("scope.yaml");
        out << fkyaml::node::serialize(scnode);
    }
    std::cout << "Now using QC " << version << "\n";
}
void uninstall(char** args, int argc) {
    if (argc < 3) {
        throw "Usage: `qcm tooling uninstall <version>`";
    }
    std::string ver = resolveVersion(args[2]);
    args[2] = ver.data();
    if (!isValidVersion(resolveVersion(args[2]).data())) {
        throw "Version " + resolveVersion(args[2]) + " isn't installed.";
    }
    const char* home_raw = getenv("HOME");
    if (!home_raw) throw "HOME not set\n";
    std::string home(home_raw);
    if (std::filesystem::exists(home + "/.qcm/versions/qc-" + args[2])) {
        std::filesystem::remove(home + "/.qcm/versions/qc-" + args[2]);
    }
    if (std::filesystem::exists(home + "/.qcm/versions/stdlib-" + args[2] + ".qc")) {
        std::filesystem::remove(home + "/.qcm/versions/stdlib-" + args[2] + ".qc");
    }
    if (getCurrentQCVersion() == args[2]) {
        if (std::filesystem::exists(home + "/.qc/bin/qc")) {
            std::filesystem::remove(home + "/.qc/bin/qc");
        }
        if (std::filesystem::exists(home + "/.qc/lib/stdlib.qc")) {
            std::filesystem::remove(home + "/.qc/lib/stdlib.qc");
        }
    }
    auto node = getConfigFileNode();  
    auto& seq = node["installed"].as_seq();
    auto it = std::find(seq.begin(), seq.end(), args[2]);
    if (it != seq.end()) {
        seq.erase(it);
    }
    node["current"] = (node["installed"].empty() ? (getCurrentQCVersion().empty() ? "" : getCurrentQCVersion()) : node["installed"][0]);
    saveConfigFileNode(node);
    if (std::filesystem::exists("scope.yaml")) {
        std::ifstream in("scope.yaml");
        auto scnode = fkyaml::node::deserialize(in);
        scnode["project"]["qc_version"] = (node["current"] != nullptr ? node["current"].get_value<std::string>() : "");
        std::ofstream out("scope.yaml");
        out << fkyaml::node::serialize(scnode);
    }
    std::cout << "Successfully uninstalled QuantumC version " << args[2] << '\n';
}
void run(char** args, int argc) {
    if (argc < 3) {
        throw "usage: `qcm run <command>`";
    }
    if (!std::filesystem::exists("scope.yaml")) {
        throw "no scope.yaml found in current directory\n";
    }
    std::ifstream file("scope.yaml");
    auto root = fkyaml::node::deserialize(file);
    auto& commands = root["commands"];
    if (!commands.contains(args[2])) {
        throw "command '" + std::string(args[2]) + "' not found in scope.yaml\n";
    }
    auto cmd = commands[args[2]].get_value<std::string>();
    if (cmd.empty()) {
        throw "command '" + std::string(args[2]) + "' is empty\n";
    }
    std::system(cmd.c_str());
}
void add(char** args, int argc);
void syncScope(char** args, int argc) {
    if (!std::filesystem::exists("scope.yaml")) {
        throw std::string("No scope.yaml found in current directory.");
    }
    std::ifstream in("scope.yaml");
    auto scnode = fkyaml::node::deserialize(in);
    in.close();
    if (!scnode.contains("project") || !scnode["project"].contains("qc_version")) {
        throw std::string("No qc_version found in scope.yaml");
    }
    std::string version = scnode["project"]["qc_version"].get_value<std::string>();
    if (version.empty()) {
        throw std::string("qc_version is empty in scope.yaml");
    }
    auto node = getConfigFileNode();
    if (node["installed"].contains(version)) {
        char* use_args[] = {args[0], (char*)"use", version.data()};
        use(use_args, 3);
        return;
    } else {
        char* install_args[] = {args[0], (char*)"install", version.data()};
        install(install_args, 3);
    }
    for (auto [dependancy_name, dependancy_url] : scnode["dependencies"].as_map()) {
        std::string name = dependancy_name.get_value<std::string>();
        std::string gz = dependancy_url.get_value<std::string>(); 
        char* add_args[] = {args[0], (char*)"add", name.data(), (char*)"git", gz.data()};
        add(add_args, 5);
    }
}
void add(char** args, int argc) {
    if (argc < 3) {
        throw "Usage: qcm add <package-name> [git <rawusercontent url to a project that has a scope.yaml> <git .tar.gz tarball url>]";
    }
    std::string name = args[2];
    std::string source;
    std::string type;
    std::string git_repo;
    if (argc >= 6) {
        type = args[3];
        source = args[4];
        git_repo = args[5];
    } else {
        type = "registry";        
        
        if (!REGISTRY.contains(name)) {
            throw "The registry does not contain package " + name + std::string("\n");
        }
        source = REGISTRY[name].repo;
    }

    if (!std::filesystem::exists("scope.yaml")) {
        throw "No scope.yaml found in current directory";
    }
    std::ifstream in("scope.yaml");
    auto root = fkyaml::node::deserialize(in);
    in.close();
    if (!root.contains("dependencies")) {
        root["dependencies"] = fkyaml::node::deserialize("{}");
    }

    auto& deps = root["dependencies"];
    deps[name] = source;
    std::string url = source;
    size_t proto = url.find("://");
    size_t host_start = (proto == std::string::npos) ? 0 : proto + 3;
    size_t path_start = url.find('/', host_start);
    if (path_start == std::string::npos) {
        throw "Invalid URL";
    }
    std::string host = url.substr(0, path_start);
    std::string path = url.substr(path_start);
    httplib::Client client(host);
    auto res = client.Get((path + "/scope.yaml").c_str());
    if (!res) {
        throw "Failed to get scope.yaml. Are you connected to Wi-Fi?";
    }
    if (res->status != 200) {
        throw "HTTP error: " + std::to_string(res->status);
    }
    url = git_repo;
    std::string scopeText = res->body;
    auto node = fkyaml::node::deserialize(scopeText);
    for (auto [dependancy_name, dependancy_url] : node["dependencies"].as_map()) {
        std::string name = dependancy_name.get_value<std::string>();
        std::string gz = dependancy_url.get_value<std::string>(); 
        char* add_args[] = {args[0], (char*)"add", name.data(), (char*)"git", gz.data()};
        add(add_args, 5);
    }
    const char* home_raw = getenv("HOME");
    if (!home_raw) throw "HOME not set\n";
    std::string home(home_raw);
    if (!std::filesystem::exists(home + "/.qcm/packages/" + name)) {
        std::filesystem::create_directories(home + "/.qcm/packages/" + name);
        std::string cmd = "curl -sL " + url + " | tar -tzf -";
        FILE* pipe = popen(cmd.c_str(), "r");
        std::vector<std::string> files;
        char buffer[512];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            std::string line(buffer);
            line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());
            size_t first_slash = line.find('/');
            if (first_slash != std::string::npos) {
                std::string clean_name = line.substr(first_slash + 1);
                if (!clean_name.empty()) {
                    files.push_back(clean_name);
                }
            }
        }
        pclose(pipe);
        for (const std::string& file_name : files) {
            if (!file_name.ends_with(".qc") && !file_name.ends_with(".md")) continue;
            std::filesystem::path full_dest_path = home + "/.qcm/packages/" + name + "/" + file_name;
            if (full_dest_path.has_parent_path()) {
                std::filesystem::create_directories(full_dest_path.parent_path());
            }
            size_t total = 0;
            size_t downloaded = 0;
            std::ofstream qcfile(full_dest_path, std::ios::binary);
            if (!qcfile.is_open()) continue;
            auto res = client.Get(
                path + std::string("/") + file_name,
                [&](const httplib::Response& response) {
                    total = std::stoull(response.get_header_value("Content-Length", "0"));
                    return true;
                },
                [&](const char* data, size_t len) {
                    qcfile.write(data, len);
                    downloaded += len;
                    int pct = total ? (downloaded * 100 / total) : 0;
                    int bars = pct / 5;
                    std::cout << "\rInstalling file " << file_name << " - [" << std::string(bars, '=') << std::string(20 - bars, ' ') << "] " << pct << "%" << std::flush;
                    return true;
                }
            ); 
        }
        std::cout << "\nPackage installation complete!\n";
    } else {
        std::cout << "Package already cached!" << '\n';
    }
    std::string src = home + "/.qcm/packages/" + name;
    std::string dst = "dependencies/" + name;
    if (std::filesystem::exists(dst) || std::filesystem::is_symlink(dst)) {
        std::cout << "Dependency of same name already exists. Deleting." << '\n';
        std::filesystem::remove_all(dst);
    }
    std::filesystem::create_directory_symlink(src, dst);
    std::ofstream out("scope.yaml");
    out << fkyaml::node::serialize(root);
    std::cout << "Added dependency '" << name << "' -> " << source << '\n';
}
void removePackage(char** args, int argc) {
    if (argc < 3) {
        throw "Usage: qcm remove <package-name>";
    }

    std::string name = args[2];

    if (!std::filesystem::exists("scope.yaml")) {
        throw "No scope.yaml found in current directory";
    }

    std::ifstream in("scope.yaml");
    auto root = fkyaml::node::deserialize(in);
    in.close();

    if (!root.contains("dependencies") || !root["dependencies"].contains(name)) {
        throw "Dependency not found: " + name;
    }
    std::string link = "dependencies/" + name;
    if (std::filesystem::exists(link) || std::filesystem::is_symlink(link)) {
        std::filesystem::remove(link);
    }
    root["dependencies"].as_map().erase(name);
    std::ofstream out("scope.yaml");
    out << fkyaml::node::serialize(root);

    std::cout << "Removed dependency '" << name << "'\n";
}
void packageHelp(char** args, int argc) {
    std::cout << R"(
run: qcm run <command> - runs that command from your scope.yaml
help: qcm help - prints help text for package manager
upgrade: qcm upgrade - installs latest version of qcm
sync: qcm sync - installs neccesary packages and matches qc version
init: qcm init - initializes a project
add: qcm add <package-alias> [git <url to a git repo (of any hoster) containing a scope.yaml>]
remove: qcm remove <package-alias> - uninstalls a package
setup: qcm setup - sets up qcm
    )" << '\n';
}
const std::vector<Command> package_commands = {{"help", packageHelp}, {"upgrade", upgrade}, {"run", run}, {"sync", syncScope}, {"init", init}, {"add", add}, {"remove", removePackage}, {"setup", setup}};
const std::vector<Command> tooling_commands = {{"help", help}, {"use", use}, {"install", install}, {"uninstall", uninstall}, {"list", list}, {"list-remote", listRemote}};
int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Please pass an argument. Try `qcm tooling help` or `qcm help`";
        return 1;
    } else {
        try {
            if (std::string(argv[1]) == "tooling") {
                char** args_only = argv + 1;
                int args_count = argc - 1;
                for (Command command : tooling_commands) {
                    if (std::string(argv[2]) == command.name) {
                        command.callback(args_only, args_count);
                        return 0;
                    }
                }
                std::cout << "Command doesn't exist. Please try `qcm tooling help`";
                return 1;
            }
            for (Command command : package_commands) {
                if (std::string(argv[1]) == command.name) {
                    command.callback(argv, argc);
                    return 0;
                }
            }
            std::cout << "Command doesn't exist. Please try `qcm help`";
            return 1;
        } catch (std::string err) {
            std::cout << "Error: " << err << '\n';
        } catch (char* err) {
            std::cout << "Error: " << err << '\n';
        } catch (const char* err) {
            std::cout << "Error: " << err << '\n';
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << '\n';
        } catch (...) {
            std::cout << "Unknown Error" << '\n';
        }
        return 1;
    }
}
