/*
 *
 *
 * ++Copyright BAKA++
 *
 * Copyright © 2003-2011 The Authors. All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Send e-mail to <projectbaka@baka.org> for further information.
 *
 * - -Copyright BAKA- -
 */

#include <stdio.h>
#include <stdlib.h>

#define NUMBER 4*1024*1024

main(int argc, char *argv[])
{
  struct timeval tv[NUMBER];
  int x;

  for (x=0; x<NUMBER; x++)
  {
    gettimeofday(&tv[x],NULL);
  }

  for (x=1; x<NUMBER; x++)
  {
    struct timeval tvm;
    tvm.tv_sec = tv[x].tv_sec - tv[x-1].tv_sec;
    tvm.tv_usec = tv[x].tv_usec - tv[x-1].tv_usec;

    while (tvm.tv_usec < 0)
    {
      tvm.tv_sec--;
      tvm.tv_usec += 1000000;
    }

    while (tvm.tv_usec > 1000000)
    {
      tvm.tv_sec--;
      tvm.tv_usec -= 1000000;
    }

    //printf("%d %d.%06d\n", x, tvm.tv_sec, tvm.tv_usec);
    //printf("%d %d.%06d\n", x, tv[x].tv_sec, tv[x].tv_usec);
    printf("%d.%06d %d.%06d\n", tv[x].tv_sec, tv[x].tv_usec, tvm.tv_sec, tvm.tv_usec);
  }
}
