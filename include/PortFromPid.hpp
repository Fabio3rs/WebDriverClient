#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ostream>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

struct ListeningPort {
    std::string protocol;
    std::string localAddress;
    uint16_t port;
};

inline std::ostream &operator<<(std::ostream &os, const ListeningPort &port) {
    os << "Protocol: " << port.protocol
       << ", Local Address: " << port.localAddress << ", Port: " << port.port;
    return os;
}

inline std::vector<ListeningPort> getListeningPortsForPid(int pid) {
    std::unordered_set<int> sockets;

    for (auto list : std::filesystem::directory_iterator(
             "/proc/" + std::to_string(pid) + "/fd")) {
        if (!list.is_socket()) {
            continue;
        }

        auto path = list.is_symlink()
                        ? std::filesystem::read_symlink(list.path())
                        : list.path();

        std::string socket = path.string();

        std::smatch match;
        std::regex_search(socket, match, std::regex(R"(\[([0-9]+)\])"));
        if (match.size() != 2) {
            continue;
        }

        sockets.insert(std::stoi(match[1]));
    }

    for (auto socket : sockets) {
        std::cout << "Socket: " << socket << "\n";
    }

    std::vector<ListeningPort> ports;

    for (const std::string &file :
         {"/proc/" + std::to_string(pid) + "/net/tcp",
          "/proc/" + std::to_string(pid) + "/net/udp"}) {
        std::ifstream input(file);
        if (!input.is_open()) {
            std::cerr << "Failed to open " << file << " for PID " << pid
                      << "\n";
            continue;
        }

        std::string line;
        std::getline(input, line); // Skip the header

        while (std::getline(input, line)) {
            std::istringstream iss(line);
            std::string localAddr;
            std::string inode;
            int state{};

            iss >> localAddr >> std::hex >> state >> std::dec >> inode;

            std::smatch match;
            std::regex_search(localAddr, match,
                              std::regex(R"((.*):([0-9A-F]+))"));
            if (match.size() != 3) {
                continue;
            }

            if (sockets.find(std::stoi(match[2])) == sockets.end()) {
                continue;
            }

            ListeningPort port;
            port.protocol =
                file.find("tcp") != std::string::npos ? "tcp" : "udp";
            port.localAddress = match[1];
            port.port = static_cast<uint16_t>(std::stoi(match[2], nullptr, 16));

            ports.push_back(port);
        }
    }

    return ports;
}
