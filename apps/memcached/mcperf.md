# twemperf (mcperf)

## Building mcperf ##

To build mcperf from distribution tarball:

    $ ./configure
    $ make
    $ sudo make install

To build mcperf from distribution tarball in _debug mode_:

    $ CFLAGS="-ggdb3 -O0" ./configure --enable-debug
    $ make
    $ sudo make install

## Help ##

    Usage: mcperf [-v verbosity level] [-o output file]
                  [-s server] [-p port]
                  [-n num-conns] [-N num-calls]
                  [-r conn-rate] [-R call-rate]

    Options:
      -v, --verbosity=N     : set logging level (default: 5, min: 0, max: 11)
      -o, --output=S        : set logging file (default: stderr)
      -s, --server=S        : set the hostname of the server (default: localhost)
      -p, --port=N          : set the port number of the server (default: 11211)
      -n, --num-conns=N     : set the number of connections to create (default: 1)
      -N, --num-calls=N     : set the number of calls to create on each connection (default: 1)
      -r, --conn-rate=R     : set the connection creation rate (default: 0 conns/sec)
      -R, --call-rate=R     : set the call creation rate (default: 0 calls/sec)

      -q, --use-noreply     : set noreply for generated requests
      ...

## Design ##

1. Single threaded.
2. Asynchronous I/O through non-blocking sockets and Linux epoll(7) syscall.

## Examples ##

The following example creates **1000 connections** to a memcached server
running on **localhost:11211**. The connections are created at the rate of
**1000 conns/sec** and on every connection it sends **10 'set' requests** at
the rate of **1000 reqs/sec** with the item sizes derived from a uniform
distribution in the interval of [1,16) bytes.

    $ mcperf --linger=0 --timeout=5 --conn-rate=1000 --call-rate=1000 --num-calls=10 --num-conns=1000 --sizes=u1,16

The following example creates **100 connections** to a memcached server
running on **localhost:11211**. Every connection is created after the previous
connection is closed. On every connection we send **100 'set' requests** and
every request is created after we have received the response for the previous
request. All the set requests generated have a fixed item size of 1 byte.

    $ mcperf --linger=0 --conn-rate=0 --call-rate=0 --num-calls=100 --num-conns=100 --sizes=d1

The following example gives you all the details of what mcperf is doing.

    $ mcperf --call-rate=0 --num-calls=100 --num-conns=1 --verbosity=11

## Protocol ##

### Storage commands ###
First, the client sends a command line which looks like this:

    <command name> <key> <flags> <exptime> <bytes> [noreply]\r\n

- `<command name>` is "set", "add", "replace", "append" or "prepend"

- `noreply` optional parameter instructs the server to not send the reply.

### Error strings ###

Each command sent by a client may be answered with an error string from the server. These error strings come in three types:

- `SERVER_ERROR <error>\r\n`

  means some sort of server error prevents the server from carrying out the command. `<error>` is a human-readable error string. In cases of severe server errors, which make it impossible to continue serving the client (this shouldn't normally happen), the server will close the connection after sending the error line. This is the only case in which the server closes a connection to a client.