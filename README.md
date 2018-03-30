# Upenn CIS505 HW3
Multicast servers/clients that distribute chat messages to different group  
The server supports Unordered, FIFO and Total orderings  

## Syntax
./chatclient [Server IP:port]  
./chatserver [-o order] [-v] <config file> <index>  
-o followed by ordering type, -v is debug flag, config file is the file that store forward and bind address of servers, index indicate which line in the file is this server

## Test
### Use proxy to simulate message delay in localhost
### Use stresstest to verify capacity and ordering
