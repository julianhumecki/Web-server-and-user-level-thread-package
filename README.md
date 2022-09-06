# Multi-threaded web server & user level thread library

•	Developed a caching, multi-threaded file web server using sockets, and a pool of pthreads in C   
•	Used mutual exclusion and synchronization primitives to ensure coordinated access to the file-cache shared by all threads in the thread_pool    
•	Improved performance of server by an average speedup of 210% when compared to a non-caching file server 

•	Developed a user-level pre-emptive threading library from scratch with mutual exclusion and synchronization primitive support in C 
•	Implemented the standard methods included in a threading library: thread_create, thread_yield, thread_exit, thread_kill, thread_sleep, thread_wakeup, thread_wait, lock_acquire, lock_release, cv_wait, cv_signal, cv_broadcast, and received a mark of 100% based on my library’s functionality and efficiency
