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

#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

constexpr size_t BUF_SZ = 4096;
constexpr int    BACKLOG = 128;

static const char *STATUS_200 =
    "HTTP/1.1 200 OK\r\nConnection: close\r\n";
static const char *STATUS_404 =
    "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
static const char *STATUS_405 =
    "HTTP/1.1 405 Method Not Allowed\r\nAllow: GET, HEAD\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
static const char *STATUS_400 =
    "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";

/* helpers */

static bool safe_path(const std::string &u) {
    /* Reject: more than one '/' after the leading slash, or any “..” */
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
            offset += sendfile(client_fd, file_fd, &offset,
                               length - offset);
    }
}

static void handle_client(int client_fd) {
    char buf[BUF_SZ] = {0};
    ssize_t r = recv(client_fd, buf, sizeof(buf)-1, 0);
    if (r <= 0) { close(client_fd); return; }

    auto [method, url] = parse_req(buf);
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

/*  entry point  */

int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <IP> <PORT>\n";
        return 1;
    }
    const char *host = argv[1];
    const char *port = argv[2];

    addrinfo hints{}, *res;
    hints.ai_family   = AF_UNSPEC;      // v4 or v6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

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

    signal(SIGCHLD, SIG_IGN);   // auto-reap children

    while (true) {
        sockaddr_storage caddr{};
        socklen_t clen = sizeof caddr;
        int cfd = accept(srv, (sockaddr*)&caddr, &clen);
        if (cfd < 0) { perror("accept"); continue; }

        pid_t pid = fork();
        if (pid == 0) {      // child
            close(srv);
            handle_client(cfd);
            _exit(0);
        }
        close(cfd);          // parent – first copy closed
    }
}

