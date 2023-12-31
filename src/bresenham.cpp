#include "mot_pap.h"

#include <cstdint>
#include <cstdlib>

#include "board.h"
#include "task.h"

#include "debug.h"
#include "rema.h"
#include "bresenham.h"

using namespace std::chrono_literals;

extern mot_pap first_axis;
extern mot_pap second_axis;

void bresenham::task() {
    struct bresenham_msg *msg_rcv;

    while (true) {
        if (xQueueReceive(queue, &msg_rcv, portMAX_DELAY) == pdPASS) {
            lDebug(Info, "%s: command received", name);

            switch (msg_rcv->type) {
            case mot_pap::TYPE_BRESENHAM:
                was_soft_stopped = false;
                move(msg_rcv->first_axis_setpoint, msg_rcv->second_axis_setpoint);
                break;

            case mot_pap::TYPE_SOFT_STOP:
                if (is_moving) {
                    int x2 = kp.out_max;
                    int x1 = kp.out_min;
                    int y2 = 500;
                    int y1 = 1;
                    int x = current_freq;
                    int y = ((static_cast<float>(y2 - y1) / (x2 - x1)) * (x - x1)) + y1;

                    int counts = y;
                    lDebug(Info, "Soft stop in %i counts", counts);

                    int first_axis_setpoint = first_axis->current_counts();
                    int second_axis_setpoint = second_axis->current_counts();

                    if (first_axis->destination_counts() > first_axis->current_counts()) {
                        first_axis_setpoint += counts;
                    }
                    // DO NOT use "else". If destination_counts() == current_counts nothing must be done
                    if (first_axis->destination_counts() < first_axis->current_counts()) {
                        first_axis_setpoint -= counts;
                    }

                    if (second_axis->destination_counts() > second_axis->current_counts()) {
                        second_axis_setpoint += counts;
                    }
                    // DO NOT use "else". If destination_counts() == current_counts nothing must be done
                    if (second_axis->destination_counts() < second_axis->current_counts()) {
                        second_axis_setpoint -= counts;
                    }

                    was_soft_stopped = true;
                    move(first_axis_setpoint, second_axis_setpoint);

                    //first_axis->soft_stop(y);
                    //second_axis->soft_stop(y);
                }
                break;

            case mot_pap::TYPE_HARD_STOP:
            default:
                    stop();
                    break;
            }

            delete msg_rcv;
            msg_rcv = NULL;
        }
    }
}


void bresenham::calculate() {
    first_axis->delta = abs(first_axis->destination_counts() - first_axis->current_counts());
    second_axis->delta = abs(second_axis->destination_counts() - second_axis->current_counts());

    first_axis->set_direction();
    second_axis->set_direction();

    error =  first_axis->delta - second_axis->delta;

    if (first_axis->delta > second_axis->delta) {
        leader_axis = first_axis;
    } else {
        leader_axis = second_axis;
    }
}

void bresenham::move(int first_axis_setpoint, int second_axis_setpoint) {
    is_moving = true;
    already_there = false;
    first_axis->destination_counts() = first_axis_setpoint;
    second_axis->destination_counts() = second_axis_setpoint;
    lDebug(Info, "MOVE, %s: %i, %s: %i", first_axis->name, first_axis_setpoint, second_axis->name, second_axis_setpoint);

    calculate();

    if (first_axis->check_already_there() && second_axis->check_already_there()) {
        already_there = true;
        stop();
        lDebug(Info, "%s: already there", name);
    } else {
        rema::update_watchdog_timer();
        kp.restart();

        current_freq = kp.run(leader_axis->destination_counts(), leader_axis->current_counts());
        lDebug(Debug, "Control output = %i: ", current_freq);

        ticks_last_time = xTaskGetTickCount();
        tmr.change_freq(current_freq);
    }
}

void bresenham::step() {
    int e2 = error << 1;
    if (e2 >= -second_axis->delta) {
        error -= second_axis->delta;
        if (!first_axis->check_already_there()) {
            first_axis->step();
        }
    }
    if (e2 <= first_axis->delta) {
        error += first_axis->delta;
        if (!second_axis->check_already_there()) {
            second_axis->step();
        }
    }
}

/**
 * @brief   supervise motor movement for stall or position reached in closed loop
 * @returns nothing
 * @note    to be called by the deferred interrupt task handler
 */
void bresenham::supervise() {
    while (true) {
        if (xSemaphoreTake(supervisor_semaphore,
                portMAX_DELAY) == pdPASS) {
            if (already_there) {
                lDebug(Info, "%s: position reached", name);
                stop();
                goto end;
            }

            if (rema::stall_control_get()) {
                bool x_stalled = first_axis->check_for_stall();   // make sure both stall checks are executed;
                bool y_stalled = second_axis->check_for_stall();   // make sure both stall checks are executed;

                if (x_stalled || y_stalled) {
                    stop();
                    rema::control_enabled_set(false);
                    goto end;
                }
            }

//            if (rema::is_watchdog_expired()) {
//                stop();
//                lDebug(Info, "Watchdog expired");
//                goto end;
//            }

            calculate();                // recalculate to compensate for encoder errors
            first_axis->set_direction();     // if didn't stop for proximity to set point, avoid going to infinity
            second_axis->set_direction();     // keeps dancing around the setpoint...

            current_freq = kp.run(leader_axis->destination_counts(), leader_axis->current_counts());
            lDebug(Debug, "Control output = %i: ", current_freq);
            tmr.change_freq(current_freq);

        }
        end: ;
    }
}

/**
 * @brief   function called by the timer ISR to generate the output pulses
 */
void bresenham::isr() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    TickType_t ticks_now = xTaskGetTickCount();

    already_there = first_axis->check_already_there() && second_axis->check_already_there();
    if (already_there) {
        stop();
        xSemaphoreGiveFromISR(supervisor_semaphore, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        goto cont;
    }

    step();

    if ((ticks_now - ticks_last_time) > pdMS_TO_TICKS(step_time.count())) {
        ticks_last_time = ticks_now;
        xSemaphoreGiveFromISR(supervisor_semaphore, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

    cont: ;
}

/**
 * @brief   if there is a movement in process, stops it
 * @returns nothing
 */
void bresenham::stop() {
    is_moving = false;
    tmr.stop();
    current_freq = 0;
}

