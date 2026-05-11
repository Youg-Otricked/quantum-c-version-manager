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
const std::string QCVM_VERSION = "0.2.0";
#include <string_view>
constexpr std::string_view TAGGED_VERSIONS[] = { "x0.15.8" };

/*
root structure = 
**~/.qcvm/qcvm.yaml**
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
    if (!std::filesystem::exists(home + "/.qcvm/config.yaml")) {
        throw "Config file doesn't exist. Please run `qcvm setup`\n";
    }
    FILE* config = std::fopen((home + "/.qcvm/config.yaml").c_str(), "r");
    if (config == nullptr) {
        throw "Config file doesn't exist. Please run `qcvm setup`\n";
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
    std::ofstream file(home + "/.qcvm/config.yaml");
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
        std::cout << ver << (std::ranges::any_of(TAGGED_VERSIONS, [ver](auto v) {return v == ver;}) ? " [QCVM tagged]" : "" ) << (node["current"].get_value<std::string>() == ver ? " *" : "") << '\n';
    }
}
void setup(char** args, int argc) {
    const char* home_raw = getenv("HOME");
    if (!home_raw) {
        throw "HOME not set\n";
    }
    std::string home(home_raw);
    std::filesystem::create_directories(home + "/.qcvm/versions");
    std::filesystem::create_directories(home + "/.qc/bin");
    std::filesystem::create_directories(home + "/.qc/lib");
    if (!std::filesystem::exists(home + "/.qcvm/config.yaml")) {
        std::ofstream file(home + "/.qcvm/config.yaml");
        auto node = fkyaml::node::deserialize(R"(
installed: []
current: null
)");
        file << node;
        file.close();
    } else {
        std::cout << "Config file already exists\n";
    }
    std::string shell;
    const char* shell_raw = getenv("SHELL");
    if (shell_raw) shell = shell_raw;
    std::string rcFile;
    if (shell.ends_with("zsh")) rcFile = home + "/.zshrc";
    else if (shell.ends_with("fish")) rcFile = home + "/.config/fish/config.fish";
    else rcFile = home + "/.bashrc";
    std::string exportLine = "export PATH=\"$HOME/.qc/bin:$PATH\"";
    if (shell.ends_with("fish")) {
        exportLine = "fish_add_path $HOME/.qc/bin";
    }
    std::ifstream rcIn(rcFile);
    std::string rcContent((std::istreambuf_iterator<char>(rcIn)), std::istreambuf_iterator<char>());
    rcIn.close();
    if (rcContent.find(".qc/bin") != std::string::npos) {
        std::cout << "PATH already configured in " << rcFile << "\n";
    } else {
        std::ofstream rcOut(rcFile, std::ios::app);
        rcOut << "\n# Added by qcvm\n" << exportLine << "\n";
        std::cout << "Added .qc/bin to PATH in " << rcFile << "\n";
        std::cout << "Run: source " << rcFile << "\n";
    }
}
void help(char** args, int argc) {
    std::cout << R"(QCVM )" << QCVM_VERSION << R"(
Commands:
help: qcvm help - lists all commands, usages, and syntax.
sync: qcvm sync - installs all dependancys and switches qc version to projects used version.
use: qcvm use <version> - changes currently used version to other currently installed version.
install: qcvm install <version> - installs version <version> and changed currently used version to it.
init: qcvm init - intializes a simple project using a setup wizard.
uninstall: qcvm uninstall <version> - uninstalls <version>.
get: qcvm get <link> - installs the qc file <link> points to in the projects deps directory.
list: qcvm list - lists installed versions of qc.
list-remote: qcvm list-remote - lists all remote versions of qc.
current: qcvm current - prints current qc version.
setup: qcvm setup - creates the basic files and the .qcvm directory. Only needs to be ran on install.
run: qcvm run <command> - runs that command defined in your scope.yaml.
remove: qcvm remove <dependancy url> - removes that dependancy.
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
        << "\ndependencies: []\n"
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
        entrypointLine = "#entrypoint=\"" + entry + "\"\n";
    }
    std::cout << "will this project be a one file or multi file project? (Y/n) ";
    std::getline(std::cin, choice);
    bool multiFile = false;
    if (choice == "Y" || choice == "y") {
        std::filesystem::create_directory("dependancys");
        multiFile = true;
    }
    std::ofstream mainFile("main.qc");
    mainFile << inlineDirs << entrypointLine <<
    (multiFile ? "namespace Exported {\n    \n}\n" : "")
    << "int " << entry << "() {\n" << "    return 0;" 
    << '\n' << '}' << '\n';
    mainFile.close();
    std::cout << "project setup complete!\n";
    std::cout.flush();
    std::cout << "\033[?1049l";
    std::cout << "project setup complete!" << '\n';
}
std::string getVersions() {
    httplib::Client client("https://api.github.com");
    client.set_default_headers({{"User-Agent", "qcvm/1.0"}});
    
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
        std::cout << tag << (std::ranges::any_of(TAGGED_VERSIONS, [tag](auto v) {return v == tag;}) ? " [QCVM tagged]" : "") << (deprecated ? " [deprecated]" : "") << '\n';
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
// https://github.com/Youg-Otricked/QuantumC/releases/download/<tag>/<filename>
void install(char** args, int argc) {
    if (argc < 3) {
        throw "Usage: `qcvm install <version>`";
    }
    if (!isValidVersion(args[2])) {
        throw "Please pass a valid version.";
    }
    std::string currentQCVersion = getCurrentQCVersion();
    if (getConfigFileNode()["installed"].contains(args[2])) {
        throw "Version " + std::string(args[2]) + "already installed. Did you mean to run `qcvm use " + args[2] + "`?";
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
    if (currentQCVersion == std::string(args[2])) {
        throw "Version " + std::string(args[2]) + " is already installed. Did you mean to run `qcvm use " + args[2] + "`? (JK it wasnt stored becuase you installed it but not via QCVM)";
    }
    httplib::Client client("https://github.com");
    client.set_default_headers({{"User-Agent", "qcvm/1.0"}});
    client.set_follow_location(true);
    const char* home_raw = getenv("HOME");
    if (!home_raw) {
        throw "HOME not set\n";
    }
    std::string home(home_raw);
    std::string versionDir = home + "/.qcvm/versions/";
    std::string binPath = versionDir + "qc-" + args[2];
    std::ofstream binFile(binPath, std::ios::binary);
    size_t total = 0;
    size_t downloaded = 0;

    auto res = client.Get(
        "/Youg-Otricked/QuantumC/releases/download/" + std::string(args[2]) + "/" + (getOS() == "linux" ? "qc-linux" : "qc-macos"),
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
void use(char** args, int argc) {
    if (argc < 3) {
        throw "Usage: `qcvm use <version>`";
    }
    const char* home_raw = getenv("HOME");
    if (!home_raw) throw "HOME not set\n";
    std::string home(home_raw);
    std::string versionDir = home + "/.qcvm/versions/";
    std::string binPath = versionDir + "qc-" + args[2];
    std::string stdlibPath = versionDir + "stdlib-" + args[2] + ".qc";
    if (!std::filesystem::exists(binPath) || !std::filesystem::exists(stdlibPath)) {
        throw "Version " + std::string(args[2]) + " not installed. Run `qcvm install " + args[2] + "`";
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
    node["current"] = args[2];
    saveConfigFileNode(node);
    if (std::filesystem::exists("scope.yaml")) {
        std::ifstream in("scope.yaml");
        auto scnode = fkyaml::node::deserialize(in);
        scnode["project"]["qc_version"] = std::string(args[2]);
        std::ofstream out("scope.yaml");
        out << fkyaml::node::serialize(scnode);
    }
    std::cout << "Now using QC " << args[2] << "\n";
}
void uninstall(char** args, int argc) {
    if (argc < 3) {
        throw "Usage: `qcvm uninstall <version>`";
    }
    if (!isValidVersion(args[2])) {
        throw "Version " + std::string(args[2]) + " isn't installed.";
    }
    const char* home_raw = getenv("HOME");
    if (!home_raw) throw "HOME not set\n";
    std::string home(home_raw);
    if (std::filesystem::exists(home + "/.qcvm/versions/qc-" + args[2])) {
        std::filesystem::remove(home + "/.qcvm/versions/qc-" + args[2]);
    }
    if (std::filesystem::exists(home + "/.qcvm/versions/stdlib-" + args[2] + ".qc")) {
        std::filesystem::remove(home + "/.qcvm/versions/stdlib-" + args[2] + ".qc");
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
        throw "usage: `qcvm run <command>`";
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
    }
    char* install_args[] = {args[0], (char*)"install", version.data()};
    install(install_args, 3);
}
void getPackage(char** args, int argc) {
    if (argc < 3) {
        throw std::string("Usage: `qcvm get <url>`");
    }
    if (!std::filesystem::exists("scope.yaml")) {
        throw std::string("No scope.yaml found in current directory.");
    }
    std::string url(args[2]);
    std::string filename = url.substr(url.find_last_of('/') + 1);
    if (filename.empty()) {
        throw std::string("Could not determine filename from URL.");
    }
    std::filesystem::create_directories("dependencies");
    std::string outPath = "dependencies/" + filename;
    std::string host, path;
    bool https = url.starts_with("https://");
    std::string stripped = url.substr(https ? 8 : 7);
    size_t slash = stripped.find('/');
    host = stripped.substr(0, slash);
    path = slash == std::string::npos ? "/" : stripped.substr(slash);
    std::ofstream outFile(outPath, std::ios::binary);
    size_t total = 0, downloaded = 0;
    auto doGet = [&](auto& client) {
        auto res = client.Get(
            path,
            [&](const httplib::Response& response) {
                total = std::stoull(response.get_header_value("Content-Length", "0"));
                return true;
            },
            [&](const char* data, size_t len) {
                outFile.write(data, len);
                downloaded += len;
                int pct = total ? (downloaded * 100 / total) : 0;
                int bars = pct / 5;
                std::cout << "\rDownloading " << filename << " - ["
                          << std::string(bars, '=') << std::string(20 - bars, ' ')
                          << "] " << pct << "%" << std::flush;
                return true;
            }
        );
        return res;
    };
    httplib::Result res;
    if (https) {
        httplib::SSLClient client(host);
        client.set_follow_location(true);
        client.set_default_headers({{"User-Agent", "qcvm/1.0"}});
        res = doGet(client);
    } else {
        httplib::Client client(host);
        client.set_follow_location(true);
        client.set_default_headers({{"User-Agent", "qcvm/1.0"}});
        res = doGet(client);
    }
    std::cout << '\n';
    outFile.close();
    if (!res || res->status != 200) {
        std::filesystem::remove(outPath);
        throw std::string("Failed to fetch ") + url;
    }
    std::ifstream in("scope.yaml");
    auto scnode = fkyaml::node::deserialize(in);
    in.close();
    scnode["dependencies"].as_seq().emplace_back(url);
    std::ofstream out("scope.yaml");
    out << fkyaml::node::serialize(scnode);
    std::cout << "Added " << filename << " to dependencies/\n";
}
const std::vector<Command> commands = {{"help", help}, {"run", run}, Command{"sync", syncScope}, {"use", use}, {"install", install}, {"init", init}, {"uninstall", uninstall}, Command{"get", getPackage}, {"list", list}, {"list-remote", listRemote}, {"setup", setup}};
int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Please pass an argument. Try `qcvm help`";
        return 1;
    } else {
        try {
            for (Command command : commands) {
                if (std::string(argv[1]) == command.name) {
                    command.callback(argv, argc);
                    return 0;
                }
            }
            std::cout << "Command doesn't exist. Please try `qcvm help`";
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