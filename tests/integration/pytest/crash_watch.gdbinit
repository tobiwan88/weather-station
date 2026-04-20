# crash_watch.gdbinit - Enhanced GDB script for NSOS SIGSEGV diagnosis

define crash_context
  echo \n=== GDB CRASH CAPTURE ===\n
  bt full
  echo --- threads ---\n
  info threads
  echo =================================\n
end

# Break at nsos_poll_update entry (line 346)
b nsos_sockets.c:346
commands
  echo \n=== nsos_poll_update called ===\n
  bt 3
  x/4gx poll
  printf "poll->node.prev=0x%lx  next=0x%lx\n", *(unsigned long *)(poll+8), *(unsigned long *)poll
  echo --- nsos_polls head ---\n
  x/gx &nsos_polls
  continue
end

# Break at sys_dlist_remove in nsos_poll_update context (line 359)
b nsos_sockets.c:359
commands
  echo \n=== nsos_poll_update calling sys_dlist_remove ===\n
  x/4gx &poll->node
  printf "poll->node.prev=0x%lx  next=0x%lx\n", *(unsigned long *)(&poll->node+8), *(unsigned long *)(&poll->node)
  echo --- nsos_polls ---\n
  x/gx &nsos_polls
  continue
end

# nsos_adapt_poll_remove entry
b nsos_adapt.c:916
commands
  echo \n=== nsos_adapt_poll_remove ===\n
  bt 2
  continue
end

# NSOS epoll fatal path
b nsi_print_error_and_exit
commands
  crash_context
  continue
end

# Zephyr kernel fatal handler
b z_fatal_error
commands
  crash_context
  continue
end

# Abort signal
catch signal SIGABRT
commands
  crash_context
  continue
end

# Segfault
catch signal SIGSEGV
commands
  crash_context
  continue
end

continue
