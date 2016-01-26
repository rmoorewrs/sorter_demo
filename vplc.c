/*
 * plc.c
 *
 *  Created on: Jan 21, 2016
 *      Author: rmoore
 *
 *      This implements some plc-like functions but is very incomplete.
 */



//
// 160121-initial code written, Rich Moore, richard.moore@windriver.com, link with libmodbus v3.0.x
//
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <memory.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <netinet/in.h>
#include <linux/tipc.h>
#include <modbus.h>
#include <vplc.h>



int plc_state_read(plc_t *plc, plc_state_t *state)
{
	if (plc && state) {
		memmove(state,plc->state,sizeof(plc_state_t));
		return(0);
	} else {
		return(-1);
	}
}

int plc_state_write(plc_t *plc, plc_state_t *new_state)
{
	uint8_t ndi,ndo,status;
	plc_state_t *plcstate,old_state;

	if (plc==0 || new_state==0 || plc->state==0){
		fprintf(stderr,"plc_state_write: NULL pointer\n");
		return(-1);
	}

	// point to plc_state for convenience
	plcstate = plc->state;

	// save the current state and number of inputs, outputs and status
	memmove(&old_state,plcstate,sizeof(plc_state_t));
	ndi=plcstate->nDI;
	ndo=plcstate->nDO;
	status=plcstate->status;


	// copy in the new state variable table and restore status and input count
	memmove(plcstate,new_state,sizeof(plc_state_t));
	plcstate->nDI=ndi;
	plcstate->nDO=ndo;
	plcstate->status=status;

	// mark status as out of synch, 'dirty' until written to interface
	plcstate->status=PLC_STATUS_DIRTY;

	return(0);
}


int plc_state_update(plc_t *plc)
{
	// Now we actually talk to the remote modbus slave/server device, write the outputs, read inputs and update the state variable table (SVT)
	//
	// First: write outputs and read them back via the modbus_write_bits() and modbus_read_bits() functions, these read/write to the
	// outputs only ("coils" in modbus terminlogy)
	//
	// Second: read inputs via the modbus_read_input_bits() function. this relates only  to the digital inputs.
	//
	// Note: if analog I/O is added that is a different set of registers and modbus functions
	//
	uint8_t ndi,ndo,status;
	int i,rc;
	uint8_t	ibuf[sizeof(plc_state_t)],obuf[sizeof(plc_state_t)];
	plc_state_t *plcstate, svt;

	if (plc==0 || plc->state==0){
			fprintf(stderr,"plc_state_update: NULL pointer\n");
			return(-1);
	}

	plcstate = plc->state;
	ndi=plcstate->nDI;
	ndo=plcstate->nDO;
	status=plcstate->status;


	
	// modbus functions use the buffer passed in for return values, so make a copy
	// to avoid corrupting the SVT
	memmove(obuf,plcstate->DO,MAX_OUTPUTS*sizeof(uint8_t));
	if  (plc->connection_type==PLC_CONNECTION_MODBUS) {
		rc = modbus_write_bits(plc->mb_slave, 0, ndo, obuf);
		if (rc != ndo) {
			printf("plc_state_write: ERROR writing to modbus (%d)", rc);
			printf("Address = %d, value = %.2x\n", 0,obuf[0]);
			return (-1);
		}
		
		// First: read back the value written to the outputs and if read is successful, copy back to SVT
		// FIXME: should probably compare the values written to the values read back
		rc = modbus_read_bits(plc->mb_slave, 0, ndo, obuf);
		if (rc != ndo){
			printf("plc_state_write: ERROR reading outputs from modbus (%d)", rc);
			printf("Address = %d, value = %.2x\n", 0,obuf[0]);
			return (-1);
		}
				
	} else if (plc->connection_type==PLC_CONNECTION_TIPC) {
		/* TODO -- this is where the client sends the SVT to the server, the server must 
		*	forward the SVT on to the modbus slave and send the updated SVT back
		*/ 
		// copy SVT to buffer for sending via TIPC
		memmove(&svt,plcstate, sizeof(plc_state_t));
		if (0 > sendto(plc->tipc_socket, &svt, sizeof(plc_state_t), 0, (struct sockaddr*)&(plc->tipc_server_addr), sizeof(plc->tipc_server_addr))) {
				perror("Client: failed to send");
				exit(1);
			}

			if (0 >= recv(plc->tipc_socket, obuf, sizeof(plc_state_t), 0)) {
				perror("Client: unexpected response");
				exit(1);
			}
		
		
	} else {
		fprintf(stderr,"plc_state_update: Invalid PLC_CONNECTION type\n");
		return (-1);
	}
	
	
	// copy outputs to SVT
	if (plc->verbose)
		printf("plc_state_write: output values read ");
	for(i=0; i < ndo;i++) {
		plcstate->DO[i]=obuf[i];
		if (plc->verbose)
			printf("[%.2x]",obuf[i]);
	}
	if (plc->verbose)
		printf("plc_state_write: SVT output values updated\n");
	
	// now read input from modbus and udate SVT
	if (plc->connection_type==PLC_CONNECTION_MODBUS) {
		
		rc = modbus_read_input_bits(plc->mb_slave,0,ndi,ibuf);
		if(rc != ndi) {
			printf("plc_state_write: ERROR reading inputs from modbus (%d)", rc);
			printf("Address = %d, value = %.2x\n", 0,ibuf[0]);
			return (-1);
		}
	} else if (plc->connection_type==PLC_CONNECTION_TIPC) {
		// we should've done both the modbus read and write at once in the server, so obuf contains 
		// the updated inputs as well, so be lazy and copy obuf to ibuf
		memmove(ibuf,obuf,sizeof(plc_state_t));
	} else {
		fprintf(stderr,"plc_state_update: Invalid PLC_CONNECTION type\n");
	}
	
	// copy inputs to SVT
	for (i=0; i < ndi; i++) {
		plcstate->DI[i]=ibuf[i];
	
	}
	// set status flag to clean because we just synched with remote successfully
	plcstate->status=PLC_STATUS_CLEAN;

	return (0);


}



void plc_shutdown(plc_t *plc)
{
        /* Close the connection */
        modbus_close(plc->mb_slave);
        modbus_free(plc->mb_slave);
        free(plc->state);
        free(plc);
 }

void plc_state_print(plc_state_t *state,int ni, int no)
{
	int i;
	printf("inputs:\t");
	for(i=0; i < ni; i++)
		printf("[%.2x]",state->DI[i]);
	printf("\n");

	printf("outputs:\t");
	for(i=0; no; i++)
		printf("[%.2x]",state->DO[i]);
	printf("\n");
}


plc_t *plc_init_modbus(char* net_addr, int net_port, int modbus_addr, int nDI, int nDO, int debug_flag)
{
	int i;
	plc_t *plc;

	// allocate and initialize plc device
	plc=malloc(sizeof(plc_t));
	plc->state=malloc(sizeof(plc_state_t)); // yes i know, if plc malloc fails, this is ugly
	if (plc==0 || plc->state==0){
		fprintf(stderr,"plc_init: error allocating memory");
		free(plc->state);
		free(plc);
		exit(-1);
	}
	// init state variable table, set all values to false, limit number of variables
	plc->state->nDI= nDI <= MAX_INPUTS ? nDI : MAX_INPUTS;
	plc->state->nDO= nDO <= MAX_OUTPUTS ? nDO : MAX_OUTPUTS;
	for (i=0; i < plc->state->nDI; i++)
		plc->state->DI[i]=FALSE;
	for (i=0; i < plc->state->nDO; i++)
			plc->state->DO[i]=FALSE;
	plc->state->status=PLC_STATUS_INIT;

	
	// connect to modbus tcp slave
	plc->mb_slave = modbus_new_tcp(net_addr, net_port); // connect to remote modbus tcp slave(server). we're modbus master(client)
	modbus_set_slave(plc->mb_slave, modbus_addr);	// set address of remote modbus slave id we want to talk to
	modbus_set_debug(plc->mb_slave,debug_flag);		// print detailed modbus debugging info or not
	plc->verbose=debug_flag;						// also set the local flag for quick reference

	if (modbus_connect(plc->mb_slave) == -1) {
		fprintf(stderr, "plc_init: Connection to modbus slave %d failed: %s\n",modbus_addr, modbus_strerror(errno));
		modbus_free(plc->mb_slave);
		exit(-1);
	}
	// set values in plc struct
	plc->connection_type=PLC_CONNECTION_MODBUS;
	strncpy(plc->net_addr,net_addr,17); // ip address string should be less than 16 bytes: aaa.bbb.ccc.ddd
	plc->net_port=net_port;
	plc->modbus_addr=modbus_addr; 

    return(plc);

}

plc_t *plc_init_tipc_client(uint32_t tipc_name_type, uint32_t tipc_name_instance, int tipc_wait, int nDI, int nDO, int debug_flag)
{
	int i;
	plc_t *plc;
	struct sockaddr_tipc server_addr;
	char buf[40] = {"start"};


	// allocate and initialize plc device
	plc=malloc(sizeof(plc_t));
	plc->state=malloc(sizeof(plc_state_t)); // yes i know, if plc malloc fails, this is ugly
	if (plc==0 || plc->state==0){
		fprintf(stderr,"plc_init: error allocating memory");
		free(plc->state);
		free(plc);
		exit(-1);
	}
	// init state variable table, set all values to false, limit number of variables
	plc->state->nDI= nDI <= MAX_INPUTS ? nDI : MAX_INPUTS;
	plc->state->nDO= nDO <= MAX_OUTPUTS ? nDO : MAX_OUTPUTS;
	for (i=0; i < plc->state->nDI; i++)
		plc->state->DI[i]=FALSE;
	for (i=0; i < plc->state->nDO; i++)
			plc->state->DO[i]=FALSE;
	plc->state->status=PLC_STATUS_INIT;
	
	// connect to modbus tcp slave
	// plc->mb_slave = modbus_new_tcp(net_addr, net_port); // connect to remote modbus tcp slave(server). we're modbus master(client)
	// modbus_set_slave(plc->mb_slave, bus_addr);	// set address of remote modbus slave id we want to talk to
	// modbus_set_debug(plc->mb_slave,debug_flag);		// print detailed modbus debugging info or not
	plc->verbose=debug_flag;						// also set the local flag for quick reference

	// set values in plc struct
	plc->connection_type=PLC_CONNECTION_TIPC;
	
	
	printf("Starting TIPC client\n");

	tipc_plc_wait_for_server(tipc_name_type, tipc_name_instance, 10000);

	plc->tipc_socket = socket(AF_TIPC, SOCK_RDM, 0); // reliable connectionless
	plc->tipc_name_type = tipc_name_type;
	plc->tipc_name_instance = tipc_name_instance;
	


	plc->tipc_server_addr.family = AF_TIPC;
	plc->tipc_server_addr.addrtype = TIPC_ADDR_NAME;
	plc->tipc_server_addr.addr.name.name.type = tipc_name_type;
	plc->tipc_server_addr.addr.name.name.instance = tipc_name_instance;
	plc->tipc_server_addr.addr.name.domain = 0;

	// send start message to server, wait for ack
	if (0 > sendto(plc->tipc_socket, buf, strlen(buf)+1, 0,
			   (struct sockaddr*)&(plc->tipc_server_addr), sizeof(plc->tipc_server_addr))) {
		perror("Client: failed to send");
		exit(1);
	}

	if (0 >= recv(plc->tipc_socket, buf, sizeof(buf), 0)) {
		perror("Client: unexpected response");
		exit(1);
	}

	printf("Client: received response: %s \n", buf);
	
    return(plc);
}

void tipc_plc_wait_for_server(uint32_t name_type, uint32_t name_instance, int wait)
{

	struct sockaddr_tipc topsrv;
	struct tipc_subscr subscr;
	struct tipc_event event;

	int sd = socket(AF_TIPC, SOCK_SEQPACKET, 0);

	memset(&topsrv, 0, sizeof(topsrv));
	topsrv.family = AF_TIPC;
	topsrv.addrtype = TIPC_ADDR_NAME;
	topsrv.addr.name.name.type = TIPC_TOP_SRV;
	topsrv.addr.name.name.instance = TIPC_TOP_SRV;

	/* Connect to topology server */

	if (0 > connect(sd, (struct sockaddr *)&topsrv, sizeof(topsrv))) {
		perror("Client: failed to connect to topology server");
		exit(1);
	}

	subscr.seq.type = htonl(name_type);
	subscr.seq.lower = htonl(name_instance);
	subscr.seq.upper = htonl(name_instance);
	subscr.timeout = htonl(wait);
	subscr.filter = htonl(TIPC_SUB_SERVICE);

	if (send(sd, &subscr, sizeof(subscr), 0) != sizeof(subscr)) {
		perror("Client: failed to send subscription");
		exit(1);
	}
	/* Now wait for the subscription to fire */

	if (recv(sd, &event, sizeof(event), 0) != sizeof(event)) {
		perror("Client: failed to receive event");
		exit(1);
	}
	if (event.event != htonl(TIPC_PUBLISHED)) {
		printf("Client: server {%u,%u} not published within %u [s]\n",
			   name_type, name_instance, wait/1000);
		exit(1);
	}

	close(sd);

}

// tipc - modbus gateway server
plc_t *plc_tipc_modbus_server(uint32_t tipc_name_type, uint32_t tipc_name_instance, char *net_address, int net_port, int modbus_addr, int nDI, int nDO, int debug_flag)
{
	int i,rc;
	plc_t *plc;
	struct sockaddr_tipc server_addr;
	struct sockaddr_tipc client_addr;
	uint8_t	ibuf[sizeof(plc_state_t)],obuf[sizeof(plc_state_t)];
	char inbuf[512];
	char outbuf[40] = "ack";
	plc_state_t *plcstate;
	int svt_size;
	socklen_t alen = sizeof(client_addr);

	// use the normal modbus init function to start with 
	plc=plc_init_modbus(net_address, net_port, modbus_addr, nDI, nDO, debug_flag);

	// set values in plc struct
	plc->connection_type=PLC_CONNECTION_TIPC_GATEWAY;
	plc->tipc_name_instance=tipc_name_instance;
	plc->tipc_name_type=tipc_name_type;
	
	// now set up the tipc server
	printf("Starting TIPC-Modbus gateway server\n");

	plc->tipc_server_addr.family = AF_TIPC;
	plc->tipc_server_addr.addrtype = TIPC_ADDR_NAMESEQ;
	plc->tipc_server_addr.addr.nameseq.type = tipc_name_type;
	plc->tipc_server_addr.addr.nameseq.lower = tipc_name_instance;
	plc->tipc_server_addr.addr.nameseq.upper = tipc_name_instance;
	plc->tipc_server_addr.scope = TIPC_ZONE_SCOPE;

	plc->tipc_socket = socket(AF_TIPC, SOCK_RDM, 0);

	if (0 != bind(plc->tipc_socket, (struct sockaddr *)&(plc->tipc_server_addr), sizeof(plc->tipc_server_addr))) {
		printf("Server: failed to bind port name\n");
		exit(1);
	}

	if (0 >= recvfrom(plc->tipc_socket, inbuf, sizeof(inbuf), 0,
			  (struct sockaddr *)&(plc->tipc_client_addr), &alen)) {
		perror("Server: unexpected message");
	}
	printf("Server: Message received: %s !\n", inbuf);

	if (0 > sendto(plc->tipc_socket, outbuf, strlen(outbuf)+1, 0,
			   (struct sockaddr *)&client_addr, sizeof(client_addr))) {
		perror("Server: failed to send");
	}
	
	// now enter into loop to relay state variables between tipc client and modbus slave
	svt_size=sizeof(plc_state_t);
	while (1) {
	
		// wait for outgoing SVT from client and relay it to modbus slave
		if (0 >= recvfrom(plc->tipc_socket, inbuf, sizeof(inbuf), 0,
					  (struct sockaddr *)&(plc->tipc_client_addr), &alen)) {
				perror("Server: unexpected message");
		}
		// plc_state_print(plc->state,plc->state->nDI,plc->state->nDO);
		// now do the modbus write and output read and input read
		plcstate=(plc_state_t *)inbuf;
		
		memmove(obuf,plcstate->DO,MAX_OUTPUTS*sizeof(uint8_t));
		rc = modbus_write_bits(plc->mb_slave, 0, nDO, obuf);
		if (rc != nDO) {
			printf("plc_tipc_modbus_server: ERROR writing to modbus (%d)", rc);
			printf("Address = %d, value = %.2x\n", 0,obuf[0]);
			return NULL;
		}
		
		// First: read back the value written to the outputs and if read is successful, copy back to SVT
		// FIXME: should probably compare the values written to the values read back
		rc = modbus_read_bits(plc->mb_slave, 0, nDO, obuf);
		if (rc != nDO){
			printf("plc_state_write: ERROR reading outputs from modbus (%d)", rc);
			printf("Address = %d, value = %.2x\n", 0,obuf[0]);
			return NULL;
		}
		
		rc = modbus_read_input_bits(plc->mb_slave,0,nDI,ibuf);
		if(rc != nDI) {
			printf("plc_state_write: ERROR reading inputs from modbus (%d)", rc);
			printf("Address = %d, value = %.2x\n", 0,ibuf[0]);
			return NULL;
		}
		
		
		// copy inputs to SVT
		for (i=0; i < nDI; i++) {
			plcstate->DI[i]=ibuf[i];
		}
		

		if (0 > sendto(plc->tipc_socket, plcstate, sizeof(plc_state_t), 0,
					   (struct sockaddr *)&client_addr, sizeof(client_addr))) {
				perror("Server: failed to send");
		}
			
		
	}
		
    return(plc);
}







