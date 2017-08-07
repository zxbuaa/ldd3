/*
 * Test out reading with poll()
 * This should run with any Unix
 *
 * Copyright (C) 2003 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2003 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 * $Id: polltest.c,v 1.1 2003/02/07 18:01:38 corbet Exp $
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
// #include <sys/poll.h>
#include <sys/select.h>
#include <fcntl.h>

char buffer[4096];

int main(int argc, char **argv)
{
    // struct pollfd pfd;
    fd_set rfds;
    struct timeval tv;
    int n;

    fcntl(0, F_SETFL, fcntl(0,F_GETFL) | O_NONBLOCK); /* stdin */
    // pfd.fd = STDIN_FILENO;  /* stdin */
    // pfd.events = POLLIN;

    while (1) {
        // n = poll(&pfd, 1, -1);
        do {
            FD_ZERO(&rfds);
            FD_SET(STDIN_FILENO, &rfds);
            tv.tv_sec = 5;
            tv.tv_usec = 0;
            n = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
        } while (n == 0);
        if (n < 0) {
            perror("select");
            break;
        }
        n = read(STDIN_FILENO, buffer, 4096);
        if (n < 0) {
            perror("stdin");
            break;
        }
        n = write(STDOUT_FILENO, buffer, n);
        if (n < 0) {
            perror("stdout");
            break;
        }
    }
    exit(1);
}
