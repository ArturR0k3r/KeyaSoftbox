#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include "ksb_common.h"

/**
 * Initialize the state machine
 * @return 0 on success, negative error code on failure
 */
int state_machine_init(void);

/**
 * Start the state machine
 */
void state_machine_start(void);

#endif // STATE_MACHINE_H