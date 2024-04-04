#include <cstdint>
#include "FreeRTOS.h"
#include "task.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "json_wp.h"
#include "tcp_server_telemetry.h"
#include "debug.h"
#include "xy_axes.h"
#include "z_axis.h"
#include "rema.h"
#include "temperature_ds18b20.h"

extern mot_pap x_axis, y_axis, z_axis;

void send_telemetry(const int sock) {
    char tx_buffer[512];
    JSON_Value *ans = json_value_init_object();
    JSON_Value *telemetry = json_value_init_object();

    int times = 0;

    while (true) {
        x_axis.read_pos_from_encoder();
        y_axis.read_pos_from_encoder();
        z_axis.read_pos_from_encoder();
        JSON_Value *coords = json_value_init_object();
        json_object_set_number(json_value_get_object(coords), "x",
                x_axis.current_counts()
                        / static_cast<double>(x_axis.inches_to_counts_factor));
        json_object_set_number(json_value_get_object(coords), "y",
                y_axis.current_counts()
                        / static_cast<double>(y_axis.inches_to_counts_factor));
        json_object_set_number(json_value_get_object(coords), "z",
                z_axis.current_counts()
                        / static_cast<double>(z_axis.inches_to_counts_factor));

        json_object_set_value(json_value_get_object(telemetry), "coords", coords);

        encoders_pico &encoders = encoders_pico::get_instance();
        struct limits limits = encoders.read_limits();

        JSON_Value *limits_json = json_value_init_object();
        json_object_set_boolean(json_value_get_object(limits_json), "left", (limits.hard & 1 << 0));
        json_object_set_boolean(json_value_get_object(limits_json), "right", (limits.hard & 1 << 1));
        json_object_set_boolean(json_value_get_object(limits_json), "up", (limits.hard & 1 << 2));
        json_object_set_boolean(json_value_get_object(limits_json), "down", (limits.hard & 1 << 3));
        json_object_set_boolean(json_value_get_object(limits_json), "in", (limits.hard & 1 << 4));
        json_object_set_boolean(json_value_get_object(limits_json), "out", (limits.hard & 1 << 5));
        json_object_set_boolean(json_value_get_object(limits_json), "probe", (limits.hard & 1 << 6));
        json_object_set_value(json_value_get_object(telemetry), "limits", limits_json);

        JSON_Value *stalled = json_value_init_object();
        json_object_set_boolean(json_value_get_object(stalled), "x", x_axis.stalled );
        json_object_set_boolean(json_value_get_object(stalled), "y", y_axis.stalled );
        json_object_set_boolean(json_value_get_object(stalled), "z", z_axis.stalled );
        json_object_set_value(json_value_get_object(telemetry), "stalled", stalled);

        bresenham &x_y_axes = x_y_axes_get_instance();
        bresenham &z_dummy_axes = z_dummy_axes_get_instance();

        JSON_Value *on_condition = json_value_init_object();
        json_object_set_boolean(json_value_get_object(on_condition), "x_y", (x_y_axes.already_there && !x_y_axes.was_soft_stopped));        // Soft stops are only sent by joystick, so no ON_CONDITION reported
        json_object_set_boolean(json_value_get_object(on_condition), "z", (z_dummy_axes.already_there && !z_dummy_axes.was_soft_stopped));
        json_object_set_value(json_value_get_object(telemetry), "on_condition", on_condition);

        if (!(times % 50)) {
            JSON_Value *temperatures = json_value_init_object();
            json_object_set_number(json_value_get_object(temperatures), "x", (static_cast<double>(temperature_ds18b20_get(0))) / 10);
            json_object_set_number(json_value_get_object(temperatures), "y", (static_cast<double>(temperature_ds18b20_get(1))) / 10);
            json_object_set_number(json_value_get_object(temperatures), "z", (static_cast<double>(temperature_ds18b20_get(2))) / 10);
            json_object_set_value(json_value_get_object(ans), "temps", temperatures);
        } else {
            json_object_remove(json_value_get_object(ans), "temps");

        }
        times++;

        json_object_set_value(json_value_get_object(ans), "telemetry", telemetry);

        int buff_len = json_serialization_size(ans); /* returns 0 on fail */
        json_serialize_to_buffer(ans, tx_buffer, buff_len);
        // lDebug(InfoLocal, "To send %d bytes: %s", buff_len, tx_buffer);

        if (buff_len > 0) {
            // send() can return less bytes than supplied length.
            // Walk-around for robust implementation.
            int to_write = buff_len;
            while (to_write > 0) {
                int written = lwip_send(sock, tx_buffer + (buff_len - to_write),
                        to_write, 0);
                if (written < 0) {
                    lDebug(Error, "Error occurred during sending telemetry: errno %d",
                            errno);
                    goto err_send;
                }
                to_write -= written;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
err_send:
    json_value_free(ans);
}
