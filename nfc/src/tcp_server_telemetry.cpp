#include <string.h>
#include <stdint.h>
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

#define KEEPALIVE_IDLE              (5)
#define KEEPALIVE_INTERVAL          (5)
#define KEEPALIVE_COUNT             (3)

static void send_telemetry(const int sock) {
    char tx_buffer[512];
    JSON_Value *ans = json_value_init_object();
    int times = 0;

    while (true) {
        json_object_set_number(json_value_get_object(ans), "POS X",
                x_axis.current_counts()
                        / static_cast<double>(x_axis.inches_to_counts_factor));
        json_object_set_number(json_value_get_object(ans), "POS Y",
                y_axis.current_counts()
                        / static_cast<double>(y_axis.inches_to_counts_factor));
        json_object_set_number(json_value_get_object(ans), "POS Z",
                z_axis.current_counts()
                        / static_cast<double>(z_axis.inches_to_counts_factor));

        JSON_Value *limits = json_value_init_object();
        json_object_set_boolean(json_value_get_object(limits), "left", false);
        json_object_set_boolean(json_value_get_object(limits), "right", true);
        json_object_set_boolean(json_value_get_object(limits), "up", false);
        json_object_set_boolean(json_value_get_object(limits), "down", true);
        json_object_set_boolean(json_value_get_object(limits), "in", true);
        json_object_set_boolean(json_value_get_object(limits), "out", false);

        json_object_set_value(json_value_get_object(ans), "LIMITS", limits);

        if (!(times % 50)) {
            JSON_Value *temperatures = json_value_init_object();
            json_object_set_number(json_value_get_object(temperatures), "TEMP X", (static_cast<double>(temperature_ds18b20_get(0))) / 10);
            json_object_set_number(json_value_get_object(temperatures), "TEMP Y", (static_cast<double>(temperature_ds18b20_get(1))) / 10);
            json_object_set_number(json_value_get_object(temperatures), "TEMP Z", (static_cast<double>(temperature_ds18b20_get(2))) / 10);
            json_object_set_value(json_value_get_object(ans), "TEMP_INFO", temperatures);
        } else {
            json_object_remove(json_value_get_object(ans), "TEMP_INFO");

        }
        times++;

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
                    lDebug(Error, "Error occurred during sending: errno %d",
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

static void tcp_telemetry_server_task(void *pvParameters) {
    uint16_t port = reinterpret_cast<uintptr_t>(pvParameters);

    char addr_str[128];
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_in dest_addr;

    struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in*) &dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(port);
    ip_protocol = IPPROTO_IP;

    int listen_sock = lwip_socket(AF_INET, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        lDebug(Error, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    lwip_setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    lDebug(Info, "Socket created");

    int err = lwip_bind(listen_sock, (struct sockaddr*) &dest_addr,
            sizeof(dest_addr));
    if (err != 0) {
        lDebug(Error, "Socket unable to bind: errno %d", errno);
        lDebug(Error, "IPPROTO: %d", AF_INET);
        goto CLEAN_UP;
    }
    lDebug(Info, "Socket bound, port %d", port);

    err = lwip_listen(listen_sock, 1);
    if (err != 0) {
        lDebug(Error, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {
        lDebug(Info, "Socket listening");

        struct sockaddr source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int sock = lwip_accept(listen_sock, (struct sockaddr*) &source_addr,
                &addr_len);
        if (sock < 0) {
            lDebug(Error, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Set tcp keepalive option
        lwip_setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive,
                sizeof(int));
        lwip_setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle,
                sizeof(int));
        lwip_setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval,
                sizeof(int));
        lwip_setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount,
                sizeof(int));
        // Convert ip address to string
        if (source_addr.sa_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in* )&source_addr)->sin_addr,
                    addr_str, sizeof(addr_str) - 1);
        }
        lDebug(Info, "Socket accepted ip address: %s", addr_str);

        send_telemetry(sock);

        lwip_shutdown(sock, 0);
        lwip_close(sock);
    }

    CLEAN_UP: lwip_close(listen_sock);
    vTaskDelete(NULL);
}

/*---------------------------------------------------------------------------*/
void stackIp_Telemetry_ThreadInit(uint16_t port) {
    sys_thread_new("tcp_telemetry_thread", tcp_telemetry_server_task,
            (void*) (uintptr_t) port,
            // DEFAULT_THREAD_STACKSIZE,
            1024,
            configMAX_PRIORITIES);
}
/*---------------------------------------------------------------------------*/
