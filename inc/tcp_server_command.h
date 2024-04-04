#ifndef TCP_SERVER_COMMAND_H_
#define TCP_SERVER_COMMAND_H_

#include <cstdint>

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "tcp_server.h"
#include "debug.h"
#include "xy_axes.h"
#include "z_axis.h"
#include "rema.h"

class tcp_server_command : public tcp_server{
public:
    tcp_server_command(const char *name, int port, bresenham  &x_y, bresenham  &z_dummy, encoders_pico &encoders) :
        tcp_server(name, port, x_y, z_dummy, encoders) {}

    void stop_all() {
        x_y.stop();
        z_dummy.stop();
        lDebug(Warn, "Stopping all");
    }

    void reply_fn(int sock) override {
        int len;
        char rx_buffer[1024];

        do {
            len = lwip_recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            if (len < 0) {
                stop_all();
                lDebug(Error, "Error occurred during receiving command: errno %d", errno);
            } else if (len == 0) {
                stop_all();
                lDebug(Warn, "Command connection closed");
            } else {
                //rema::update_watchdog_timer();
                rx_buffer[len] = 0;  // Null-terminate whatever is received and treat it like a string
                lDebug(InfoLocal, "Command received %s", rx_buffer);

                char *tx_buffer;

                int ack_len = json_wp(rx_buffer, &tx_buffer);

                // lDebug(InfoLocal, "To send %d bytes: %s", ack_len, tx_buffer);

                if (ack_len > 0) {
                    // send() can return less bytes than supplied length.
                    // Walk-around for robust implementation.
                    int to_write = ack_len;

                    while (to_write > 0) {
                        int written = lwip_send(sock, tx_buffer + (ack_len - to_write),
                                to_write, 0);
                        if (written < 0) {
                            stop_all();
                            lDebug(Error, "Error occurred during sending command: errno %d",
                                    errno);
                            goto free_buffer;
                        }
                        to_write -= written;
                    }

    free_buffer:
                    if (tx_buffer) {
                        delete[] tx_buffer;
                        tx_buffer = NULL;
                    }
                }
            }
        } while (len > 0);
    }


JSON_Value* logs_cmd(JSON_Value const *pars);

JSON_Value* protocol_version_cmd(JSON_Value const *pars);

JSON_Value* control_enable_cmd(JSON_Value const *pars);

JSON_Value* stall_control_cmd(JSON_Value const *pars);

JSON_Value* set_coords_cmd(JSON_Value const *pars);

JSON_Value* kp_set_tunings_cmd(JSON_Value const *pars);

JSON_Value* axes_hard_stop_all_cmd(JSON_Value const *pars);

JSON_Value* axes_soft_stop_all_cmd(JSON_Value const *pars);

JSON_Value* network_settings_cmd(JSON_Value const *pars);

JSON_Value* mem_info_cmd(JSON_Value const *pars);
JSON_Value* temperature_info_cmd(JSON_Value const *pars);
JSON_Value* move_closed_loop_cmd(JSON_Value const *pars);
JSON_Value* move_free_run_cmd(JSON_Value const *pars); 
JSON_Value* move_incremental_cmd(JSON_Value const *pars);

JSON_Value* read_encoders_cmd(JSON_Value const *pars);

JSON_Value* read_limits_cmd(JSON_Value const *pars);

JSON_Value* cmd_execute(char const *cmd, JSON_Value const *pars); 

int json_wp(char *rx_buff, char **tx_buff);
    
};


#endif /* TCP_SERVER_COMMAND_H_ */
