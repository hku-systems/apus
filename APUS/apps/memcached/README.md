   The memcached server cannot work when compiling in RDMA machines, so I copy the whole server from hemingserver1 and submit it to github.

To connect to memcached server, using telnet as a client. First parameter is host while second is port.

> telnet 127.0.0.1 11211
type version to see if it could work, it will return VERSION id.
Escape character is '^]'.
version
VERSION 1.4.25
    
Usage: <command name> <key> <flags> <exptime> <bytes>\r\n <data block>\r\n
for example: 
set mykey 22 0 8 
12345678
             
<command name> means operations: set, get, replace ...
<key> means a key's name you want to operate
<flags> should be a random value, it seems there is no meaning.
<exptime> should be 0, means never over time
<bytes> mean the length of the value
Remember press enter button after: set mykey 22 0 8, and start pressing value in another line. 
The length of value id defined as 8 bytes, so it could have 8 letters. I press 12345678 as an example.

For example get mykey
it will return :
    VALUE mykey 0 8
    12345678 


