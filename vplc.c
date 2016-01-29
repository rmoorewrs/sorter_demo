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
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <netinet/in.h>
#include <linux/tipc.h>
#include <pthread.h>
#include <semaphore.h>
#include <modbus.h>
#include <vplc.h>

static void *hb_server (void *param);
static void *hb_client (void *param);
static void sighand (int signo);

static sem_t hb_mutex;
static int hb_received = 0;
static int hb_client_run = 0;

#define HB_LOOP_PERIOD_USEC      200000 /* 5 Hz */

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
	static int hb_timeout = 0;

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
	    if  (plc->mode == MODE_ACTIVE) {
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
    	   } /* if mode == MODE_ACTIVE */
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
	// set status flag to clean because we just synched with remote
        // successfully
	plcstate->status=PLC_STATUS_CLEAN;

	/* Check for heartbeats. */
	sem_wait(&hb_mutex);
	if (hb_received) {
		hb_timeout = 0;
		hb_received = 0;
		/*
		 * Our peer is alive, now that we're receiving heartbeats again
		 */
		if (!hb_client_run) {
	 		/* Switch us to STANDBY mode if our peer is ACTIVE. */
			if ((plc->mode == MODE_ACTIVE) &&
			    (plc->other_mode == MODE_ACTIVE)) {
				plc->mode = MODE_STANDBY;
				printf ("Switched to STANDBY mode\n");
			}

			/* Restart our heartbeat client */
			printf ("Restarting heartbeat client thread...\n");
			plc->conn = NULL;
			hb_client_run = 1;
			if (pthread_create (&plc->client_thread, NULL,
			    		    hb_client, (void *)plc)) {
				fprintf(stderr,
					"Error creating client thread\n");
				exit (-1);
			}
		}
	} else if (hb_client_run) {
		hb_timeout++;
	}
	sem_post(&hb_mutex);

	/* Only check for peer heartbeats if our client is alive. */
	if ((hb_client_run) && (hb_timeout > 6)) {
		hb_timeout = 0;

		/* Uh oh - looks like other instance failed. */
		printf ("Loss of peer heartbeat\n");

 		/* Switch us to ACTIVE mode if we're STANDBY. */
		if (plc->mode != MODE_ACTIVE) {
			plc->mode = MODE_ACTIVE;
			printf ("Switched to ACTIVE mode\n");
		}

		/* Kill our client thread (if running). */
		if (plc->client_thread)
			pthread_kill (plc->client_thread, SIGALRM);
	}

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

void plc_init_heartbeat (plc_t *plc)
{
	pthread_t server_thread;

	/* Create initial heartbeat client thread */
	if (!hb_client_run) {
		plc->conn = NULL;
		hb_client_run = 1;
		if (pthread_create (&plc->client_thread, NULL,
				    hb_client, (void *)plc)) {
			fprintf(stderr, "Error creating client thread\n");
			exit (-1);
		}
	}

	/* Create a pthread to receive heartbeat messages */
	if (pthread_create (&server_thread, NULL, hb_server, (void *)plc)) {
		fprintf(stderr, "Error creating server thread\n");
		exit (-1);
	}

	/* Wait here to see if we receive heartbeats from a peer */
	sleep (2);
	sem_wait (&hb_mutex);
	if (!hb_received) {
		/*
		 * No heartbeat from peer, we must be the first one up.
		 * Set mode to ACTIVE.
		 */
		plc->mode = MODE_ACTIVE;
		printf("Setting to MODE_ACTIVE\n");
	} else {
		plc->mode = MODE_STANDBY;
		printf("Setting to MODE_STANDBY\n");
	}
	sem_post (&hb_mutex);
}

plc_t *plc_init_modbus(char* net_addr, int instance, int net_port, int modbus_addr, int nDI, int nDO, int debug_flag)

{
	int i, rc;
	plc_t *plc;
	struct sigaction actions;

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

	// increase modbus response timeout
	printf("Setting modbus response timeout to %u sec, %ul usec\n",
		MODBUS_RESPONSE_TIMEOUT_SEC, 
		MODBUS_RESPONSE_TIMEOUT_USEC);
	plc->response_timeout.tv_sec=MODBUS_RESPONSE_TIMEOUT_SEC;
	plc->response_timeout.tv_usec=MODBUS_RESPONSE_TIMEOUT_USEC;
	modbus_set_response_timeout(plc->mb_slave,&plc->response_timeout);

	if (modbus_connect(plc->mb_slave) == -1) {
		fprintf(stderr, "plc_init: Connection to modbus slave %d failed: %s\n",modbus_addr, modbus_strerror(errno));
		modbus_free(plc->mb_slave);
		exit(-1);
	}

	/* create, initialize semaphore */
	if (sem_init (&hb_mutex, 1, 1) < 0) {
		perror("semaphore initilization");
		exit(-1);
	}

	/* setup sighandler for client thread */
	memset(&actions, 0, sizeof(actions));
	sigemptyset(&actions.sa_mask);
	actions.sa_flags = 0;
	actions.sa_handler = sighand;

	rc = sigaction (SIGALRM, &actions, NULL);
	if (rc < 0) {
		perror("signal initilization");
		exit(-1);
	}

	// set values in plc struct
	plc->connection_type=PLC_CONNECTION_MODBUS;
	strncpy(plc->net_addr,net_addr,17); // ip address string should be less than 16 bytes: aaa.bbb.ccc.ddd
	plc->net_port=net_port;
	plc->modbus_addr=modbus_addr; 
	plc->instance = instance;


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
	int instance = 0;

	// use the normal modbus init function to start with 
	plc=plc_init_modbus(net_address, instance, net_port, modbus_addr, nDI, nDO, debug_flag);

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

static void *hb_server (void *param) 
    {
    status_t status;
    mesg_async_chan_t *chan;
    mesg_async_info_t async_info;
    mesg_chan_id_t achid;
    string_t msg;
    hbmsg_t *hbmsg;
    char cvalue[10];
    plc_t *plc = (plc_t *)param;

    /* give each node a unique channel to listen on */
    sprintf (cvalue,"%d", 1000 + plc->instance);
        
    printf ("Starting heartbeat server thread. CHID = %s\n", cvalue);
    
    chan = SSAPI_mesg_AsyncChanCreate (cvalue, MESG_CHAN_SERVICE_GLOBAL, 1024);
    if (chan == NULL)
        {
        printf ("Server: Failed to create asynchronous channel \n");
        exit(1);
        }

    /* Get the async channel ID */
    achid = SSAPI_mesg_AsyncGetChanId (chan);
    
    while (1)
        {
        /* Wait for messages */
        msg = SSAPI_mesg_AsyncReceive (achid, &async_info);
     
        if (msg != NULL)
            {
            /* Got something! */
            hbmsg = MESG_GET_DATAPTR (msg); 
            sem_wait(&hb_mutex);
            hb_received = 1;
            plc->other_mode = hbmsg->mode;
            if (plc->mode == MODE_STANDBY) /* sync state with ACTIVE */
                plc->state->current_state = hbmsg->current_state;
            sem_post(&hb_mutex);
            //printf("HB: instance %d mode %d\n", hbmsg->instance, hbmsg->mode);

#if 0
            /* Verify only one instance is active */
            if ((hbmsg->mode == MODE_ACTIVE) &&
                (plc->mode == MODE_ACTIVE))
                {
                /*
                 * The other node reports that he's active,
                 * we're active too, so switch us to standby.
                 */
                printf ("We're currently ACTIVE but other instance %d reports "
                        "the same. Switching us to STANDBY ...\n",
                        hbmsg->instance);
                plc->mode == MODE_STANDBY;
                }

            /* ... and verify only one instance is standby */
            if ((hbmsg->mode == MODE_STANDBY) &&
                (plc->mode == MODE_STANDBY))
                {
                /*
                 * The other node reports that he's standby,
                 * we're standby too, so switch us to active.
                 */
                printf ("We're currently STANDBY but other instance %d reports "
                        "the same. Switching us to ACTIVE ...\n",
                        hbmsg->instance);
                plc->mode == MODE_ACTIVE;
                }
#endif

            /* It's our responsibility:to free the buffer */
            SSAPI_mesg_FreeBuffer (msg);
            }
        else 
            {
            /* Uh-oh! */
            printf ("Server [%d]: Failed to receive!\n Error = %d\n",
                    plc->instance, errno);
            }
        }
    pthread_exit (0);
    }

static void *hb_client (void *param) 
{
	plc_t *plc = (plc_t *)param;
	char cvalue[10];
	status_t status;
        hbmsg_t *hbmsg;

	/* Create async client connection to the other instance */
	printf ("Starting heartbeat client thread...\n");
	if (plc->instance == 1) {
		sprintf (cvalue,"1002");
	} else {
		sprintf (cvalue,"1001");
	}
	plc->conn = SSAPI_mesg_AsyncChanConnect (cvalue,
						 MESG_CHAN_SERVICE_GLOBAL,
						 1024);
	if (plc->conn == NULL) {
		fprintf (stderr, "No other sorter instance found - "
			 "disabling heartbeat\n");
		hb_client_run = 0;
		pthread_exit (0);
	}

	/* Loop */
	while (hb_client_run) {
		/* Let the other instance know we're alive (heartbeat) */
		hbmsg = SSAPI_mesg_AllocBuffer (sizeof (hbmsg_t)); 
        	if (hbmsg == NULL) {
			printf ("Failed to allocate buffer for sending\n");
			sleep(1);
			continue;
		}
		hbmsg->instance = plc->instance;
		hbmsg->mode = plc->mode;
		hbmsg->current_state = plc->state->current_state; 
		status = SSAPI_mesg_AsyncSend (plc->conn, hbmsg,
						   sizeof(hbmsg_t));
        	if (status < 0) {
			printf ("Failed to send hb message. Status = %d\n",
				 status);
			SSAPI_mesg_FreeBuffer(hbmsg);
		}

		usleep (HB_LOOP_PERIOD_USEC);
	}

	/*
	 * Our peer died. Clean up and exit this thread. When peer 
	 * comes back to life, we'll restart our client thread.
	 */
	printf ("Client thread exiting...\n");
	SSAPI_mesg_AsyncChanDisconnect (plc->conn);
	pthread_exit (0);
}

static void sighand (int signo)
{
	printf("SIGALRM received\n");
	hb_client_run = 0; 
}
