# CSC209-A2
Assignment 2: Implementing a Shell
Makefile        # Compiles your shell program and runs the tests
README          # This file
tsh.c           # The shell program that you will write and make your video on
tshref          # The reference shell binary.

# The remaining files are used to test your shell
sdriver.pl      # The trace-driven shell driver
trace*.txt      # The 17 trace files that control the shell driver
tshref.out      # Example output of the reference shell on all 17 traces

# Little C programs that are called by the trace files
myspin.c        # Takes argument <n> and spins for <n> seconds
mysplit.c       # Forks a child that spins for <n> seconds
mystop.c        # Spins for <n> seconds and sends SIGTSTP to itself
myint.c         # Spins for <n> seconds and sends SIGINT to itself
