#include <stdlib.h>
#include <stdint.h>
#include <chrono>

#include "y_axis.h"
#include "mot_pap.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "board.h"

#include "debug.h"
#include "tmr.h"
#include "gpio.h"

#define Y_AXIS_TASK_PRIORITY ( configMAX_PRIORITIES - 3 )
#define Y_AXIS_SUPERVISOR_TASK_PRIORITY ( configMAX_PRIORITIES - 1)

tmr y_axis_tmr = tmr(LPC_TIMER2, RGU_TIMER2_RST, CLK_MX_TIMER2, TIMER2_IRQn);
mot_pap y_axis("y_axis", y_axis_tmr);

/**
 * @brief 	creates the queues, semaphores and endless tasks to handle X axis movements.
 * @returns	nothing
 */
void y_axis_init() {
    y_axis.queue = xQueueCreate(5, sizeof(struct mot_pap_msg*));
    y_axis.motor_resolution = 25000;
    y_axis.encoder_resolution = 500;
    y_axis.inches_to_counts_factor = 1000;

    y_axis.gpios.step = gpio {4, 6, SCU_MODE_FUNC0, 2, 5}.init_output();        //DOUT2 P4_6    PIN11   GPIO2[6]
    y_axis.gpios.direction = gpio {4, 8, SCU_MODE_FUNC4, 5, 12}.init_output();  //DOUT4 P4_8    PIN15   GPIO5[12]

    y_axis.kp = {100,                               //!< Kp
            kp::DIRECT,                             //!< Control type
            y_axis.step_time,                       //!< Update rate (ms)
            -100000,                                //!< Min output
            100000,                                 //!< Max output
            10000                                   //!< Absolute Min output
    };

    y_axis.supervisor_semaphore = xSemaphoreCreateBinary();

    if (y_axis.supervisor_semaphore != NULL) {
        // Create the 'handler' task, which is the task to which interrupt processing is deferred
        xTaskCreate([](void *axis) { static_cast<mot_pap*>(axis)->supervise();}, "Y_AXIS supervisor",
        256,
        &y_axis, Y_AXIS_SUPERVISOR_TASK_PRIORITY, NULL);
        lDebug(Info, "y_axis: supervisor task created");
    }

    xTaskCreate([](void *axis) { static_cast<mot_pap*>(axis)->task();}, "Y_AXIS", 256, &y_axis,
    Y_AXIS_TASK_PRIORITY, NULL);

    lDebug(Info, "y_axis: task created");

}

/**
 * @brief   handle interrupt from 32-bit timer to generate pulses for the stepper motor drivers
 * @returns nothing
 * @note    calls the supervisor task every x number of generated steps
 */
extern "C" void TIMER2_IRQHandler(void) {
    if (y_axis.tmr.match_pending()) {
        y_axis.isr();
    }
}
