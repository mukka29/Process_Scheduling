1. Compile with make
$ make
gcc -Werror -ggdb -c queue.c
gcc -Werror -ggdb master.c queue.o -o master
gcc -Werror -ggdb user.c -o user

2. Execute the program
  $ ./master

  #Master output is in log.txt
  $ cat log.txt
