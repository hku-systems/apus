# Guard.py checkpoint and restore procedure

# CheckPoint

CheckPoint procedure will use criu to dump a running process memory image into files. The procedure will be as follows.

1. to send "disconnect" command to RDMA program through unix socket.

1. to collect the information about regular files which are opened by RDMA program and write them into fd_index.txt. And then copy files into fd_dir/. This step must be before criu dump, becuase the /proc/pid/fd information is only avaiable when the process is alive.

1. to call /sbin/criu dump, in this case, the RDMA program will be closed by criu

1. to pack an extra dir, for example, /data/store/ into ext_res_dir/

1. zip all files into a zip file, for example, checkpoint_1.zip.

1. to dilivery the new zip file to all other machines.

1. to restore self, becuase the process has been closed by criu. In the restore procuder, "reconnect" command will be sent.

# Restore

Restore procedure will unzip the checkpoint_[N].zip, in which N will be the largest one. Then uses criu restore command to clone a new process in the machine.

1. to find checkpoint_[N].zip and zip it into a tempDir.

1. find current pid and kill the program.

1. to unpack an extra dir, for example, ext_res_dir/ into /data/store/. And unpack fd_dir/

1. to call criu restore.

1. to clean tempDir.

1. to obtain new pid, since the pid maybe changed when restoring in a different machine.

1. to send "reconnect" command to RDMA program through unix socket.

