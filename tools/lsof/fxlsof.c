// SPDX-License-Identifier: MIT
// fxlsof: the part of lsof that Steam's client uses, for x86 guests under FEX on Android.
//
// Steam checks the peer of its UI websocket with `lsof -P -F upnR -i TCP@127.0.0.1:<port>`
// (logs/transport_client.txt: GetIPCConnectionDetails). Real lsof reads /proc/net/tcp,
// which Android denies to apps, and NETLINK_SOCK_DIAG is denied too. FEX records every
// guest TCP socket (patches/fex/src/AndroidTcpRegistry.h), and libfxpath does the same for
// native arm64 programs such as Valve's arm64 client (tools/fxpath); this tool answers from those
// records and reports a socket only while /proc/<pid>/fd of its process still holds it,
// so the answer is as real as lsof's. Stale records are deleted.
//
// Supported: -i [proto][@host][:port], -F <fields> (p R u n; p is always printed), -P, -n.
// Output matches lsof -F: per process "p<pid>" "R<ppid>" "u<uid>", then "n<local>-><remote>".
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_HITS 256

// Used when FXD_ROOT is unset; build-payload.sh passes the build's root with -D.
#ifndef FXD_DEFAULT_ROOT
#define FXD_DEFAULT_ROOT "/data/data/ro.cobrabm.fexdroid/files/rootfs"
#endif

struct hit {
  int pid;
  char name[200];
};

static const char* registry_dir(char* buf, size_t len) {
  const char* root = getenv("FXD_ROOT");
  snprintf(buf, len, "%s/usr/share/fex-emu/fexdroid-tcp", root && *root ? root : FXD_DEFAULT_ROOT);
  return buf;
}

// Does /proc/<pid>/fd still hold socket:[inode]?
static int owns_socket(int pid, const char* inode) {
  char dir[64], want[300], path[320], link[300];
  snprintf(dir, sizeof(dir), "/proc/%d/fd", pid);
  snprintf(want, sizeof(want), "socket:[%s]", inode);
  DIR* d = opendir(dir);
  if (!d) {
    return 0;
  }
  int found = 0;
  struct dirent* e;
  while (!found && (e = readdir(d))) {
    if (e->d_name[0] == '.') {
      continue;
    }
    snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
    ssize_t n = readlink(path, link, sizeof(link) - 1);
    if (n > 0) {
      link[n] = '\0';
      found = strcmp(link, want) == 0;
    }
  }
  closedir(d);
  return found;
}

static int proc_field(int pid, int want_ppid) {
  char path[64], line[256];
  if (want_ppid) {
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE* f = fopen(path, "r");
    if (!f) {
      return -1;
    }
    int ppid = -1;
    if (fgets(line, sizeof(line), f)) {
      const char* p = strrchr(line, ')'); // comm may contain spaces
      if (p) {
        sscanf(p + 1, " %*c %d", &ppid);
      }
    }
    fclose(f);
    return ppid;
  }
  snprintf(path, sizeof(path), "/proc/%d/status", pid);
  FILE* f = fopen(path, "r");
  if (!f) {
    return -1;
  }
  int uid = -1;
  while (fgets(line, sizeof(line), f)) {
    if (sscanf(line, "Uid: %d", &uid) == 1) {
      break;
    }
  }
  fclose(f);
  return uid;
}

static void host_name(const char* addr, int numeric, char* out, size_t len) {
  if (!numeric && (strcmp(addr, "127.0.0.1") == 0 || strcmp(addr, "::1") == 0)) {
    snprintf(out, len, "localhost");
  } else if (strchr(addr, ':')) {
    snprintf(out, len, "[%s]", addr);
  } else {
    snprintf(out, len, "%s", addr);
  }
}

static int addr_matches(const char* want_host, int want_port, const char* addr, int port) {
  if (want_port >= 0 && want_port != port) {
    return 0;
  }
  if (!want_host) {
    return 1;
  }
  if (strcmp(want_host, addr) == 0) {
    return 1;
  }
  // localhost means the loopback addresses, as lsof resolves it.
  return strcmp(want_host, "localhost") == 0 && (strcmp(addr, "127.0.0.1") == 0 || strcmp(addr, "::1") == 0);
}

int main(int argc, char** argv) {
  const char* fields = NULL;
  const char* spec = NULL;
  int numeric = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
      spec = argv[++i];
    } else if (strncmp(argv[i], "-i", 2) == 0 && argv[i][2]) {
      spec = argv[i] + 2;
    } else if (strcmp(argv[i], "-F") == 0 && i + 1 < argc && argv[i + 1][0] != '-') {
      fields = argv[++i];
    } else if (strncmp(argv[i], "-F", 2) == 0) {
      fields = argv[i][2] ? argv[i] + 2 : "pn";
    } else if (strcmp(argv[i], "-n") == 0) {
      numeric = 1;
    } else if (strcmp(argv[i], "-P") == 0) {
      // Ports are always numeric here.
    } else {
      fprintf(stderr, "fxlsof: unsupported option %s (only -i/-F/-P/-n)\n", argv[i]);
      return 1;
    }
  }
  if (!spec) {
    fprintf(stderr, "fxlsof: only -i queries are supported\n");
    return 1;
  }

  // [46][protocol][@hostname|hostaddr][:service|port]
  char buf[256];
  snprintf(buf, sizeof(buf), "%s", spec);
  char* s = buf;
  if (*s == '4' || *s == '6') {
    s++;
  }
  char* at = strchr(s, '@');
  char* colon = strrchr(s, ':');
  if (at && colon && colon < at) {
    colon = NULL;
  }
  int want_port = -1;
  if (colon) {
    *colon = '\0';
    want_port = atoi(colon + 1);
  }
  const char* want_host = NULL;
  if (at) {
    *at = '\0';
    want_host = at + 1;
    if (*want_host == '[') { // [::1]
      want_host++;
      char* end = strchr(at + 1, ']');
      if (end) {
        *end = '\0';
      }
    }
  }
  if (*s && strcasecmp(s, "TCP") != 0) {
    return 1; // UDP and others are not recorded.
  }

  char dirpath[512];
  DIR* d = opendir(registry_dir(dirpath, sizeof(dirpath)));
  if (!d) {
    return 1;
  }
  struct hit hits[MAX_HITS];
  int nhits = 0;
  struct dirent* e;
  while ((e = readdir(d)) && nhits < MAX_HITS) {
    if (!isdigit((unsigned char)e->d_name[0]) || strchr(e->d_name, '.')) {
      continue;
    }
    char path[800], line[256];
    snprintf(path, sizeof(path), "%s/%s", dirpath, e->d_name);
    FILE* f = fopen(path, "r");
    if (!f) {
      continue;
    }
    char* ok = fgets(line, sizeof(line), f);
    fclose(f);
    int pid, lport, rport;
    char laddr[64], raddr[64];
    if (!ok || sscanf(line, "%d %63s %d %63s %d", &pid, laddr, &lport, raddr, &rport) != 5) {
      continue;
    }
    if (!owns_socket(pid, e->d_name)) {
      unlink(path); // Closed socket or dead process.
      continue;
    }
    int remote = strcmp(raddr, "-") != 0;
    if (!addr_matches(want_host, want_port, laddr, lport) && !(remote && addr_matches(want_host, want_port, raddr, rport))) {
      continue;
    }
    char lh[80], rh[80];
    host_name(laddr, numeric, lh, sizeof(lh));
    hits[nhits].pid = pid;
    if (remote) {
      host_name(raddr, numeric, rh, sizeof(rh));
      snprintf(hits[nhits].name, sizeof(hits[nhits].name), "%s:%d->%s:%d", lh, lport, rh, rport);
    } else {
      snprintf(hits[nhits].name, sizeof(hits[nhits].name), "%s:%d", lh, lport);
    }
    nhits++;
  }
  closedir(d);
  if (!nhits) {
    return 1;
  }

  // One process set per pid (lsof prints processes in pid order), then its files.
  for (int i = 0; i < nhits; i++) {
    for (int j = i + 1; j < nhits; j++) {
      if (hits[j].pid < hits[i].pid) {
        struct hit t = hits[i];
        hits[i] = hits[j];
        hits[j] = t;
      }
    }
  }
  for (int i = 0; i < nhits; i++) {
    if (i == 0 || hits[i].pid != hits[i - 1].pid) {
      if (fields) {
        printf("p%d\n", hits[i].pid);
        if (strchr(fields, 'R')) {
          printf("R%d\n", proc_field(hits[i].pid, 1));
        }
        if (strchr(fields, 'u')) {
          printf("u%d\n", proc_field(hits[i].pid, 0));
        }
      }
    }
    if (fields) {
      if (strchr(fields, 'n')) {
        printf("n%s\n", hits[i].name);
      }
    } else {
      printf("%d TCP %s\n", hits[i].pid, hits[i].name);
    }
  }
  return 0;
}
