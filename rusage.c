#if 0
/*─────────────────────────────────────────────────────────────────╗
│ To the extent possible under law, Justine Tunney has waived      │
│ all copyright and related or neighboring rights to this file,    │
│ as it is written in the following disclaimers:                   │
│   • http://unlicense.org/                                        │
│   • http://creativecommons.org/publicdomain/zero/1.0/            │
╚─────────────────────────────────────────────────────────────────*/
#endif

#include <stdio.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/errno.h>
#include <math.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h> // For CLK_TCK.

// #define CLK_TCK 100

#define PREFIX "\e[1mRL\e[0m: "

int64_t time_usec() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000000 + tv.tv_usec;
}

static void PrintResourceReport(struct rusage *ru) {
  long utime, stime;
  long double ticks;
  if (ru->ru_maxrss) {
    fprintf(stderr, "%sballooned to %ld kB (%1.3f MB) in size\n", PREFIX,
            ru->ru_maxrss / 1024, (double) ru->ru_maxrss / (1024.0 * 1024.0));
  }
  if ((utime = ru->ru_utime.tv_sec * 1000000 + ru->ru_utime.tv_usec) |
      (stime = ru->ru_stime.tv_sec * 1000000 + ru->ru_stime.tv_usec)) {
    ticks = ceill((long double)(utime + stime) / (1000000.L / CLK_TCK));
    fprintf(stderr, "%sneeded %1.6f s cpu (%d%% kernel)\n", PREFIX,
            (double) (utime + stime) / 1000000.0,
            (int)((long double)stime / (utime + stime) * 100));
    if (ru->ru_idrss) {
      fprintf(stderr, "%sneeded %ld kB memory on average\n", PREFIX,
              lroundl(ru->ru_idrss / ticks));
    }
    if (ru->ru_isrss) {
      fprintf(stderr, "%sneeded %ld kB stack on average\n", PREFIX,
              lroundl(ru->ru_isrss / ticks));
    }
    if (ru->ru_ixrss) {
      fprintf(stderr, "%smapped %ld kB shared on average\n", PREFIX,
              lroundl(ru->ru_ixrss / ticks));
    }
  }
  if (ru->ru_minflt || ru->ru_majflt) {
    fprintf(stderr, "%scaused %ld page faults (%d%% memcpy)\n", PREFIX,
            ru->ru_minflt + ru->ru_majflt,
            (int)((long double)ru->ru_minflt / (ru->ru_minflt + ru->ru_majflt) *
                  100));
  }
  if (ru->ru_nvcsw + ru->ru_nivcsw > 1) {
    fprintf(stderr, "%s%ld context switches (%d%% consensual)\n", PREFIX,
            ru->ru_nvcsw + ru->ru_nivcsw,
            (int)((long double)ru->ru_nvcsw / (ru->ru_nvcsw + ru->ru_nivcsw) *
                  100));
  }
  if (ru->ru_msgrcv || ru->ru_msgsnd) {
    fprintf(stderr, "%sreceived %ld message%s and sent %ld\n", PREFIX,
            ru->ru_msgrcv, ru->ru_msgrcv == 1 ? "" : "s", ru->ru_msgsnd);
  }
  if (ru->ru_inblock || ru->ru_oublock) {
    fprintf(stderr, "%sperformed %ld read%s and %ld write i/o operations\n",
            PREFIX, ru->ru_inblock, ru->ru_inblock == 1 ? "" : "s",
            ru->ru_oublock);
  }
  if (ru->ru_nsignals) {
    fprintf(stderr, "%sreceived %ld signals\n", PREFIX, ru->ru_nsignals);
  }
  if (ru->ru_nswap) {
    fprintf(stderr, "%sgot swapped %ld times\n", PREFIX, ru->ru_nswap);
  }
}

struct rusage rusage;

int main(int argc, char *argv[]) {
  int pid, wstatus;
  long double ts1, ts2;
  sigset_t chldmask, savemask;
  struct sigaction dflt, ignore, saveint, savequit;
  if (argc < 2) {
    fprintf(stderr, "Usage: %s PROG [ARGS...]\n", argv[0]);
    return 1;
  }
  dflt.sa_flags = 0;
  dflt.sa_handler = SIG_DFL;
  sigemptyset(&dflt.sa_mask);
  ignore.sa_flags = 0;
  ignore.sa_handler = SIG_IGN;
  sigemptyset(&ignore.sa_mask);
  sigaction(SIGINT, &ignore, &saveint);
  sigaction(SIGQUIT, &ignore, &savequit);
  sigemptyset(&chldmask);
  sigaddset(&chldmask, SIGCHLD);
  sigprocmask(SIG_BLOCK, &chldmask, &savemask);
  ts1 = time_usec();
  pid = fork();
  if (pid == -1) {
    perror("fork failed");
    exit(1);
  }
  if (!pid) {
    sigaction(SIGINT, &dflt, 0);
    sigaction(SIGQUIT, &dflt, 0);
    sigprocmask(SIG_SETMASK, &savemask, 0);
    execvp(argv[1], argv + 1);
    fprintf(stderr, "exec failed %d\n", errno);
    _Exit(127);
  }
  while (wait4(pid, &wstatus, 0, &rusage) == -1) {
    if (errno != EINTR) {
      perror("unexpected error");
      exit(1);
    }
  }
  ts2 = time_usec();
  sigaction(SIGINT, &saveint, 0);
  sigaction(SIGQUIT, &savequit, 0);
  sigprocmask(SIG_SETMASK, &savemask, 0);
  fprintf(stderr, "%stook %0.6f s wall time\n", PREFIX,
          (double) (ts2 - ts1) / 1000000.0);
  PrintResourceReport(&rusage);
  if (WIFEXITED(wstatus)) {
    return WEXITSTATUS(wstatus);
  } else {
    return 128 + WTERMSIG(wstatus);
  }
}
