handle SIGSEGV nostop noprint nopass
break dbg_panic_halt
break hard_shutdown
break bootstrap
break kshell_test
break kshell_help

s gdb_wait = 0
continue
