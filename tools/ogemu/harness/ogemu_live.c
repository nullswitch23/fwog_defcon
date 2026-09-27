/* stdin / TCP inject thread. Lines are the same JSONL as --script. */
#include "ogemu.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int s_listen_port;
static int s_use_stdin;

#ifdef _WIN32
static DWORD WINAPI live_thread(LPVOID arg)
#else
static void *live_thread(void *arg)
#endif
{
    (void)arg;
    char line[512];

    if (s_use_stdin) {
        while (fgets(line, sizeof line, stdin)) {
            if (ogemu_script_push_line(line) != 0) {
                ogemu_request_quit(2);
                break;
            }
        }
        (void)ogemu_script_push_line("quit");
#ifdef _WIN32
        return 0;
#else
        return NULL;
#endif
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "ogemu: WSAStartup failed\n");
        ogemu_request_quit(2);
        return 0;
    }
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) {
        fprintf(stderr, "ogemu: socket failed\n");
        ogemu_request_quit(2);
        return 0;
    }
    BOOL yes = 1;
    (void)setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (char *)&yes, sizeof yes);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((u_short)s_listen_port);
    if (bind(ls, (struct sockaddr *)&addr, sizeof addr) != 0 ||
        listen(ls, 1) != 0) {
        fprintf(stderr, "ogemu: bind/listen %u failed\n", s_listen_port);
        closesocket(ls);
        ogemu_request_quit(2);
        return 0;
    }
    fprintf(stderr, "ogemu: listen 127.0.0.1:%u (JSONL world bus)\n",
            s_listen_port);
    for (;;) {
        SOCKET c = accept(ls, NULL, NULL);
        if (c == INVALID_SOCKET) break;
        char buf[512];
        unsigned n = 0;
        for (;;) {
                char ch;
                int r = recv(c, &ch, 1, 0);
                if (r <= 0) break;
                if (ch == '\n') {
                    buf[n] = '\0';
                    if (ogemu_script_push_line(buf) != 0) {
                        ogemu_request_quit(2);
                        break;
                    }
                    n = 0;
                } else if (ch != '\r' && n + 1u < sizeof buf) {
                    buf[n++] = ch;
                }
            }
        closesocket(c);
    }
    closesocket(ls);
    return 0;
#else
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0) {
        perror("ogemu: socket");
        ogemu_request_quit(2);
        return NULL;
    }
    int yes = 1;
    (void)setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)s_listen_port);
    if (bind(ls, (struct sockaddr *)&addr, sizeof addr) != 0 ||
        listen(ls, 1) != 0) {
        perror("ogemu: bind/listen");
        close(ls);
        ogemu_request_quit(2);
        return NULL;
    }
    fprintf(stderr, "ogemu: listen 127.0.0.1:%u (JSONL world bus)\n",
            s_listen_port);
    for (;;) {
        int c = accept(ls, NULL, NULL);
        if (c < 0) break;
        FILE *f = fdopen(c, "r");
        if (!f) { close(c); continue; }
        while (fgets(line, sizeof line, f)) {
            if (ogemu_script_push_line(line) != 0) {
                ogemu_request_quit(2);
                break;
            }
        }
        fclose(f);
    }
    close(ls);
    return NULL;
#endif
}

int ogemu_live_stdin(void) {
    ogemu_set_live(true);
    s_use_stdin = 1;
#ifdef _WIN32
    HANDLE t = CreateThread(NULL, 0, live_thread, NULL, 0, NULL);
    if (!t) return -1;
    CloseHandle(t);
#else
    pthread_t t;
    if (pthread_create(&t, NULL, live_thread, NULL) != 0) return -1;
    (void)pthread_detach(t);
#endif
    return 0;
}

int ogemu_live_listen(unsigned port) {
    if (port == 0u || port > 65535u) return -1;
    ogemu_set_live(true);
    s_listen_port = (int)port;
    s_use_stdin = 0;
#ifdef _WIN32
    HANDLE t = CreateThread(NULL, 0, live_thread, NULL, 0, NULL);
    if (!t) return -1;
    CloseHandle(t);
#else
    pthread_t t;
    if (pthread_create(&t, NULL, live_thread, NULL) != 0) return -1;
    (void)pthread_detach(t);
#endif
    return 0;
}
