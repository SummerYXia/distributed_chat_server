# Upenn CIS505 HW3
Multicast servers/clients that distribute chat messages to different group  
The server supports unordered, FIFO and total orderings  

## Syntax
./chatclient [Server's IP:port]  
./chatserver [-o order] [-v] [config file] [index]  
-o followed by ordering type, -v is debug flag, config file is the file that store address of servers, index indicate which line in the file is this server

## Usage
### Client
/join [group]: join a chat room, this fails if already in a chat room  
/nick [name]: init name will be client's IP:port, use this to update name  
/part: leave current chat room  
/quit: quit and close client  
Input that doesn't start with "/" is considered as a message that broadcast to all clients in the same chat room  

## Test
### Use proxy to simulate message delay in localhost
./proxy [-v] [-d max delay microseconds] [-l lossProbability] [config file]  
If using proxy, config file should have 2 addresses for every server: forward and bind. Otherwise servers have 1 address.
### Use stresstest to verify capacity and ordering
./stresstest [-d delay] [-o order] [-c #clients] [-m #messages] [-g #group] [config file]  
Keep all servers listed in config file running before stresstest

## TODO: add causal ordering and message losses handling
