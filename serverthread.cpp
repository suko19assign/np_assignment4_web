#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>

/* You will to add includes here */

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

static bool safe_path(const std::string &u) {
    if (u.find("..") != std::string::npos) return false;
    return std::count(u.begin(), u.end(), '/') <= 1;
}

static std::pair<std::string,std::string> parse_req(const char *buf) {
    std::istringstream iss(buf);
    std::string m,u,v;
    iss >> m >> u >> v;
    return {m,u};
}

static void send_file(int cfd, int ffd, size_t len, bool body) {
    std::ostringstream h;
    h << STATUS_200 << "Content-Length: " << len << "\r\n\r\n";
    std::string s = h.str();
    send(cfd, s.c_str(), s.size(), 0);
    if (body && len) {
        off_t off = 0;
        while (off < (off_t)len)
            off += sendfile(cfd, ffd, &off, len-off);
    }
}

using namespace std;

int main(int argc, char *argv[]){
  
  /* Do more magic */


  
  printf("done.\n");
  return(0);


  
}
