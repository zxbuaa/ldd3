/*
 * asynctest.c: use async notification to read stdin
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

int gotdata=0;
void sighandler(int signo)
{
    if (signo==SIGIO)
        gotdata++;
    fprintf(stderr, "%s: %d\n", __func__, gotdata);
}

void sigaction_cb(int signo, siginfo_t *siginfo, void *context)
{
    if (signo == SIGIO)
        gotdata++;
    fprintf(stderr, "%s: from %s by uid=%d pid=%d\n", __func__,
        siginfo->si_code == SI_USER ? "KILL" :
        siginfo->si_code == SI_KERNEL ? "KERNEL" :
        siginfo->si_code == SI_TKILL ? "TKILL" : "OTHER",
        siginfo->si_uid, siginfo->si_pid);
}

char buffer[4096];

int main(int argc, char **argv)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    // action.sa_handler = sighandler;
    // action.sa_flags = 0;
    action.sa_sigaction = sigaction_cb;
    action.sa_flags = SA_SIGINFO;

    sigaction(SIGIO, &action, NULL);

    fcntl(STDIN_FILENO, F_SETOWN, getpid());
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | FASYNC | O_NONBLOCK);

    while(1) {
        int rsize;
        /* this only returns if a signal arrives */
        sleep(86400); /* one day */
        if (!gotdata)
            continue;

        while ((rsize = read(STDIN_FILENO, buffer, 4096)) > 0) {
            int wsize = 0;
            fprintf(stderr, "%s: read %d\n", __func__, rsize);
            while ((wsize += write(STDOUT_FILENO, buffer + wsize, rsize - wsize)) < rsize) {
            }
            fprintf(stderr, "%s: write %d\n", __func__, wsize);
        }

        gotdata=0;
    }
    exit(1);
}
