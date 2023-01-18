Programming Assignment 4
By: Jess Hamlin
For: CSCI4273

Purpose:
    The purpose of this project is to create a distributed file system.
    Files will be stored on multiple servers by a client, in such a way so if some servers 
        are disabled the files can still be accessed. 

Usage: 
    To start a client: 
        ./dfc <Configuration File>
        Example: ./dfc dfc.conf
    To start a server:
        ./dfs <Folder To Use> <Port To Use>
        Example: ./dfs /DFS1 10001
    
    The file directory ended up looking like this:
    ./
    ├─ DFC/
    │  ├─ dfc.c
    │  ├─ Images and files
    ├─ dfs.c
    ├─ DFS Folders/
    ├─ makefile

    Note how dfs.c is one directory higher than dfc.c.
    When using the dfc, file paths should be entered relative to the current directory,
        not the directory the dfc is in. 

Code Discussion:
    This program is in all about 1200 lines of code written over two days. 
    The instructions were somewhat vague as to the file structure, so I decided to put the makefile and dfs.c 
    on the top level, with separate folders for clients and server directories. This made sense because every server
    used the same server.c file. 

    - Both clients and servers use unique configuration files that implement a login system. These are not read
    using regex, just using the first letters to tell lines apart, so it is not too robust.

    - Get, List, and Put were all implemented. These work with any file extension and run in a seamless loop. 
    They all require the correct username and password from a client's configuration file.

    - Redundancy works, if one server goes off line files can still be accessed. Files are split and placed
    among four servers in the fashion described in the instructions. 

    - A client will try for 1 second to reconnect with a server if it fails. A server can fail in the middle of 
    a session without much impact. This was done with simply sleeping for 1 second and retrying if a connection fails, 
    and using setsockopt to add timeout value for read and write operations.

    NOTE: There is an issue where packets are sometimes dropped by the connection. Implementing acknowledgements and resending
    packets seemed out of scope for this project, so if a packet is dropped, the server that sent it will be considered offline.
    Once a server is offline, it will stay that way until the client is restarted. If there are issues with packets being dropped, 
    restart the client and try again.
        
Extra Credit:
    Data encryption:
        Files are encrypted when sent to a server using a simple xor operation based on a users password. When filed are received, 
        they are decrypted in the same way. In both encryption and decryption, each byte of the file is XORed with the ascii 
        values of the password summed up, and bound between 0 and 256 with a modulo. This is not the best encryption, but it is functional.
    Traffic optimization:
        When searching for the pieces of a file, a client will first send each server a list of pieces it needs, from the lowest 
        ID server to highest. A server will respond with which of the pieces a client needs it has, and the client will update 
        its needs before bothering other servers. The client builds an array mapping needed pieces to a server that has that piece.
        Then only 2-3 servers are contacted for their pieces of the file, and only one overall copy of the file is transmitted instead
        of 2. 
    Implement subfolder on DFS:
        Make a subfolder with mkdir <dir without a slash after>
        Access it after a get or put request with:
            get <filename> <dir with slash after>
            put <filename> <dir with slash after>
        This has not been tested much, but seems to work well enough.