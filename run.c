#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// fork(), aborting on error.
// Return the pid
static int fork_or_die() {
  int pid = fork();
  if (pid < 0) {
    perror("fork");
    _exit(-1);
  }
  return pid;
}

static void noop(int ignore) {}

int main(int argc, char *argv[]) {
  /**
   * Consider shell code like `/bin/echo hi | cat`. Here the shell wants to
   * place `cat` in `echo`'s process group. However `echo` may exit very
   * quickly. The desired behavior is that `cat` can join `echo`s process group
   * even if `echo` has exited (but before it is reaped). This behavior is
   * realized on Linux and macOS; it fails on WSL.
   *
   * This test app creates two children: a leader and a follower, places the
   * follower in the leader's process group, and then signals the leader's
   * process group. On macOS/Linux, the follower can join the pgrp and receive
   * the signal regardless of whether the leader has exited (expected behavior).
   * On Windows 10 WSL, if the leader is still alive (keepalive_leader) things
   * are copacetic. However  if the leader has exited, the follower cannot join
   * the leader's process group and cannot receive the signal.
   */
  int keepalive_leader = argc > 1 && !strcmp(argv[1], "keepalive");

  // Handle SIGUSR1 so our children can awake from pause().
  signal(SIGUSR1, noop);

  // Launch the group leader. It may exit quickly.
  int leader = fork_or_die();
  if (leader == 0) {
    // Leader process.
    printf("Leader %d spawned\n", getpid());
    if (setpgid(0, getpid()) < 0) {
      perror("leader failed to enter its own group");
    }
    if (keepalive_leader)
      pause();
    printf("Leader %d exiting\n", getpid());
    _exit(0);
  }

  // Have the parent set the leader to lead its own pgroup.
  if (setpgid(leader, leader) < 0) {
    // This is the known race between parent-side setpgid and the child exiting.
    // It is expected to fail occasionally.
    // perror("leader parentside setpgid");
  }
  // Wait a bit, allowing the leader to exit if it decides to.
  sleep(1);

  // Create a new child and place it in the (possibly exited) leader's group.
  int follower = fork_or_die();
  if (follower == 0) {
    // Follower process.
    printf("Follower %d spawned\n", getpid());
    if (setpgid(0, leader) < 0) {
      perror("follower failed to setpgid to leader's group");
    }
    pause();
    printf("Follower %d exiting\n", getpid());
    _exit(0);
  }
  if (setpgid(follower, leader) < 0) {
    perror("child parentside setpgid");
  }
  sleep(1);

  printf("Parent signalling pgrp %d\n", leader);
  if (killpg(leader, SIGUSR1) < 0) {
    perror("killpg");
  }
  waitpid(leader, 0, 0);
  waitpid(follower, 0, 0);
  printf("Parent reaped children, exiting\n");
  return 0;
}
