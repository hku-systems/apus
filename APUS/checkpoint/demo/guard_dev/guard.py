#!/usr/bin/env python
"""
Author: Jingyu
Date: 6 April, 2016
Description: A guard for handling do_checkpoint and do_restore command.

Work Flow:
1. Starting. After RDMA program has been stared. The following information, including node_id, name and cfg file path, will be prepared as command parameters of the guard.py.

python guard.py node_id aim_name rdma.cfg

2. Providing services. The guard will start two types of interfaces(inner and outer). The inner interface is an unix socket, which is used for accepting command from the RDMA program in the same machine. The outer interface will be a HTTP service, which implements the operations, such as checkpoint and restore. The detailed interface will be introduced in the next section.


Interface:

Inner: unix socket
1. checkpoint node_id round_id
A RDMA program (the leader) will send this command to $UNIX_SOCK. 
If the node_id is not itself, the command will route to the destnation, through outer.1.

The guard will launch criu to make a checkpoint, then wake up the RDMA program.
Once the checkpoint dump files are created, they will be packed as a zip file then thie file will be copied to others through outer interface with the help of scp command.

2. restore node_id round_id
A RDMA program (the leader) will send this command to $UNIX_SOCK. The guard will 
If the node_id is not itself, the command will route to the destnation, through outer interface.

The guard will launch criu to restore a checkpoint. Firstly, a version which is nearest and smaller than round_id is selected. Then the guard will kill the RDMA program through kill -9. After that, the guard will launch criu command to restore the program.

Outer: TCP socket, HTTP service
1. checkpoint?node_id=XX&round_id=XXXX
A URL such as http://$BIND_HOST:$BIND_PORT/checkpoint?node_id=XX&round_id=XXXX will be created and be sent to node_id machine. The guard will firstly reply this request. Then start a thread to do checkpoint which is descripted in Inner.1

2. restore?node_id=XX&round_id=XXXX
A URL such as http://$BIND_HOST:$BIND_PORT/restore?node_id=XX&round_id=XXXX will be created and be sent to node_id machine. The guard will firstly reply this request. The start a thread to do restore which is descripted in Inner.2

"""

from wsgiref.simple_server import make_server
from cgi import parse_qs, escape
import SocketServer
from multiprocessing import Process
import subprocess
import time
import sys
import os
import tempfile
import zipfile
import shutil
import string
import pylibconfig2 as cfg
import urllib2
import threading
import socket
import stat

# They are global variables, and will be initialised in init()
#=================
# The location of RDMA configuration file path, I need parse ip and port, and then find PID by lsof
RDMA_CFG=""
# PID of the process which will be checkpoint
AIM_PID=-1
# self id repesents the guard
SELF_ID=-1 
# Dir for storing checkpoint zip files.
STORE_BASE="/tmp/checkpoint_store"
STORE_UPPER="/tmp"
# kill cmd 
AIM_NAME="redis-server"
KILL_CMD="pkill -9 %s"%(AIM_NAME)
GETPID_CMD="ps -ef | grep  %s | grep -v python | grep -v ssh | grep -v sh | grep -v grep | awk '{print $2}' | head -n 1"%(AIM_NAME)
# RSYNC cmd
RSYNC_CMD=""
# user name
RSYNC_USER=""
NODE_INFO=None
#=================
# The IP:PORT I am listening for service.
BIND_HOST="0.0.0.0"
BIND_PORT=12345

#disconnect cmd
DISCONNECT_CMD="disconnect\n"
RECONNECT_CMD="reconnect\n"
LIBEVENT_SOCK="/tmp/checkpoint.server.sock"
# The unix socket I am listening for accepting internal request.
UNIX_SOCK="/tmp/guard.sock"

# all db files should be stored into this dir
EXT_RES_DIR_PARENT="/data/"
EXT_RES_DIR="/data/store/"
# EXT_RES_DIR will be copied into this dir, and then be packed
EXT_RES_DIR_INNER="ext_res_dir/" 
FD_INDEX_NAME="fd_index.txt"
FD_STORE_DIR_INNER="fd_dir/"
#update pid every 5 s
TIMER_SECS=5.0
#check point timer 5 mins
CK_TIMER_SECS=5.0*60
# only do checkpoint at no.1
TIMER_CK_ID=1

DEF_INDEX="""<!DOCTYPE html>
<html>
<body>
<h1>Guard works.</h1>
<div>
<div>
<a href='/checkpoint?node_id=1&round_id=1'>checkpoint</a>
</div>
<div>
<a href='/restore?node_id=1&round_id=1'>restore</a>
</div>
</div>
</body>
</html>
"""

def cfg_init(fname):
	global RSYNC_CMD,RSYNC_USER,NODE_INFO
	RSYNC_USER = os.getlogin()
	fsize = 4096 # max size of a cfg
	if os.path.exists(fname):
		f = open(fname,'r')
		buff = f.read(fsize)	
		conf = cfg.Config(buff)
		node_info = conf.consensus_config
		NODE_INFO = node_info
		#print "[cfg_init] ", node_info	
		# update rsync
		try:
			self_conf = node_info[SELF_ID]	
			self_ip = self_conf.ip_address
			#"rsync -aP --delete %s hkucs@10.22.1.2:%s ; rsync -aP --delete %s hkucs@10.22.1.3:%s"%(STORE_BASE,STORE_UPPER,STORE_BASE,STORE_UPPER)
			rsync_option="rsync -aP --delete "
			RSYNC_CMD=""
			for item in node_info:
				if item.ip_address != self_ip: # replicas
					RSYNC_CMD=RSYNC_CMD + rsync_option + STORE_BASE +" %s@%s:%s ; "%(RSYNC_USER,item.ip_address,STORE_UPPER)	
		except Exception as e:
			print "[cfg_init] configuration error. exit"
			exit()
		print "[cfg_init] rsync_cmd: %s"%(RSYNC_CMD)
	else:
		print "[cfg_init] %s is not found. I will exit."%(fname)

# The init function will parse $RDMA_CFG
def init():
	global SELF_ID, AIM_NAME, RDMA_CFG
	try:
		argv = sys.argv
		SELF_ID=int(argv[1])
		AIM_NAME=argv[2]
		KILL_CMD="pkill -9 %s"%(AIM_NAME)
		GETPID_CMD="ps -ef | grep  %s | grep -v python | grep -v ssh | grep -v grep | awk '{print $2}' | head -n 1"%(AIM_NAME)
		reset_pid()
		RDMA_CFG=argv[3]
	except Exception as e:
		print "[init] error: %s"%(str(e))
		exit()
	print "The guard has got self_id:%d, aim_pid:%d, and cfg file: %s"%(SELF_ID,AIM_PID,RDMA_CFG)
	if os.path.exists(STORE_BASE):
		shutil.rmtree(STORE_BASE)
		os.mkdir(STORE_BASE)
	else: # dir is not existed, we need create one
		os.mkdir(STORE_BASE)
	cfg_init(RDMA_CFG)

# default handler of index.html
def index(environ, start_response):
	start_response('200 OK',[('Content-Type', 'text/html')])
	return [DEF_INDEX] 

# default handler of 404
def not_found(environ, start_response):
	start_response('404 NOT FOUND', [('Content-Type', 'text/plain')])
	return ['Not Found']

def outer_parse(environ):
	parameters = parse_qs(environ.get('QUERY_STRING', ''))
	try:
		node_id = escape(parameters['node_id'][0])
		round_id = escape(parameters['round_id'][0])
		node_id = int(node_id)
		round_id = int(round_id)
		return (node_id,round_id)
	except Exception as e:
		print "[outer_checkpoint] error: %s"%(str(e))
		return (-1,-1)

def outer_checkpoint(environ, start_response):
	start_response('200 OK',[('Content-Type', 'text/html')])
	node_id, round_id = outer_parse(environ)
	print "[outer_checkpoint] node_id:%d, round_id:%d"%(node_id,round_id)
	if SELF_ID == node_id:
		inner_checkpoint(node_id,round_id)
	else:
		print "[outer_checkpoint] access deny, id is invalid SELF_ID:%d, node_id:%d"%(SELF_ID,node_id)
	return ['checkpoint ok\n']	

def outer_restore(environ, start_response):
	start_response('200 OK',[('Content-Type', 'text/html')])
	node_id, round_id = outer_parse(environ)
	print "[outer_restore] node_id:%d, round_id:%d"%(node_id,round_id)
	if SELF_ID == node_id:
		inner_restore(node_id,round_id)
	else:
		print "[outer_checkpoint] access deny, id is invalid SELF_ID:%d, node_id:%d"%(SELF_ID,node_id)
	return ['restore ok\n']

# The implementation is based on http://lucumr.pocoo.org/2007/5/21/getting-started-with-wsgi/
def application(environ, start_response):
	"""
	The main WSGI application. This function will dispatch the current request to currect handler.
	"""
	urls=[
		('index', index),
		('checkpoint', outer_checkpoint),
		('restore',outer_restore)
	]
	path = environ.get("PATH_INFO")
	for prefix, callback in urls:
		match = path[1:].startswith(prefix)
		if match:
			return callback(environ, start_response)
	return not_found(environ, start_response)

def getMaxCheckPointID():
	max_id = 0
	for root, dirs, files in os.walk(STORE_BASE):
		for name in files:
			#print "[getNextBaseName] name: %s"%(name)
			if name.startswith("checkpoint_"):
				prefix_size = len("checkpoint_")
				sufix_size = len(".zip")
				file_id = name[prefix_size:len(name)-sufix_size]
				absName = os.path.join(root,name)
				#print "[getNextBaseName] absName: %s, current file_id: %s"%(absName,file_id)	
				try:
					file_id = int(file_id)
					if max_id < file_id:
						max_id = file_id
				except Exception as e:
					pass
	return max_id
def getCurrBaseName():
	max_id = getMaxCheckPointID()
	return os.path.join(STORE_BASE,"checkpoint_%d"%(max_id))
def getNextBaseName():
	max_id = getMaxCheckPointID() 
	next_id = max_id+1
	return os.path.join(STORE_BASE,"checkpoint_%d"%(next_id))

def send_reconnect_cmd():
	"""
	Send a reconnect cmd to a unix socket, which is listened by libevent thread in RDMA code.
	"""
	sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	try:
		MAX_RECV=512
		sock.connect(LIBEVENT_SOCK)
		sock.send(RECONNECT_CMD)
		reply = sock.recv(MAX_RECV)
		print "[send_reconnect_cmd] %s, reply: %s"%(RECONNECT_CMD,reply)	
	except Exception as e:
		print "[send_reconnect_cmd] send %s failed at unix socket, err:%s"%(RECONNECT_CMD,str(e))
		pass

def send_disconnect_cmd():
	"""
	Send a disconnect cmd to a unix socket, which is listened by libevent thread in RDMA code.
	"""	
	sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	try:
		MAX_RECV=512
		sock.connect(LIBEVENT_SOCK)
		sock.send(DISCONNECT_CMD)
		reply = sock.recv(MAX_RECV)
		print "[send_disconnect_cmd] %s, reply: %s"%(DISCONNECT_CMD,reply)	
	except Exception as e:
		print "[send_disconnect_cmd] send %s failed at unix socket, err:%s"%(DISCONNECT_CMD,str(e))
		pass

def generate_fd_index(tmpDir,AIM_PID):
	"""
	1. to generate a fd_index.txt,
	"""
	print "[generate_fd_index] tmpDir: %s, AIM_PID:%d"%(tmpDir,AIM_PID)
	if -1 == AIM_PID:
		return -1
	fd_index_path = os.path.join(tmpDir,FD_INDEX_NAME);
	fd_index=None
	try:
		fd_index = open(fd_index_path,"wb");
	except Exception as e:
		print "[generate_fd_index] open %s failed."%(fd_index_path)
		return -1
	if None != fd_index: 
		proc_fd_dir = "/proc/%d/fd"%(AIM_PID)
		print "[generate_fd_index] reading %s"%(proc_fd_dir)
		dirs = os.listdir(proc_fd_dir)
		for item in dirs:
			abs_path = os.path.join(proc_fd_dir,item)
			orig_path = os.readlink(abs_path)
			# can not use orig_path, since some are not standard file name. However, os.stat will follow the softlink
			mode = os.stat(abs_path).st_mode 
			if stat.S_ISREG(mode):
				print "[generate_fd_index] found fd files:%s"%(orig_path)
				fd_index.write("%s\n"%(orig_path))
		print "[generate_fd_index] fd_index will be closed"
		fd_index.close()
		return 0
	else:
		return -1

def pack_ext_res(tmpDir,AIM_PID):
	"""
	Two steps:
	1. to read a fd_index.txt, then pack all opened fd but regular files. into dir fd_dir/.
	2. pack a directory define as EXT_RES_DIR, such as /data into ext_res_dir/.
	"""
	print "[pack_ext_res] tmpDir: %s, AIM_PID:%d"%(tmpDir,AIM_PID)
	# read fd_index.txt
	fd_index_path = os.path.join(tmpDir,FD_INDEX_NAME);
	fd_dir = os.path.join(tmpDir,FD_STORE_DIR_INNER)
	os.mkdir(fd_dir)
	fd_index=None
	try:
		fd_index = open(fd_index_path,"rb")
		lines = fd_index.readlines()
		for item in lines:
			item = item.strip('\n')
			shutil.copy(item, fd_dir)
			print "[pack_ext_res] %s has been stored into %s."%(item,fd_dir)
	except Exception as e:
		print "[pack_ext_res] open file %s failed."%(fd_index_path)
		return -1
	# pack EXT_RES_DIR
	ext_res_dir_inner = os.path.join(tmpDir,EXT_RES_DIR_INNER)
	try:
		if os.path.exists(EXT_RES_DIR):
			shutil.copytree(EXT_RES_DIR,ext_res_dir_inner)
			print "[pack_ext_res] %s has been stored into %s"%(EXT_RES_DIR,ext_res_dir_inner)
		else:
			print "[pack_ext_res] %s has not been existed. skipped."%(EXT_RES_DIR)			
		return 0
	except Exception as e:
		print "[pack_ext_res] %s failed to be stored.%s"%(EXT_RES_DIR,str(e))
		return -1

def unpack_ext_res(tmpDir):
	"""
	Two steps:
	1. to read fd_index.txt, then replace files from fd_dir/ to abslute path.
	2. to replace /data from ext_res_dir/
	"""
	print "[unpack_ext_res] tmpDir: %s"%(tmpDir)
	# read fd_index.txt
	fd_index_path = os.path.join(tmpDir,FD_INDEX_NAME);
	fd_dir = os.path.join(tmpDir,FD_STORE_DIR_INNER)
	fd_index=None
	try:
		fd_index = open(fd_index_path,"rb")
		lines = fd_index.readlines()
		for item in lines:
			item = item.strip('\n')
			base_name = os.path.basename(item)
			src_name = os.path.join(fd_dir,base_name)
			try:
				shutil.copyfile(src_name,item)
				print "[unpack_ext_res] %s has been copy to %s."%(src_name,item)
			except Exception as e:
				print "[unpack_ext_res] %s failed to restore."%(item)
				pass
	except Exception as e:
		print "[pack_ext_res] open file %s failed."%(fd_index_path)
		return -1
	# replace
	ext_res_dir_inner = os.path.join(tmpDir,EXT_RES_DIR_INNER)
	# remove it first, then move
	try:
		shutil.rmtree(EXT_RES_DIR)
		if os.path.exists(ext_res_dir_inner):
			shutil.move(ext_res_dir_inner,EXT_RES_DIR_PARENT)
			return 0
		else:
			return -1
	except Exception as e:
		print "[unpack_ext_res] replace error. error: %s"%(str(e))
		return -1

def inner_checkpoint(node_id,round_id):
	print "[inner_checkpoint] criu will be used for checkpointing pid: %d at machine %d, at round %d"%(AIM_PID,node_id,round_id)
	# mkdir dump
	tmpDir = None
	try:
		tmpDir = tempfile.mkdtemp()
	except Exception as e:
		pass
	if tmpDir:
		send_disconnect_cmd()
		print "[inner_checkpoint] sleep 1 seconds to wait for disconnect"
		time.sleep(1)
		reset_pid()
		# before criu dump, fd_index.txt should be generated and be stored into tmpDir.
		# return 0 means ok
		retcode = generate_fd_index(tmpDir,AIM_PID)
		if retcode:
			print "[inner_checkpoint] generate_fd_index called but failed.\n"
			sys.stdout.flush()
			return
		#remove --shell-job
		#remove --leave-running
		cmd="/sbin/criu dump -v4 --file-locks -o /tmp/criu.dump.log -D %s -t %d"%(tmpDir,AIM_PID)
		print "[inner_checkpoint]cmd: %s"%(cmd)
		retcode = subprocess.call(cmd,shell=True)
		if retcode:
			print "[inner_checkpoint] criu dump error, please cat /tmp/criu.dump.log"
		else:
			# Currently, the target has been closed by criu.
			# pack all external resource, such as files, into tmpDir.
			pack_ext_res(tmpDir,AIM_PID);
			zipBaseName = getNextBaseName() # without extName
			zipAbsName = shutil.make_archive(zipBaseName,'zip',tmpDir)
			print "[inner_checkpoint] checkpoint %s is created."%(zipAbsName)
			shutil.rmtree(tmpDir)
			#rsync zip file to others
			print "[inner_checkpoint] rsync cmd: %s"%(RSYNC_CMD)
			subprocess.call(RSYNC_CMD,shell=True)
			# restore RDMA 
			inner_restore(node_id,round_id)
	else:
		print "[inner_checkpoint]creat tmpDir failed."
	sys.stdout.flush()
	return


def reset_pid():
	"""
		criu may load checkpoint.zip from other machine. The pid will be changed after restore.
	"""	
	global AIM_PID 
	try:
		pid_str = subprocess.check_output(GETPID_CMD,shell=True)
		pid = int(pid_str)  
		if AIM_PID != pid:
			AIM_PID = pid
			print "[reset_pid] AIM_PID has been updated as %d"%(AIM_PID)
	except Exception as e:
		print "[reset_pid] Failed to get pid by cmd: %s"%(GETPID_CMD)
	threading.Timer(TIMER_SECS,reset_pid).start()
	
def inner_restore(node_id,round_id):
	print "[inner_restore] criu will be used for restoring at machine %d, at round %d"%(node_id,round_id)
	# mkdir dump
	sys.stdout.flush()
	tmpDir = None
	currZip = getCurrBaseName()+".zip"
	print "[inner_restore] find current checkpoint file %s"%(currZip)
	try:
		tmpDir = tempfile.mkdtemp()
	except Exception as e:
		pass
	if tmpDir and os.path.exists(currZip):
		# unzip
		with zipfile.ZipFile(currZip,'r') as zf:
			zf.extractall(tmpDir)
		# kill
		reset_pid() 
		subprocess.call(KILL_CMD,shell=True)
		# wait for kill
		time.sleep(1)
		# Before criu restore, fd files and external files need to be replaced.
		unpack_ext_res(tmpDir)
		# remove --shell-job
		cmd="/sbin/criu restore -v4 --file-locks -o /tmp/criu.restore.log -d -D %s"%(tmpDir)
		print "[inner_restore]cmd: %s"%(cmd)
		retcode = subprocess.call(cmd,shell=True)
		if retcode :
			print "[inner_restore] criu restore failed. please cat /tmp/criu.restore.log"
		shutil.rmtree(tmpDir)
		reset_pid()
		# send reconnect cmd
		send_reconnect_cmd()	
		sys.stdout.flush()
		return
	else:
		print "[inner_restore] unable to to restore since %s is missing."%(currZip) 

def inner_service(cmd,node_id,round_id):
	urls=[
		('checkpoint',inner_checkpoint),
		('restore',inner_restore)	
	]
	path = cmd
	for prefix, callback in urls:
		match = (path == prefix)
		if match:
			callback(node_id,round_id)
	return

def getIPbyID(node_id):
	ret_ip="0.0.0.0"
	if not NODE_INFO:
		print "[getIPbyID] node info has not been init"
		exit()
	try:
		ret_ip = NODE_INFO[node_id].ip_address
	except Exception as e:
		print "[getIPbyID] out of bound %d in %d"(node_id,len(NODE_INFO))
	return	ret_ip 

def getPortbyID(node_id):
	return int(BIND_PORT) 

def make_req(url):
	content = urllib2.urlopen(url).read()
	print "[make_req] ret:%s"%(content)
	return 
def route(cmd,node_id,round_id):
	ip_str=getIPbyID(node_id)	
	port_int=getPortbyID(node_id)
	url = "http://%s:%d/%s?node_id=%d&round_id=%d"%(ip_str,port_int,cmd,node_id,round_id)
	print "[route] url:%s"%(url)
	make_req(url)
	return

# The handler for unix socket
class InnerHandler(SocketServer.BaseRequestHandler):
	def handle(self):
		print "[inner] Client is connected."
		recv_size=4096
		request = self.request.recv(recv_size)
		request = request.strip('\n')
		print "[inner] recv:#%s#"%(request)
		parts = request.split()
		if 3!=len(parts):
			print "[inner] Invalied parameters."
			self.request.sendall("[inner] ERROR")
			return
		else:
			(cmd,node_id,round_id) = parts
			print "[inner] cmd: %s , node_id: %s, round_id: %s"%(cmd,node_id,round_id)
			try:
				node_id = int(node_id)
				round_id = int(round_id)
			except Exception as e:
				print "[init] error: %s"%(str(e))
				self.request.sendall("[inner] ERROR")
				return
			print "[inner] self_id: %d, node_id: %d"%(SELF_ID,node_id)
			if SELF_ID == node_id: # This is a inner call
				inner_service(cmd,node_id,round_id)	
				self.request.sendall("[inner] OK. The calling is served.\n")
			else: # This is a outer call
				route(cmd,node_id,round_id)
				self.request.sendall("[inner] OK. The calling is routed.\n")
			sys.stdout.flush()

# The function will start outer interface handler in a thread
def start_outer(args):
	print "[outer] Interface(http://%s:%d) will start."%(BIND_HOST,BIND_PORT)
	srv = make_server(BIND_HOST, BIND_PORT, application)
	srv.serve_forever()

def timer_checkpoint():
	print "[timer checkpoint] checkpoint will be done at this machine"
	inner_checkpoint(SELF_ID,-1)
	print "[inner] check point will be done in every %d seconds."%(CK_TIMER_SECS)
	threading.Timer(CK_TIMER_SECS, timer_checkpoint).start()

# The function will start inner interface handler in a thread
def start_inner(args):
	try:
		os.unlink(UNIX_SOCK)
	except OSError:
		if os.path.exists(UNIX_SOCK):
			print "[inner] ERR file %s cannot be removed."%(UNIX_SOCK)
			print "[inner] Interface failed."
			return
	threading.Timer(TIMER_SECS, reset_pid).start()
	if TIMER_CK_ID == SELF_ID:
		print "[inner] check point will be done in every %d seconds."%(CK_TIMER_SECS)
		threading.Timer(CK_TIMER_SECS, timer_checkpoint).start()
	print "[inner] Interface(%s) will start."%(UNIX_SOCK)
	srv = SocketServer.UnixStreamServer(UNIX_SOCK,InnerHandler)	
	srv.serve_forever()

# print the usage of this python program
def usage():
	print "Usage:\npython guard.py node_id aim_name rdma.cfg"

if __name__ == '__main__':
	argc = len(sys.argv)
	if 4!=argc:
		usage()
		exit()
	else:
		init()
		outer = Process(target=start_outer,args=((),))
		outer.start()
		inner = Process(target=start_inner,args=((),))
		inner.start()
		outer.join()
		inner.join()
