#include <stdlib.h>
#include <stdint.h>
#include <chrono>

#include "x_axis.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "board.h"

#include "debug.h"
#include "tmr.h"
#include "gpio.h"

#define X_AXIS_TASK_PRIORITY ( configMAX_PRIORITIES - 1 )
#define X_AXIS_SUPERVISOR_TASK_PRIORITY ( configMAX_PRIORITIES - 3)

QueueHandle_t x_axis_queue = NULL;

tmr x_axis_tmr = tmr(LPC_TIMER1, RGU_TIMER1_RST, CLK_MX_TIMER1, TIMER1_IRQn);
mot_pap x_axis("x_axis", x_axis_tmr);

/**
 * @brief   handles the X axis movement.
 * @param   par     : unused
 * @returns never
 * @note    Receives commands from x_axis_queue
 */
static void x_axis_task(void *par)
{
      while (true) {
        x_axis.task();
    }
}

/**
 * @brief   checks if stalled and if position reached in closed loop.
 * @param   par : unused
 * @returns never
 */
static void x_axis_supervisor_task(void *par)
{
    while (true) {
        xSemaphoreTake(x_axis.supervisor_semaphore, portMAX_DELAY);
        x_axis.supervise();
    }
}

/**
 * @brief 	creates the queues, semaphores and endless tasks to handle X axis movements.
 * @returns	nothing
 */
void x_axis_init() {
    x_axis.queue = xQueueCreate(5, sizeof(struct mot_pap_msg*));

      x_axis.type = mot_pap::TYPE_STOP;
      x_axis.inches_to_counts_factor = 1000;
      x_axis.half_pulses = 0;
      x_axis.pos_act = 0;

      x_axis.gpios.direction = gpio { 4, 5, SCU_MODE_FUNC0, 2, 6 };     //DOUT1 P4_5    PIN10   GPIO2[5]
      x_axis.gpios.step = gpio { 4, 8, SCU_MODE_FUNC4, 5, 12 };         //DOUT4 P4_8   PIN15   GPIO5[12]  X_AXIS_STEP

      x_axis.gpios.direction.init_output();
      x_axis.gpios.step.init_output();

      x_axis.kp = {100,                               //!< Kp
              kp::DIRECT,                             //!< Control type
              x_axis.step_time,                       //!< Update rate (ms)
              -100000,                                //!< Min output
              100000,                                 //!< Max output
              10000                                   //!< Absolute Min output
      };

      x_axis.supervisor_semaphore = xSemaphoreCreateBinary();

      if (x_axis.supervisor_semaphore != NULL) {
          // Create the 'handler' task, which is the task to which interrupt processing is deferred
          xTaskCreate(x_axis_supervisor_task, "X_AXIS supervisor",
          256,
          NULL, X_AXIS_SUPERVISOR_TASK_PRIORITY, NULL);
          lDebug(Info, "x_axis: supervisor task created");
      }

      xTaskCreate(x_axis_task, "X_AXIS", 256, NULL,
      X_AXIS_TASK_PRIORITY, NULL);

      lDebug(Info, "x_axis: task created");

}

/**
 * @brief   handle interrupt from 32-bit timer to generate pulses for the stepper motor drivers
 * @returns nothing
 * @note    calls the supervisor task every x number of generated steps
 */
void TIMER1_IRQHandler(void) {
    if (x_axis.tmr.match_pending()) {
        x_axis.isr();
    }
}
