//#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <cctype>
#include "debug.h"
#include "FreeRTOS.h"
    int len;


#include "mot_pap.h"
#include "net_commands.h"
#include "settings.h"
#include "rema.h"
#include "bresenham.h"
#include "z_axis.h"
#include "xy_axes.h"
#include "temperature_ds18b20.h"
#include "encoders_pico.h"
#include "tcp_server_command.h"

#define PROTOCOL_VERSION  	"JSON_1.0"

extern encoders_pico *encoders;

bresenham* tcp_server_command::get_axes(const char *axis) {

    switch (*axis) {
    case 'z':
    case 'Z':
        return z_dummy_axes;
        break;
    default:
        return x_y_axes;
        break;
    }
}

JSON_Value* tcp_server_command::logs_cmd(JSON_Value const *pars) {
    if (pars && json_value_get_type(pars) == JSONObject) {
        JSON_Object *pars_object = json_value_get_object(pars);
        double quantity = json_object_get_number(pars_object, "quantity");

        JSON_Value *root_value = json_value_init_object();
        JSON_Value *msg_array = json_value_init_array();

        int msgs_waiting = uxQueueMessagesWaiting(debug_queue);
        int extract = MIN(quantity, msgs_waiting);

        for (int x = 0; x < extract; x++) {
            char *dbg_msg = NULL;
            if (xQueueReceive(debug_queue, &dbg_msg,
                    (TickType_t) 0) == pdPASS) {
                json_array_append_string(json_value_get_array(msg_array),
                        dbg_msg);
                delete[] dbg_msg;
                dbg_msg = NULL;
            }
        }

        json_object_set_value(json_value_get_object(root_value), "DEBUG_MSGS",
                msg_array);
        return root_value;
    }
    return NULL;
}

JSON_Value* tcp_server_command::protocol_version_cmd(JSON_Value const *pars) {
    JSON_Value *root_value = json_value_init_object();
    json_object_set_string(json_value_get_object(root_value), "Version", PROTOCOL_VERSION);
    return root_value;
}

JSON_Value* tcp_server_command::control_enable_cmd(JSON_Value const *pars) {
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *pars_object = json_value_get_object(pars);

    if (json_object_has_value_of_type(pars_object, "enabled", JSONBoolean)) {
        bool enabled = json_object_get_boolean(pars_object, "enabled");
        rema::control_enabled_set(enabled);
    }
    json_object_set_boolean(json_value_get_object(root_value), "STATUS",
            rema::control_enabled_get());
    return root_value;
}

JSON_Value* tcp_server_command::stall_control_cmd(JSON_Value const *pars) {
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *pars_object = json_value_get_object(pars);
    if (json_object_has_value_of_type(pars_object, "enabled",
            JSONBoolean)) {
        bool enabled = json_object_get_boolean(pars_object,
                "enabled");
        rema::stall_control_set(enabled);
    }
    json_object_set_boolean(json_value_get_object(root_value), "STATUS",
            rema::stall_control_get());
    return root_value;
}

JSON_Value* tcp_server_command::set_coords_cmd(JSON_Value const *pars) {
    if (pars && json_value_get_type(pars) == JSONObject) {
        JSON_Object *pars_object = json_value_get_object(pars);
        if (json_object_has_value(pars_object, "position_x")) {
            double pos_x = json_object_get_number(pars_object, "position_x");
            x_y_axes->first_axis->set_position(pos_x);
        }

        if (json_object_has_value(pars_object, "position_y")) {
            double pos_y = json_object_get_number(pars_object, "position_y");
            x_y_axes->second_axis->set_position(pos_y);
        }

        if (json_object_has_value(pars_object, "position_z")) {
            double pos_z = json_object_get_number(pars_object, "position_z");
            z_dummy_axes->first_axis->set_position(pos_z);
        }
    }
    JSON_Value *root_value = json_value_init_object();
    json_object_set_boolean(json_value_get_object(root_value), "ACK", true);
    return root_value;
}

JSON_Value* tcp_server_command::kp_set_tunings_cmd(JSON_Value const *pars) {
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    JSON_Object *pars_object = json_value_get_object(pars);

    if (pars && json_value_get_type(pars) == JSONObject) {

        char const *axes = json_object_get_string(pars_object, "axes");
        double kp = json_object_get_number(pars_object, "kp");
        int update = static_cast<int>(json_object_get_number(pars_object, "update"));
        int min = static_cast<int>(json_object_get_number(pars_object, "min"));
        int max = static_cast<int>(json_object_get_number(pars_object, "max"));

        bresenham *axes_ = get_axes(axes);

        if (axes_ == nullptr) {
            json_object_set_boolean(root_object, "ACK", false);
            json_object_set_string(root_object, "ERROR",
                    "No axis specified");
        } else {
            axes_->step_time = std::chrono::milliseconds(update);
            axes_->kp.set_output_limits(min, max);
            axes_->kp.set_sample_period(axes_->step_time);
            axes_->kp.set_tunings(kp);
            lDebug(Debug, "KP Settings set");
            json_object_set_boolean(root_object, "ACK", true);
        }
    }
    return root_value;
}

JSON_Value* tcp_server_command::axes_hard_stop_all_cmd(JSON_Value const *pars) {
    x_y_axes->send({mot_pap::TYPE_HARD_STOP});
    z_dummy_axes->send({mot_pap::TYPE_HARD_STOP});
    JSON_Value *root_value = json_value_init_object();
    json_object_set_boolean(json_value_get_object(root_value), "ACK", true);
    return root_value;
}

JSON_Value* tcp_server_command::axes_soft_stop_all_cmd(JSON_Value const *pars) {
    x_y_axes->send({mot_pap::TYPE_SOFT_STOP});
    z_dummy_axes->send({mot_pap::TYPE_SOFT_STOP});
    JSON_Value *root_value = json_value_init_object();
    json_object_set_boolean(json_value_get_object(root_value), "ACK", true);
    return root_value;
}

JSON_Value* tcp_server_command::network_settings_cmd(JSON_Value const *pars) {
    if (pars && json_value_get_type(pars) == JSONObject) {
        JSON_Object *pars_object = json_value_get_object(pars);
        char const *ipaddr = json_object_get_string(pars_object, "ipaddr");
        char const *netmask = json_object_get_string(pars_object, "netmask");
        char const *gw = json_object_get_string(pars_object, "gw");
        uint16_t port = static_cast<uint16_t>(json_object_get_number(pars_object, "port"));


        if (gw && ipaddr && netmask && port != 0) {
            lDebug(Info,
                    "Received network settings: ipaddr:%s, netmask:%s, gw:%s, port:%d",
                    ipaddr, netmask, gw, port);

            int octet1, octet2, octet3, octet4;
            unsigned char *ipaddr_bytes =
                    reinterpret_cast<unsigned char*>(&(settings::network.ipaddr.addr));

            if (sscanf(ipaddr, "%d.%d.%d.%d", &octet1, &octet2, &octet3, &octet4) == 4) {
                ipaddr_bytes[0] = static_cast<unsigned char>(octet1);
                ipaddr_bytes[1] = static_cast<unsigned char>(octet2);
                ipaddr_bytes[2] = static_cast<unsigned char>(octet3);
                ipaddr_bytes[3] = static_cast<unsigned char>(octet4);
            }

            unsigned char *netmask_bytes =
                    reinterpret_cast<unsigned char*>(&(settings::network.netmask.addr));
            if (sscanf(netmask, "%d.%d.%d.%d", &octet1, &octet2, &octet3, &octet4) == 4) {
                netmask_bytes[0] = static_cast<unsigned char>(octet1);
                netmask_bytes[1] = static_cast<unsigned char>(octet2);
                netmask_bytes[2] = static_cast<unsigned char>(octet3);
                netmask_bytes[3] = static_cast<unsigned char>(octet4);
            }

            unsigned char *gw_bytes =
                    reinterpret_cast<unsigned char*>(&(settings::network.gw.addr));
            if (sscanf(gw, "%d.%d.%d.%d", &octet1, &octet2, &octet3, &octet4) == 4) {
                gw_bytes[0] = static_cast<unsigned char>(octet1);
                gw_bytes[1] = static_cast<unsigned char>(octet2);
                gw_bytes[2] = static_cast<unsigned char>(octet3);
                gw_bytes[3] = static_cast<unsigned char>(octet4);
            }

            settings::network.port = port;

            settings::save();
            lDebug(Info, "Settings saved. Restarting...");

            Chip_UART_SendBlocking(DEBUG_UART, "\n\n", 2);

            Chip_RGU_TriggerReset(RGU_CORE_RST);
        }

        JSON_Value *root_value = json_value_init_object();
        json_object_set_boolean(json_value_get_object(root_value), "ACK", true);
        return root_value;
    }
    return NULL;
}

JSON_Value* tcp_server_command::mem_info_cmd(JSON_Value const *pars) {
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    json_object_set_number(root_object, "MEM_TOTAL", configTOTAL_HEAP_SIZE);
    json_object_set_number(root_object, "MEM_FREE", xPortGetFreeHeapSize());
    json_object_set_number(root_object, "MEM_MIN_FREE", xPortGetMinimumEverFreeHeapSize());
    return root_value;
}

JSON_Value* tcp_server_command::temperature_info_cmd(JSON_Value const *pars) {
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    json_object_set_number(root_object, "TEMP X", (static_cast<double>(temperature_ds18b20_get(0))) / 10);
    json_object_set_number(root_object, "TEMP Y", (static_cast<double>(temperature_ds18b20_get(1))) / 10);
    json_object_set_number(root_object, "TEMP Z", (static_cast<double>(temperature_ds18b20_get(2))) / 10);
    return root_value;
}

JSON_Value* tcp_server_command::move_closed_loop_cmd(JSON_Value const *pars) {
    if (pars && json_value_get_type(pars) == JSONObject) {
        JSON_Object *pars_object = json_value_get_object(pars);
        char const *axes = json_object_get_string(pars_object, "axes");
        bresenham *axes_ = get_axes(axes);

        double first_axis_setpoint = json_object_get_number(pars_object, "first_axis_setpoint");

        double second_axis_setpoint = json_object_get_number(pars_object, "second_axis_setpoint");

        bresenham_msg msg;
        msg.type = mot_pap::TYPE_BRESENHAM;
        msg.first_axis_setpoint = static_cast<int>(first_axis_setpoint
                * axes_->first_axis->inches_to_counts_factor);
        msg.second_axis_setpoint = static_cast<int>(second_axis_setpoint
                * axes_->second_axis->inches_to_counts_factor);

        axes_->send(msg);

        lDebug(Info, "AXIS_BRESENHAM SETPOINT X= %f, SETPOINT Y=%f",
                first_axis_setpoint, second_axis_setpoint);
    }
    JSON_Value *root_value = json_value_init_object();
    json_object_set_boolean(json_value_get_object(root_value), "ACK", true);
    return root_value;
}


JSON_Value* tcp_server_command::move_free_run_cmd(JSON_Value const *pars) {
    if (pars && json_value_get_type(pars) == JSONObject) {
        JSON_Object *pars_object = json_value_get_object(pars);
        char const *axes = json_object_get_string(pars_object, "axes");
        bresenham *axes_ = get_axes(axes);

        int first_axis_setpoint, second_axis_setpoint;
        if (json_object_has_value_of_type(pars_object,
                "first_axis_setpoint", JSONNumber)) {
            first_axis_setpoint = static_cast<int>(json_object_get_number(
                    pars_object, "first_axis_setpoint"));
        } else {
            first_axis_setpoint = axes_->first_axis->current_counts;
        }

        if (json_object_has_value_of_type(pars_object, "second_axis_setpoint", JSONNumber)) {
            second_axis_setpoint = static_cast<int>(json_object_get_number(
                    pars_object, "second_axis_setpoint"));
        } else {
            second_axis_setpoint = axes_->second_axis->current_counts;
        }

        bresenham_msg msg;
        msg.type = mot_pap::TYPE_BRESENHAM;
        msg.first_axis_setpoint = first_axis_setpoint;
        msg.second_axis_setpoint = second_axis_setpoint;
        axes_->send(msg);
        lDebug(Info, "AXIS_BRESENHAM SETPOINT X= %i, SETPOINT Y=%i",
                first_axis_setpoint, second_axis_setpoint);
    }
    JSON_Value *root_value = json_value_init_object();
    json_object_set_boolean(json_value_get_object(root_value), "ACK", true);
    return root_value;
}

JSON_Value* tcp_server_command::move_incremental_cmd(JSON_Value const *pars) {
    if (pars && json_value_get_type(pars) == JSONObject) {
        JSON_Object *pars_object = json_value_get_object(pars);
        char const *axes = json_object_get_string(pars_object, "axes");
        bresenham *axes_ = get_axes(axes);

        double first_axis_delta, second_axis_delta;
        if (json_object_has_value_of_type(pars_object,
                "first_axis_delta", JSONNumber)) {
            first_axis_delta = json_object_get_number(pars_object, "first_axis_delta");
        } else {
            first_axis_delta = 0;
        }

        if (json_object_has_value_of_type(pars_object, "second_axis_delta", JSONNumber)) {
            second_axis_delta = json_object_get_number(pars_object, "second_axis_delta");
        } else {
            second_axis_delta = 0;
        }

        bresenham_msg msg;
        msg.type = mot_pap::TYPE_BRESENHAM;
        msg.first_axis_setpoint = axes_->first_axis->current_counts + (first_axis_delta * axes_->first_axis->inches_to_counts_factor);
        msg.second_axis_setpoint = axes_->second_axis->current_counts + (second_axis_delta * axes_->first_axis->inches_to_counts_factor);
        axes_->send(msg);
        lDebug(Info, "AXIS_BRESENHAM SETPOINT X=%i, SETPOINT Y=%i",
                msg.first_axis_setpoint, msg.second_axis_setpoint);
    }
    JSON_Value *root_value = json_value_init_object();
    json_object_set_boolean(json_value_get_object(root_value), "ACK", true);
    return root_value;
}

JSON_Value* tcp_server_command::read_encoders_cmd(JSON_Value const *pars) {
    JSON_Value *root_value = json_value_init_object();

    JSON_Object *pars_object = json_value_get_object(pars);
    if (json_object_has_value_of_type(pars_object, "axis", JSONString)) {
        char const *axis = json_object_get_string(pars_object, "axis");
        json_object_set_number(json_value_get_object(root_value), axis, encoders->read_counter(axis[0]));
        return root_value;
    } else {

        uint8_t rx[4 * 3] = {0x00};
        encoders->read_counters(rx);
        int32_t x = (rx[0] << 24 | rx[1] << 16 | rx[2] << 8 | rx[3] << 0);
        int32_t y = (rx[4] << 24 | rx[5] << 16 | rx[6] << 8 | rx[7] << 0);
        int32_t z = (rx[8] << 24 | rx[9] << 16 | rx[10] << 8 | rx[11] << 0);
        int32_t w[[maybe_unused]] = (rx[12] << 24 | rx[13] << 16 | rx[14] << 8 | rx[15] << 0);

        json_object_set_number(json_value_get_object(root_value), "X", x);
        json_object_set_number(json_value_get_object(root_value), "Y", y);
        json_object_set_number(json_value_get_object(root_value), "Z", z);
        return root_value;
    }
}

JSON_Value* tcp_server_command::read_limits_cmd(JSON_Value const *pars) {
    JSON_Value *root_value = json_value_init_object();
    json_object_set_number(json_value_get_object(root_value), "ACK", encoders->read_limits().hard);
    return root_value;
}

// @formatter:off
const tcp_server_command::cmd_entry tcp_server_command::cmds_table[] = {
        {
                "PROTOCOL_VERSION",     /* Command name */
                &tcp_server_command::protocol_version_cmd,   /* Associated function */
        },
        {
                "CONTROL_ENABLE",
                &tcp_server_command::control_enable_cmd,
        },
        {
                "STALL_CONTROL",
                &tcp_server_command::stall_control_cmd,
        },
        {
                "AXES_HARD_STOP_ALL",
                &tcp_server_command::axes_hard_stop_all_cmd,
        },
        {
                "AXES_SOFT_STOP_ALL",
                &tcp_server_command::axes_soft_stop_all_cmd,
        },
        {
                "LOGS",
                &tcp_server_command::logs_cmd,
        },
        {
                "KP_SET_TUNINGS",
                &tcp_server_command::kp_set_tunings_cmd,
        },
        {
                "NETWORK_SETTINGS",
                &tcp_server_command::network_settings_cmd,
        },
        {
                "MEM_INFO",
                &tcp_server_command::mem_info_cmd,
        },
        {
                "TEMP_INFO",
                &tcp_server_command::temperature_info_cmd,
        },
        {
                "SET_COORDS",
                &tcp_server_command::set_coords_cmd,
        },
        {
                "MOVE_FREE_RUN",
                &tcp_server_command::move_free_run_cmd,
        },
        {
                "MOVE_CLOSED_LOOP",
                &tcp_server_command::move_closed_loop_cmd,
        },
        {
                "MOVE_INCREMENTAL",
                &tcp_server_command::move_incremental_cmd,
        },
        {
                "READ_ENCODERS",
                &tcp_server_command::read_encoders_cmd,
        },
        {
                "READ_LIMITS",
                &tcp_server_command::read_limits_cmd,
        },
};
// @formatter:on

/**
 * @brief 	searchs for a matching command name in cmds_table[], passing the parameters
 * 			as a JSON object for the called function to parse them.
 * @param 	*cmd 	:name of the command to execute
 * @param   *pars   :JSON object containing the passed parameters to the called function
 */
JSON_Value* tcp_server_command::cmd_execute(char const *cmd, JSON_Value const *pars) {
    bool cmd_found = false;
    for (unsigned int i = 0; i < (sizeof(cmds_table) / sizeof(cmds_table[0]));
            i++) {
        if (!strcmp(cmd, cmds_table[i].cmd_name)) {
            //return cmds_table[i].cmd_function(pars);
            return (this->*(cmds_table[i].cmd_function))(pars);
        }
    }
    if (!cmd_found) {
        lDebug(Error, "No matching command found");
    }
    return NULL;
}


/**
 * @brief Defines a simple wire protocol base on JavaScript Object Notation (JSON)
 * @details Receives an JSON object containing an array of commands under the
 * "commands" key. Every command entry in this array must be a JSON object, containing
 * a "command" string specifying the name of the called command, and a nested JSON object
 * with the parameters for the called command under the "pars" key.
 * The called function must extract the parameters from the passed JSON object.
 *
 * Example of the received JSON object:
 *
 * {
     "commands" : [
         {"command": "ARM_FREE_RUN",
             "pars": {
                 "dir": "CW",
                 "speed": 8
             }
         },
         {"command": "LOGS",
             "pars": {
                 "quantity": 10
             }
         }
     ]
   }

 * Every executed command has the chance of returning a JSON object that will be inserted
 * in the response JSON object under a key corresponding to the executed command name, or
 * NULL if no answer is expected.
 */

/**
 * @brief 	Parses the received JSON object looking for commands to execute and appends
 * 			the outputs of the called commands to the response buffer.
 * @param 	*rx_buff 	:pointer to the received buffer from the network
 * @param   **tx_buff	:pointer to pointer, will be set to the allocated return buffer
 * @returns	the length of the allocated response buffer
 */
int tcp_server_command::json_wp(char *rx_buff, char **tx_buff) {
    JSON_Value *rx_JSON_value = json_parse_string(rx_buff);
    JSON_Value *tx_JSON_value = json_value_init_object();
    *tx_buff = NULL;
    int buff_len = 0;

    if (!rx_JSON_value || (json_value_get_type(rx_JSON_value) != JSONObject)) {
        lDebug(Error, "Error json parse.");
    } else {
        JSON_Object *rx_JSON_object = json_value_get_object(rx_JSON_value);
        JSON_Array *commands = json_object_get_array(rx_JSON_object,
                "commands");

        if (commands != NULL) {
            for (unsigned int i = 0; i < json_array_get_count(commands); i++) {
                JSON_Object *command = json_array_get_object(commands, i);
                char const *command_name = json_object_get_string(command,
                        "command");
                lDebug(Info, "Command Found: %s", command_name);
                JSON_Value *pars = json_object_get_value(command, "pars");

                JSON_Value *ans = cmd_execute(command_name, pars);
                if (ans) {
                    json_object_set_value(json_value_get_object(tx_JSON_value),
                            command_name, ans);
                }
            }
        }

        buff_len = json_serialization_size(tx_JSON_value); /* returns 0 on fail */
        *tx_buff = new char [buff_len];
        if (!(*tx_buff)) {
            lDebug(Error, "Out Of Memory");
            buff_len = 0;
        } else {
            json_serialize_to_buffer(tx_JSON_value, *tx_buff, buff_len);
        }
    }
    json_value_free(rx_JSON_value);
    json_value_free(tx_JSON_value);
    return buff_len;
}
