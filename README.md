# sorter_demo

This program acts as Programmable Logic Controller controlling a conveyor belt box-sorting system over a modbus/tcp network interface. The conveyor belt sorter is simulated by a commercial program called FactoryIO from http://www.realgames.pt, which receives modbus/tcp messages and simulates the behavior of the virtual factory.

sorter_demo can talk directly to the modbus tcp server (the Factorio IO program) or it can relay messages via a TIPC link to a TIPC-modbus gateway program. TIPC is used to implement failover between soft PLCs and a connectionless protocol makes this easier.

To make sorter_demo talk to modbus directly , run it with the 'm' option and the IP address of the Modbus TCP server:

./sorter_demo m 192.168.11.40

To reset the state of the factory via modbus, use the 'r' option

./sorter_demo r 192.168.11.40

To make sorter_demo relay messages via the TIPC gateway, with gateway running:

./sorter_demo t


