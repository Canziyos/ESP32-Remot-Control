#include "commands.h"

/* Forward declarations */
void cmd_ping(const char*, struct cmd_ctx_t*);
void cmd_auth(const char*, struct cmd_ctx_t*);
void cmd_settoken(const char*, struct cmd_ctx_t*);
void cmd_diag(const char*, struct cmd_ctx_t*);
void cmd_led_on(const char*, struct cmd_ctx_t*);
void cmd_led_off(const char*, struct cmd_ctx_t*);
void cmd_version(const char*, struct cmd_ctx_t*);
void cmd_ota(const char*, struct cmd_ctx_t*);
void cmd_setwifi(const char*, struct cmd_ctx_t*);
void cmd_errsrc(const char*, struct cmd_ctx_t*);
void cmd_dht(const char*, struct cmd_ctx_t*);
void cmd_dhtstream(const char*, struct cmd_ctx_t*);
void cmd_dhtstate(const char*, struct cmd_ctx_t*);

#define CMD(name, auth, fn) { (name), sizeof(name)-1, (auth), (fn) }

const cmd_entry_t CMDS[] = {
    CMD("ping",     false, cmd_ping),
    CMD("auth",     false, cmd_auth),
    CMD("settoken", true,  cmd_settoken),
    CMD("diag",     true,  cmd_diag),
    CMD("led_on",   true,  cmd_led_on),
    CMD("led_off",  true,  cmd_led_off),
    CMD("version",  false, cmd_version),
    CMD("ota",      true,  cmd_ota),
    CMD("setwifi",  true,  cmd_setwifi),
    CMD("errsrc",   false, cmd_errsrc),
    CMD("dht?",      false, cmd_dht),         // print last sample.
    CMD("dhtstream", true,  cmd_dhtstream),   // requires auth.
    CMD("dhtstate",  false, cmd_dhtstate),    // query state.
};
const size_t CMD_COUNT = sizeof(CMDS)/sizeof(CMDS[0]);
