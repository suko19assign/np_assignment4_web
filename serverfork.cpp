#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>

/* You will to add includes here */

static const char *STATUS_200 =
    "HTTP/1.1 200 OK\r\nConnection: close\r\n";
static const char *STATUS_404 =
    "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
static const char *STATUS_405 =
    "HTTP/1.1 405 Method Not Allowed\r\nAllow: GET, HEAD\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
static const char *STATUS_400 =
    "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
    
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

using namespace std;

int main(int argc, char *argv[]){
  
  /* Do more magic */


  
  printf("done.\n");
  return(0);


  
}
