#ifndef SORTER_DEMO_H_
#define SORTER_DEMO_H_

/*
 * these enums represent the devices attached to the digital inputs (DI)
 * and outputs (DO) set up in the virtual factory. 
 * The virtual factory and these definitions have to match
 */
enum {
    DO_EMITTER_01 = 0,
    DO_STOP_BLADE_01,     /* stop blade between conveyors */
    DO_STOP_BLADE_02,     /* stop blade between conveyors */
    DO_PUSHER_01,         /* box pusher */
    DO_PUSHER_02,         /* box pusher */
    DO_PUSHER_03,         /* box pusher */
    DO_WARNING_LIGHT_01,
    DO_EOL                /* End of list */
};

enum {
    DI_DIFFUSE_01 = 0,
    DI_DIFFUSE_02,
    DI_DIFFUSE_03,
    DI_DIFFUSE_04,
    DI_DIFFUSE_05,
    DI_DIFFUSE_06,
    DI_DIFFUSE_07,
    DI_RETROREF_NOT_01,
    DI_RETROREF_NOT_02,
    DI_PUSH_FR_LIMIT_01,  /* Front limit switches for the box pushers */
    DI_PUSH_BK_LIMIT_01,  /* Back limit switches for the box pushers */
    DI_PUSH_FR_LIMIT_02,  /* Front limit switches for the box pushers */
    DI_PUSH_BK_LIMIT_02,  /* Back limit switches for the box pushers */
    DI_PUSH_FR_LIMIT_03,  /* Front limit switches for the box pushers */
    DI_PUSH_BK_LIMIT_03,  /* Back limit switches for the box pushers */
    DI_RESET_BUTTON_01,
    DI_EOL
};

/* Define main states */
enum {
    STATE_NOT_READY = 0,
    STATE_READY,
    STATE_WAITING_FOR_BOX_EXIT_EMITTER,
    STATE_WAITING_FOR_BOX_AT_GATE_01,
    STATE_BOX_AT_GATE_01,
    STATE_WAITING_FOR_BOX_AT_GATE_02,
    STATE_BOX_AT_GATE_02,
    STATE_BOX_DETECTED,
    STATE_BOX_WEIGHING,
    STATE_BOX_WEIGHED,
    STATE_BOX_PRE_PUSH,
    STATE_BOX_AT_PUSHER,
    STATE_BOX_PUSHING,
    STATE_BOX_PUSHED,
    STATE_WAITING_FOR_BOX_AT_CHUTE,
    STATE_BOX_SORTED,
    STATE_ERROR,
    STATE_EOL
};

/* Define main states */
char *state_strings[] =
{
    "STATE_NOT_READY",
    "STATE_READY",
    "STATE_WAITING_FOR_BOX_EXIT_EMITTER",
    "STATE_WAITING_FOR_BOX_AT_GATE_01",
    "STATE_BOX_AT_GATE",
    "STATE_BOX1_DETECTED",
    "STATE_BOX1_SORTING",
    "STATE_BOX2_DETECTED",
    "STATE_BOX2_SORTING",
    "STATE_BOX3_DETECTED",
    "STATE_BOX3_SORTING",
    "STATE_WAITING_FOR_BOX_AT_CHUTE",
    "STATE_BOX_SORTED",
    "STATE_ERROR",
    "STATE_EOL"
};

/* box types */
enum {
    BOX_TYPE_UNKNOWN = 0,
    BOX_TYPE_01,
    BOX_TYPE_02,
    BOX_TYPE_03,
    BOX_EOL
};

#endif /* SORTER_DEMO_H_ */
