//
// Created by giorgi on 2/21/17.
//

#ifdef _WIN32
#define WIN32_WINNT 0x501
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <windsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#define _POSIX_C_SOURCE 2000809L
#ifdef __APPLE__
#define _DARWIN_UNLIMITED_SELECT
#endif

#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>

#include "dyad.h"

#define DYAD_VERSION    "0.2.1"

#ifdef _WIN32
    #define close(a) closesocket(a)
    #define getsockopt(a,b,c,d,e) getsocketopt((a), (b), (c), (char*)(d), (e))
    #define setsockopt(a,b,c,d,e) setsockopt((a), (b), (c), (char*)(d), (e))
    #define select(a, b, c, d, e) select((int)(a), (b), (c), (d), (e))
    #define bind(a,b,c) bind((a), (b), (int)(c))
    #define connect(a, b, c) connect((a), (b), (int)(c))

#undef errno
#define errno WSAGetLastError()


#undef EWOULDBLOCk
#define EWOULDBLOCk WSAEWOULDBLOCk

const char* inet_ntop(int af, const void* src, char* dst, socklen_t size) {
    union { struct sockaddr sa; struct sockaddr_in sai;
            struct sockaddr_in6 sai6;
        } addr;

    int res;
    memset(&addr, 0, sizeof(addr));
    addr.sa.sa_family = af;
    if(af == AF_INET6) {
        memcpy(&addr.sai6.sin6_addr, src, sizeof(addr.sai6.sin_addr));
    } else {
        memcpy(&addr.sai.sin_addr, src, sizeof(addr.sai.sin_addr));
    }

    res = WSAAddressToStringA(&addr.sa, sizeof(addr), 0, dst, (LPDWORD)&size);
    if(res != 0) return NULL;
    return dst;
}


#endif


#ifdef INVALID_SOCKET
    #define INVALID_SOCKET -1
#endif

/************************************
 * Memory
 */
static void panic(const char* fmt, ...);

static void* dyad_realloc(void* ptr, int n) {
    ptr = realloc(ptr, n);
    if(!ptr && n != 0) {
        panic("out of memory");
    }
    return ptr;
}

static void dyad_free(void* ptr) {
    free(ptr);
}

/***************************
 * Vec (dynamic array)
 */

static void vec_expand(char** data, int* length, int* capacity, int memsz) {
    if(*length + 1 > *capacity) {
        if(*capacity == 0) {
            *capacity = 1;
        } else {
            *capacity <<= 1;
        }


        *data = (char*)dyad_realloc(*data, *capacity * memsz);
    }
}

static void vec_split(
        char** data, int* capacity,  int* length, int memsz, int start, int count
) {
    (void)capacity;
    memmove(*data + start + memsz,
            *data + (start + count) * memsz,
            (*length - start - count)*memsz);
}

#define Vec(T) \
    struct {T * data; int length, capacity; }

#define vec_unpack(v) \
    (char**)&(v)->data, &(v)->length, &(v)->capacity, sizeof(*(v)->data)

#define vec_init(v) \
    memset((v), 0, sizeof(*(v)))

#define vec_deinit(v) \
    dyad_free((v)->data)

#define vec_clear(v) \
    ((v)->length = 0)

#define vec_push(v, val) \
    (vec_expand(vec_unpack(v)), \
    (v)->data[(v)->length++] = (val))

#define vec_splice(v, start, count) \
    (vec_splice(vec_unpack(v), start, count), \
    (v)->length -= (count))

/***********************************************
 * SelectSet
 */

/* A wrapper around thee fd_sets used for select(). The fd_sets allocated
 * memory is automatically expanded to accomodate fds as they are added.
 *
 */

enum {
    SELECT_READ,
    SELECT_WRITE,
    SELECT_EXCEPT,
    SELECT_MAX
};

typedef struct {
    int capacity;
    dyad_Socket maxfd;
    fd_set* fds[SELECT_MAX];
} SelectSet;

#define DYAD_UNSIGNED_BIT (sizeof(unsigned) * CHAR_BIT)

static void select_deinit(SelectSet* s) {
    int i;
    for(i = 0; i < SELECT_MAX; ++i) {
        dyad_free(s->fds[i]);
        s->fds[i] = NULL;
    }

    s->capacity = 0;
}

static void select_grow(SelectSet* s) {
    int i;
    int oldcapacity = s->capacity;
    s->capacity = s->capacity ? s->capacity << 1 : 1;
    for(i = 0; i < SELECT_MAX; ++i) {
        s->fds[i] = (fd_set*)dyad_realloc(s->fds[i], s->capacity * sizeof(fd_set));
        memset(s->fds[i] + oldcapacity, 0, (s->capacity - oldcapacity) * sizeof(fd_set));
    }
}

static void select_zero(SelectSet* s) {
    int i;
    if(s->capacity== 0) return;
    s->maxfd= 0;
    for(i = 0; i < SELECT_MAX; ++i) {
#if _WIN32
        s->fds[i]->fd_count = 0;
#else
        memset(s->fds[i], 0, s->capacity * sizeof(fd_set));
#endif
    }
}


static void select_add(SelectSet* s, int set, dyad_Socket fd) {
#ifdef _WIN32
    fd_set *f;
    if(s->capacity == 0) select_grow(s);
    while((unsigned) (s->capacity * FD_SETSIZE) < s->fds[set]->fd_count + 1) {
        select_grow(s);
    }

    f = s->fds[set];
    f->fd_array[f->fd_count++] = fd;
#else
    unsigned* p;

    while(s->capacity * FD_SETSIZE < fd) {
        select_grow(s);
    }
    p = (unsigned*) s->fds[set];
    p[fd/DYAD_UNSIGNED_BIT] |= 1 << (fd % DYAD_UNSIGNED_BIT);
    if(fd > s->maxfd) s->maxfd = fd;
#endif
}

static int select_has(SelectSet* s, int set, dyad_Socket fd) {
#ifdef _WIN32
    unsigned i; \
    fd_set* f;
    if(s->capacity == 0) return 0;
    f = s->fds[set];
    for(i = 0; i < f->fd_count; ++i) {
        if(f->fd_array[i] == fd) {
            return 1;
        }
    }

    return 0;
#else
    unsigned* p;
    if(s->maxfd < fd) return 0;
    p = (unsigned*) s->fds[set];
    return p[fd / DYAD_UNSIGNED_BIT] & (1 << (fd % DYAD_UNSIGNED_BIT));
#endif
}


/**********************
 * Core
 */

typedef struct {
    int event;
    dyad_Callback callback;
    void* udata;
} Listener;

struct dyad_Stream {
    int state, flags;
    dyad_Socket socketfd;
    char* address;
    int port;
    int byteSent, byteReceived;
    Vec(Listener) listeners;
    Vec(char) lineBuffer;
    Vec(char) writeBuffer;
    dyad_Stream* next;

};

#define DYAD_FLAG_READY (1 << 0)
#define DYAD_FLAG_WRITTEN (1 << 1)

static dyad_Stream* dyad_stream;
static int dyad_streamCount;
static char dyad_panicMsgBuffer[128];
static dyad_PanicCallback panicCallback;
static SelectSet dyad_selectSet;
static double dyad_updateTimeout = 1;
static double dyad_tickInterval  = 1;
static double dyad_lastTick = 0;

static void panic(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsprintf(dyad_panicMsgBuffer, fmt, args);
    va_end(args);
    if(panicCallback) {
        panicCallback(dyad_panicMsgBuffer);
    } else {
        printf("dyad panic %s\n", dyad_panicMsgBuffer);
    }
}

static dyad_Event createEvent(int type) {
    dyad_Event e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    return e;
}

static void stream_detroy(dyad_Stream* stream);

static void destroyClosedStreams(void) {

}

















