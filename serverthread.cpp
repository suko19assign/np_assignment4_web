/*  A very small threaded web server
 *  
 *  API identical to serverfork.cpp, but each client is served
 *  by a detached std::thread instead of fork().
 */

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

constexpr size_t BUF_SZ = 4096;
constexpr int    BACKLOG = 128;
constexpr size_t HDR_LIMIT = 8192;

static const char *STATUS_200 =
    "HTTP/1.1 200 OK\r\nConnection: close\r\n";
static const char *STATUS_404 =
    "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
static const char *STATUS_405 =
    "HTTP/1.1 405 Method Not Allowed\r\nAllow: GET, HEAD\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
static const char *STATUS_400 =
    "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";

static bool safe_path(const std::string &u) {
    if (u.find("..") != std::string::npos) return false;
    return std::count(u.begin(), u.end(), '/') <= 1;
}

static std::pair<std::string,std::string> parse_req(const char *buf) {
    std::istringstream iss(buf);
    std::string m, u, v;
    iss >> m >> u >> v;
    return {m, u};
}

static void send_file(int cfd, int ffd, size_t len, bool body) {
    std::ostringstream h;
    h << STATUS_200 << "Content-Length: " << len << "\r\n\r\n";
    std::string s = h.str();
    send(cfd, s.c_str(), s.size(), 0);
    if (body && len) {
        off_t off = 0;
        while (off < (off_t)len)
            off += sendfile(cfd, ffd, &off, len - off);
    }
}

static void client_worker(int cfd) {
    timeval tv{15,0};
    setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    std::string req;
    char buf[BUF_SZ];
    while (req.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = recv(cfd, buf, sizeof(buf), 0);
        if (n <= 0) { close(cfd); return; }
        req.append(buf, n);
        if (req.size() > HDR_LIMIT) {
            send(cfd, STATUS_400, strlen(STATUS_400), 0);
            close(cfd); return;
        }
    }
    auto [method, url] = parse_req(req.c_str());
    if (method != "GET" && method != "HEAD") {
        send(cfd, STATUS_405, strlen(STATUS_405), 0);
        close(cfd); return;
    }
    if (url.empty() || url[0] != '/' || !safe_path(url)) {
        send(cfd, STATUS_400, strlen(STATUS_400), 0);
        close(cfd); return;
    }
    std::string path = "." + (url == "/" ? "/index.html" : url);
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        send(cfd, STATUS_404, strlen(STATUS_404), 0);
        close(cfd); return;
    }
    struct stat st{};
    fstat(fd, &st);
    send_file(cfd, fd, st.st_size, method == "GET");
    close(fd);
    close(cfd);
}

int main(int argc, char *argv[]) {
    std::string host_str, port_str;
    if (argc == 3) {
        host_str = argv[1];
        port_str = argv[2];
    } else if (argc == 2) {
        std::string hp = argv[1];
        size_t pos = std::string::npos;
        if (!hp.empty() && hp[0] == '[') {
            size_t close_br = hp.find(']');
            if (close_br != std::string::npos && close_br + 1 < hp.size() && hp[close_br + 1] == ':')
                pos = close_br + 1;
        }
        if (pos == std::string::npos) pos = hp.rfind(':');
        if (pos == std::string::npos) {
            std::cerr << "Usage: " << argv[0] << " <IP:PORT> or <IP> <PORT>\n";
            return 1;
        }
        host_str = hp.substr(0, pos);
        if (!host_str.empty() && host_str.front() == '[' && host_str.back() == ']')
            host_str = host_str.substr(1, host_str.size() - 2);
        port_str = hp.substr(pos + 1);
    } else {
        std::cerr << "Usage: " << argv[0] << " <IP:PORT> or <IP> <PORT>\n";
        return 1;
    }
    const char *host = host_str.empty() ? nullptr : host_str.c_str();
    const char *port = port_str.c_str();

    addrinfo hints{}, *res;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(host, port, &hints, &res) != 0) {
        perror("getaddrinfo");
        return 1;
    }

    int srv = -1;
    for (auto *p = res; p; p = p->ai_next) {
        srv = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (srv < 0) continue;
        int yes = 1;
        setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        if (bind(srv, p->ai_addr, p->ai_addrlen) == 0) break;
        close(srv); srv = -1;
    }
    freeaddrinfo(res);
    if (srv < 0) { perror("bind"); return 1; }
    if (listen(srv, BACKLOG) < 0) { perror("listen"); return 1; }

    while (true) {
        sockaddr_storage cs{};
        socklen_t clen = sizeof cs;
        int cfd = accept(srv, (sockaddr *)&cs, &clen);
        if (cfd < 0) { perror("accept"); continue; }
        std::thread(client_worker, cfd).detach();
    }
}

