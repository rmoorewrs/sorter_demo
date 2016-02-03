/*
 * vplc.h
 *
 *  Created on: Jan 21, 2016
 *      Author: rmoore
 */

#ifndef VPLC_H_
#define VPLC_H_

#include <modbus.h>
#include <netinet/in.h>
#include <linux/tipc.h>

/* IPC specific header files */
#include <common.h>
#include <mesg_service.h>
#include <mesg_common.h>

#define MAX_BUFSIZE                     64
#define MAX_INPUTS                      16
#define MAX_OUTPUTS                     16

#define MODBUS_RESPONSE_TIMEOUT_SEC	10
#define MODBUS_RESPONSE_TIMEOUT_USEC	0

enum {
    MODE_STANDBY = 0,
    MODE_ACTIVE
};

typedef struct heartbeat_msg
{
    int instance;
    int mode;
    int current_state;
} hbmsg_t;

enum {
    PLC_STATUS_INIT = 0,
    PLC_STATUS_CLEAN,
    PLC_STATUS_DIRTY,
    PLC_STATUS_ERROR
};

/* define different connection types */
enum {
    PLC_CONNECTION_NONE = 0,
    PLC_CONNECTION_MODBUS,
    PLC_CONNECTION_UDP,
    PLC_CONNECTION_TIPC,
    PLC_CONNECTION_TIPC_GATEWAY,
    PLC_CONNECTION_EOL
};

/* set up global state variable table */
typedef struct _plc_state {
    int     current_state;
    uint8_t status;
    uint8_t nDI;
    uint8_t nDO;
    uint8_t DI[MAX_INPUTS];
    uint8_t DO[MAX_OUTPUTS];
} plc_state_t;

typedef struct _plc {
    modbus_t *      mb_slave;
    char            net_addr[80];
    int             net_port;
    int             modbus_addr;
    int             connection_type;
    int             verbose;
    long            update_count;
    struct timeval  response_timeout;
    plc_state_t *   state;
    int             instance;
    int             mode;       /* mode of this node */
    int             other_mode; /* mode of other node */
    pthread_t       client_thread;

    /* TIPC Client specific */
    mesg_conn_t *   conn;
} plc_t;

plc_t * plc_init_modbus (char *, int, int, int, int, int, int);
plc_t * plc_init_tipc_client (uint32_t, uint32_t, int, int, int, int);
void    plc_state_print (plc_state_t *, int, int);
int     plc_state_read (plc_t *, plc_state_t *);
int     plc_state_write (plc_t *, plc_state_t *);
int     plc_state_update (plc_t *);
void    plc_shutdown (plc_t *);
int     plc_timer (plc_t *, int);
void    plc_init_heartbeat (plc_t *);

#endif /* VPLC_H_ */
