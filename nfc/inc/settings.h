#ifndef SETTINGS_H_
#define SETTINGS_H_

#include "eeprom.h"
#include "lwip/ip_addr.h"

struct network_settings {
    ip_addr_t gw;
    ip_addr_t ipaddr;
    ip_addr_t netmask;
    uint16_t port;
};

class settings {
public:

    static network_settings network;
    /**
     * @brief   initializes EEPROM
     * @returns nothing
     */
    static void init() {
        EEPROM_init();
    }

    static void erase(void);

    static void defaults();

    static void save();

    static void read();
};

#endif /* SETTINGS_H_ */
