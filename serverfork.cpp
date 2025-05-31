/*  A very small forking web server
 *  ───────────────────────────────
 *  Usage:  ./serverfork <IP> <PORT>
 *  Example: ./serverfork 0.0.0.0 8282
 *
 *  Handles GET and HEAD for files that live in the current working directory
 *  (at most one “/” after the initial “/” in the URL, no “..”).  Every accepted
 *  connection is served in its own child process created with fork(2).
 *
 *  IPv4/IPv6 agnostic – we rely on getaddrinfo().
 */

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>

#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

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
    size_t slash_cnt = std::count(u.begin(), u.end(), '/');
    return slash_cnt <= 1;
}

static std::pair<std::string,std::string> parse_req(const char *buf) {
    std::istringstream iss(buf);
    std::string method, url, version;
    iss >> method >> url >> version;
    return {method, url};
}

static void send_file(int client_fd, int file_fd, size_t length, bool body) {
    std::ostringstream hdr;
    hdr << STATUS_200
        << "Content-Length: " << length << "\r\n\r\n";
    std::string h = hdr.str();
    send(client_fd, h.c_str(), h.size(), 0);
    if (body && length) {
        off_t offset = 0;
        while (offset < static_cast<off_t>(length))
            offset += sendfile(client_fd, file_fd, &offset, length - offset);
    }
}

static void handle_client(int client_fd) {
    timeval tv{15,0};
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    std::string req;
    char buf[BUF_SZ];
    while (req.find("\r\n\r\n") == std::string::npos) {
        ssize_t r = recv(client_fd, buf, sizeof(buf), 0);
        if (r <= 0) { close(client_fd); return; }
        req.append(buf, r);
        if (req.size() > HDR_LIMIT) {
            send(client_fd, STATUS_400, strlen(STATUS_400), 0);
            close(client_fd);
            return;
        }
    }
    auto [method, url] = parse_req(req.c_str());
    if (method != "GET" && method != "HEAD") {
        send(client_fd, STATUS_405, strlen(STATUS_405), 0);
        close(client_fd);
        return;
    }
    if (url.empty() || url[0] != '/' || !safe_path(url)) {
        send(client_fd, STATUS_400, strlen(STATUS_400), 0);
        close(client_fd);
        return;
    }
    std::string path = "." + (url == "/" ? "/index.html" : url);
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        send(client_fd, STATUS_404, strlen(STATUS_404), 0);
        close(client_fd);
        return;
    }
    struct stat st{};
    fstat(fd, &st);
    send_file(client_fd, fd, st.st_size, method == "GET");
    close(fd);
    close(client_fd);
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

    signal(SIGCHLD, SIG_IGN);
    while (true) {
        sockaddr_storage caddr{};
        socklen_t clen = sizeof caddr;
        int cfd = accept(srv, (sockaddr *)&caddr, &clen);
        if (cfd < 0) { perror("accept"); continue; }
        pid_t pid = fork();
        if (pid == 0) {
            close(srv);
            handle_client(cfd);
            _exit(0);
        }
        close(cfd);
    }
}

