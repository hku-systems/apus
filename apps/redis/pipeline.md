## Request/Response protocols and RTT
By default every client sends the next command only when the reply of the previous command is received, this means that the server will likely need a read call in order to read each command from every client. Also RTT is paid as well.

So for instance a four commands sequence is something like this:
- *Client*: INCR X
- *Server*: 1
- *Client*: INCR X
- *Server*: 2
- *Client*: INCR X
- *Server*: 3
- *Client*: INCR X
- *Server*: 4

## Redis Pipelining
A Request/Response server can be implemented so that it is able to process new requests even if the client didn't already read the old responses. This way it is possible to send *multiple commands* to the server without waiting for the replies at all, and finally read the replies in a single step.

This is an example using the raw netcat utility:
```
$ (printf "PING\r\nPING\r\nPING\r\n"; sleep 1) | nc localhost 6379
+PONG
+PONG
+PONG
```
This time we are not paying the cost of RTT for every call, but just one time for the three commands.

To be very explicit, with pipelining the order of operations of our very first example will be the following:
- *Client*: INCR X
- *Client*: INCR X
- *Client*: INCR X
- *Client*: INCR X
- *Server*: 1
- *Server*: 2
- *Server*: 3
- *Server*: 4

**IMPORTANT NOTE**: While the client sends commands using pipelining, the server will be forced to queue the replies, using memory. 

## It's not just a matter of RTT
Pipelining is not just a way in order to reduce the latency cost due to the round trip time, it actually improves by a huge amount the total operations you can perform per second in a given Redis server. This is the result of the fact that, without using pipelining, serving each command is very cheap from the point of view of accessing the data structures and producing the reply, but it is very costly from the point of view of doing the socket I/O. This involes calling the `read()` and `write()` syscall, that means going from user land to kernel land. The context switch is a huge speed penalty.

When pipelining is used, many commands are usually read with a single `read()` system call, and multiple replies are delivered with a single `write()` system call. Because of this, the number of total queries performed per second initially increases almost linearly with longer pipelines, and eventually reaches 10 times the baseline obtained not using pipelining, as you can see from the following graph:
