/*
 * Dedicated to the public
 */

#include <libbk.h>

int sig=SIGTERM;
int timeout=10;
int finished=0;
int status;
int pid;
int waitaftersignal=0;
/*
 * This program exits with bizzare exit status so that it's more likely that
 * user scripts will be able to determine if the exit status derived from the
 * subprocess or this program.
 */
int error_exit=143;

void usage(void);


void reaper(int signum);
void reaper(int signum)
{
  while(waitpid(pid,&status,0)>=0);
  finished++;
  exit(status>>8);
}

int main(int argc, char **argv)
{
  char ch;

  while ((ch = getopt(argc, argv, "+s:t:e:w")) != EOF)
  {
    switch(ch)
    {
    case 'e':
      error_exit=atoi(optarg);
      break;
    case 's':
      sig=atoi(optarg);
      break;
    case 't':
      timeout=atoi(optarg);
      break;
    case 'w':
      waitaftersignal++;
      break;
    default:
      usage();
    }
  }
  argc -= optind;
  argv += optind;

  if (!argc)
  {
    usage();
    exit(1);
  }

  if (signal(SIGCHLD,reaper) == SIG_ERR)
  {
    perror("signal");
    exit(error_exit);
  }

  if (!(pid=fork()))
  {
    if (execvp(*argv,argv)<0)
    {
      perror("fork");
      exit(error_exit);
    }

  }
  sleep(timeout);

  if (!finished)
  {
    kill(pid,sig);
    if (waitaftersignal)
    {
      waitpid(pid,&status,0);
      exit(WEXITSTATUS(status));
    }
    exit(error_exit);
  }
  exit(WEXITSTATUS(status));
}


void usage(void)
{
  fprintf(stderr,"timeout [[-e timeout_exit_code ] | [-w]] [-t timeout] [-s signal] <cmd> [args]\n");
}
