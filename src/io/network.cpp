/* 
 * Triplane Classic - a side-scrolling dogfighting game.
 * Copyright (C) 1996,1997,2009  Dodekaedron Software Creations Oy
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * tjt@users.sourceforge.net
 */

/*******************************************************************************

   Purpose:
   	Networked game server

*******************************************************************************/

#include "util/wutil.h"
#include "io/network.h"
#include "io/video.h"
#include "gfx/bitmap.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#if !defined(_MSC_VER)
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#else
#include "network_win.h"
#endif
#include <SDL.h>
#include <SDL_endian.h>
#include <zlib.h>

// maximum number of simultaneous (accept()ed) connections
#define MAX_CONNECTIONS         20
// buffer sizes for sending
// uncompressed: needs to fit at least a maximum-sized packet
#define SENDU_BUFFER_SIZE       512*1024
// compressed: needs to at least fit all data of a single frame
// this is allocated for every connection
#define SENDC_BUFFER_SIZE       4*1024*1024
// receive buffer size, allocated for every connection
#define RECEIVE_BUFFER_SIZE     2048
// maximum possible packet length from client to host
#define MAXIMUM_C_PACKET_LENGTH 80
// max. number of lines of network information and chat to display on screen
#define NETINFO_LINES           15

static int network_host_active = 0, network_display_enabled = 1;
static int listen_socket = -1;
static char net_password[21];
static int game_mode = 0, last_pingid = 0;
static uint32_t last_ping_end = 0;

// values for clients[i].state
#define CS_UNUSED    0
#define CS_CONNECTED 1
#define CS_ACCEPTED  2

static struct clientdata {
    uint8_t state;              // values CS_*
    // fields below are invalid if state == CS_UNUSED
    int socket;
    struct sockaddr_in addr;
    uint32_t connecttime;
    char rbuffer[RECEIVE_BUFFER_SIZE];
    int rbufend;
    // fields below are invalid if state < CS_ACCEPTED
    // FIXME make cbuffer a ring buffer?
    char cbuffer[SENDC_BUFFER_SIZE];
    int cbufend;
    z_stream zs;
    char name[21];
    int last_pingid;
    // which country the client has sent WANTCONTROLS for, -1 if none
    int wanted_controls;
} clients[MAX_CONNECTIONS];

// which clientname is allowed to have the controls for each player if
// it wants to (empty string = host only)
static char controls_clientname[4][21];
// which clientid currently has the controls
static int controls_active_for_client[4];

static uint8_t net_controls[4];    // the current controls (in bits 0-5)

// uncompressed packets to be compressed and moved to client buffers
static char netsend_ubuffer[SENDU_BUFFER_SIZE];
static int netsend_ubufend = 0;
static int netsend_target = -1;

static char netinfo_texts[NETINFO_LINES][400];
static uint32_t netinfo_times[NETINFO_LINES];
static int netinfo_next = 0;
static Font *netinfo_font;

/*
 * Prints a message on screen in the host and possibly the clients
 * to_clients: 0 = to host only, 1 = to host and all clients
 * fmt, ...: printf()-style format string and arguments
 * Please don't include newlines in fmt (this prints exactly one
 * line).
 */
static void netinfo_printf(uint8_t to_clients, const char *fmt, ...) {
    char teksti[400];
    va_list arg;

    va_start(arg, fmt);
    vsprintf(teksti, fmt, arg);
    va_end(arg);

    printf("Net: %s\n", teksti);

    if (to_clients)
      netsend_infomsg(teksti);

    strcpy(netinfo_texts[netinfo_next], teksti);
    netinfo_times[netinfo_next] = SDL_GetTicks();
    netinfo_next = (netinfo_next + 1) % NETINFO_LINES;
}

void network_print_serverinfo(void) {
    int x = 12, y = 6, linesep = 10;
    int i, nclients = 0;
    uint32_t oldesttime;
    int display_was_enabled;

    if (!network_host_active)
        return;

    // don't show this info in the client windows
    display_was_enabled = network_display_enable(0);

    for (i = 0; i < MAX_CONNECTIONS; i++)
        if (clients[i].state >= CS_ACCEPTED)
            nclients++;

    netinfo_font->printf(x, y, "Server active (%d client%s)",
                         nclients, nclients==1 ? "" : "s");
    y += linesep;

    // don't display info older than 10 seconds
    oldesttime = SDL_GetTicks() - 10000;

    i = (netinfo_next + 1) % NETINFO_LINES;
    do {
        if (netinfo_times[i] != 0 && netinfo_times[i] > oldesttime) {
            netinfo_font->printf(x, y, "%s", netinfo_texts[i]);
            y += linesep;
        }
        i = (i + 1) % NETINFO_LINES;
    } while (i != (netinfo_next + 1) % NETINFO_LINES);

    network_display_enable(display_was_enabled);
}

static const char *inetaddr_str(const struct sockaddr_in *addr) {
    static char buf[100];
    if (addr->sin_family != AF_INET)
        return "?unknown address family?";
    sprintf(buf, "%s:%d",
            inet_ntoa(addr->sin_addr),
            ntohs(addr->sin_port));
    return buf;
}

static const char *clientaddr_str(int clientid) {
    return inetaddr_str(&clients[clientid].addr);
}

/* Return id of (accepted) client having name, or -1 if none */
static int clientid_of_name(const char *clientname) {
    int i;

    for (i = 0; i < MAX_CONNECTIONS; i++) {
        if (clients[i].state < CS_ACCEPTED)
            continue;
        if (strcmp(clients[i].name, clientname) == 0)
            return i;
    }

    return -1;
}

static void compress_init(z_stream *zs) {
    int r;

    zs->next_in = Z_NULL;
    zs->avail_in = 0;
    zs->next_out = Z_NULL;
    zs->avail_out = 0;
    zs->zalloc = Z_NULL;
    zs->zfree = Z_NULL;
    zs->opaque = Z_NULL;

    r = deflateInit(zs, 1);
    assert(r == Z_OK);
}

static void compress_deinit(z_stream *zs) {
    deflateEnd(zs);
}

static void client_close(int clientid) {
    int i;

    netinfo_printf(0, "Closing connection to client #%d", clientid);

    close(clients[clientid].socket);
    if (clients[clientid].state >= CS_ACCEPTED)
        compress_deinit(&clients[clientid].zs);
    clients[clientid].state = CS_UNUSED;
    clients[clientid].socket = -1;

    for (i = 0; i < 4; i++) {
        if (controls_active_for_client[i] == clientid) {
            network_reallocate_controls();
            break;
        }
    }
}

/* does not remove compressed data from netsend_ubuffer */
static void do_compress(int clientid, int flushmode) {
    struct clientdata *client = &clients[clientid];
    int r;

    if (client->state < CS_ACCEPTED)
        return;

    client->zs.next_in = (Bytef *) netsend_ubuffer;
    client->zs.avail_in = netsend_ubufend;

    client->zs.next_out = (Bytef *)
        &client->cbuffer[client->cbufend];
    client->zs.avail_out = SENDC_BUFFER_SIZE - client->cbufend;

    r = deflate(&client->zs, flushmode);
    assert(r == Z_OK || r == Z_BUF_ERROR || r == Z_STREAM_END);

    if (client->zs.avail_out == 0) {
        // client cbuffer is full
        netinfo_printf(0, "Error: %s did not keep up with sent data",
                       clients[clientid].name);
        client_close(clientid);
        return;
    }

    client->cbufend = SENDC_BUFFER_SIZE - client->zs.avail_out;

    assert(client->zs.avail_in == 0); // all data was compressed

    if (flushmode == Z_FINISH) {
        assert(r == Z_STREAM_END);
        deflateReset(&client->zs);
    }
}

static void net_do_compress(int flushmode) {
    if (netsend_target == -1) {
        int i;
        for (i = 0; i < MAX_CONNECTIONS; i++)
            do_compress(i, flushmode);
    } else {
        do_compress(netsend_target, flushmode);
    }
    netsend_ubufend = 0;
}

static void write_to_net(const void *data, int bytes) {
    assert(bytes <= SENDU_BUFFER_SIZE);

    if (bytes > SENDU_BUFFER_SIZE - netsend_ubufend)
        net_do_compress(Z_NO_FLUSH);

    assert(bytes <= SENDU_BUFFER_SIZE - netsend_ubufend);
    memcpy(&netsend_ubuffer[netsend_ubufend], data, bytes);
    netsend_ubufend += bytes;
}

static void write_to_net_8(uint8_t value) {
    write_to_net(&value, sizeof(uint8_t));
}

static void write_to_net_s16(int16_t value) {
    int16_t be_value = SDL_SwapBE16(value);
    write_to_net(&be_value, sizeof(int16_t));
}

static void write_to_net_16(uint16_t value) {
    uint16_t be_value = SDL_SwapBE16(value);
    write_to_net(&be_value, sizeof(uint16_t));
}

static void write_to_net_32(uint32_t value) {
    uint32_t be_value = SDL_SwapBE32(value);
    write_to_net(&be_value, sizeof(uint32_t));
}

static void write_to_net_fixedstring(const char *s, int length) {
    char buf[300];
    assert(length <= 300);
    memset(buf, 0, length);
    strncpy(buf, s, length-1);
    write_to_net(buf, length);
}

static void write_to_net_hdr(uint32_t length, uint8_t type) {
    write_to_net_32(length);
    write_to_net_8(type);
}

// changes target of future netsend_* commands
// newtarget = clientid or -1 = all
// returns old target
static int netsend_change_target(int newtarget) {
    int oldtarget = netsend_target;

    if (newtarget == oldtarget)
        return oldtarget;

    assert(newtarget >= -1 && newtarget < MAX_CONNECTIONS);

    net_do_compress(Z_PARTIAL_FLUSH);
    assert(netsend_ubufend == 0);

    netsend_target = newtarget;

    return oldtarget;
}

void netsend_videomode(char mode) {
    if (!network_host_active || !network_display_enabled)
        return;

    write_to_net_hdr(6, NET_PKTTYPE_VIDEOMODE);
    write_to_net_8(mode);
}

// oper = 0: send as is
// oper = 1: reverse order and multiply by 4
// oper = 2: multiply by 4
void netsend_setpal_range(const char pal[][3],
                          int firstcolor, int n, int oper) {
    int i;

    if (!network_host_active || !network_display_enabled)
        return;

    write_to_net_hdr(8+n*3, NET_PKTTYPE_SETPAL_RANGE);
    write_to_net_8(firstcolor);
    write_to_net_16(n);

    if (oper == 1) {
        for (i = n - 1; i >= 0; i--) {
            write_to_net_8(4 * pal[i][0]);
            write_to_net_8(4 * pal[i][1]);
            write_to_net_8(4 * pal[i][2]);
        }
    } else if (oper == 2) {
        for (i = 0; i < n; i++) {
            write_to_net_8(4 * pal[i][0]);
            write_to_net_8(4 * pal[i][1]);
            write_to_net_8(4 * pal[i][2]);
        }
    } else {
        for (i = 0; i < n; i++) {
            write_to_net_8(pal[i][0]);
            write_to_net_8(pal[i][1]);
            write_to_net_8(pal[i][2]);
        }
    }
}

void netsend_setpal_range_black(int firstcolor, int n) {
    if (!network_host_active || !network_display_enabled)
        return;

    write_to_net_hdr(8, NET_PKTTYPE_SETPAL_RANGE_BLACK);
    write_to_net_8(firstcolor);
    write_to_net_16(n);
}

void netsend_endofframe(void) {
    if (!network_host_active || !network_display_enabled)
        return;

    write_to_net_hdr(5, NET_PKTTYPE_ENDOFFRAME);
    net_do_compress(Z_PARTIAL_FLUSH);
}

void netsend_ping(int pingid) {
    if (!network_host_active)
        return;

    write_to_net_hdr(6, NET_PKTTYPE_PING);
    write_to_net_8(pingid);
    net_do_compress(Z_PARTIAL_FLUSH);
}

void netsend_gamemode(char mode) {
    if (!network_host_active)
        return;

    write_to_net_hdr(6, NET_PKTTYPE_GAMEMODE);
    write_to_net_8(mode);
}

void netsend_infomsg(const char *msg) {
    if (!network_host_active)
        return;

    write_to_net_hdr(76, NET_PKTTYPE_INFOMSG);
    write_to_net_fixedstring(msg, 71);
}

void netsend_fillrect(int x, int y, int w, int h, int c) {
    if (!network_host_active || !network_display_enabled)
        return;

    write_to_net_hdr(14, NET_PKTTYPE_FILLRECT);
    write_to_net_16(x);
    write_to_net_16(y);
    write_to_net_16(w);
    write_to_net_16(h);
    write_to_net_8(c);
}

int netsend_bitmapdata(int bitmapid,
                       int width, int height, int hastransparency,
                       const unsigned char *image_data) {
    if (!network_host_active || !network_display_enabled)
        return 0;

    write_to_net_hdr(12+width*height, NET_PKTTYPE_BITMAPDATA);
    write_to_net_16(bitmapid);
    write_to_net_16(width);
    write_to_net_16(height);
    write_to_net_8(hastransparency ? 1 : 0);
    write_to_net(image_data, width*height);

    return 1;
}

void netsend_bitmapdel(int bitmapid) {
    if (!network_host_active || !network_display_enabled)
        return;

    write_to_net_hdr(7, NET_PKTTYPE_BITMAPDEL);
    write_to_net_16(bitmapid);
}

void netsend_bitmapblitfs(int bitmapid) {
    if (!network_host_active || !network_display_enabled)
        return;

    write_to_net_hdr(7, NET_PKTTYPE_BITMAPBLITFS);
    write_to_net_16(bitmapid);
}

void netsend_bitmapblit(int bitmapid, int xx, int yy) {
    if (!network_host_active || !network_display_enabled)
        return;

    write_to_net_hdr(11, NET_PKTTYPE_BITMAPBLIT);
    write_to_net_16(bitmapid);
    write_to_net_s16(xx);
    write_to_net_s16(yy);
}

void netsend_bitmapblitclipped(int bitmapid, int xx, int yy,
                               int rx, int ry, int rx2, int ry2) {
    if (!network_host_active || !network_display_enabled)
        return;

    write_to_net_hdr(19, NET_PKTTYPE_BITMAPBLITCLIPPED);
    write_to_net_16(bitmapid);
    write_to_net_s16(xx);
    write_to_net_s16(yy);
    write_to_net_16(rx);
    write_to_net_16(ry);
    write_to_net_16(rx2);
    write_to_net_16(ry2);
}

void netsend_blittobitmap(int source_bitmapid, int target_bitmapid,
                          int xx, int yy) {
    if (!network_host_active || !network_display_enabled)
        return;

    write_to_net_hdr(13, NET_PKTTYPE_BLITTOBITMAP);
    write_to_net_16(source_bitmapid);
    write_to_net_16(target_bitmapid);
    write_to_net_s16(xx);
    write_to_net_s16(yy);
}

void netsend_fade_out(int type) {
    if (!network_host_active || !network_display_enabled)
        return;

    write_to_net_hdr(6, NET_PKTTYPE_FADE_OUT);
    write_to_net_8(type);
}

void netsend_play_music(const char *modname) {
    if (!network_host_active)
        return;

    write_to_net_hdr(12, NET_PKTTYPE_PLAY_MUSIC);
    write_to_net_fixedstring(modname, 7);
}

void netsend_stop_music() {
    if (!network_host_active)
        return;

    write_to_net_hdr(5, NET_PKTTYPE_STOP_MUSIC);
}

void netsend_play_sample(const char *samplename, int leftvol,
                         int rightvol, int looping) {
    if (!network_host_active)
        return;

    write_to_net_hdr(15, NET_PKTTYPE_PLAY_SAMPLE);
    write_to_net_fixedstring(samplename, 7);
    write_to_net_8(leftvol);
    write_to_net_8(rightvol);
    write_to_net_8(looping ? 1 : 0);
}

void netsend_stop_all_samples(void) {
    if (!network_host_active)
        return;

    write_to_net_hdr(5, NET_PKTTYPE_STOP_ALL_SAMPLES);
}

/* sends any initial packets to a newly accepted client */
static void send_initial_data(int clientid) {
    int oldtarget = netsend_change_target(clientid);

    netsend_gamemode(game_mode);
    netsend_mode_and_curpal();
    all_bitmaps_resend_if_sent();
    /* FIXME send any currently playing sounds and music */

    netsend_change_target(oldtarget);
}


void network_activate_host(const char *listenaddr,
                           int port,
                           const char *password,
                           Font *info_font) {
    struct sockaddr_in sin;
    int i;

    strncpy(net_password, password, 20);
    net_password[20] = 0;

    network_host_active = 1;
    netsend_ubufend = 0;
    last_pingid = 0;
    game_mode = 0;
    netinfo_next = 0;
    netinfo_font = info_font;

    for (i = 0; i < MAX_CONNECTIONS; i++) {
        clients[i].state = CS_UNUSED;
        clients[i].socket = -1;
    }

    for (i = 0; i < NETINFO_LINES; i++) {
        netinfo_times[i] = 0;
    }

    for (i = 0; i < 4; i++) {
        /*
         * The default is to allow no controls for network players
         * (otherwise we might need to check config.player_type to see
         * which players exist).
         */
        controls_clientname[i][0] = '\0';
        controls_active_for_client[i] = -1;
        net_controls[i] = 0;
    }

    listen_socket = socket(PF_INET, SOCK_STREAM, 0);
    if (listen_socket < 0) {
        perror("socket");
        exit(1);
    }

    char val = 1;
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
    /* don't care if this failed */

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    if (listenaddr[0] != '\0')
#ifndef _MSC_VER
        inet_aton(listenaddr, &sin.sin_addr);
#else
        inet_pton(AF_INET, listenaddr, &(sin.sin_addr));
#endif
    sin.sin_port = htons(port);

    if (bind(listen_socket, (sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(listen_socket, 5) < 0) {
        perror("listen");
        exit(1);
    }

    netinfo_printf(0, "Server listening on TCP port %d", port);
}

static void net_recv_quit(int client) {
    netinfo_printf(0, "%s has disconnected", clients[client].name);
    client_close(client);
}

static void net_recv_pong(int client, uint8_t pingid) {
    clients[client].last_pingid = pingid;
}

static void net_recv_want_controls(int client, uint8_t playernum) {
    clients[client].wanted_controls = playernum;
    network_reallocate_controls();
}

static void net_recv_disable_controls(int client) {
    int i;

    clients[client].wanted_controls = -1;

    for (i = 0; i < 4; i++) {
        if (controls_active_for_client[i] == client) {
            network_reallocate_controls();
            break;
        }
    }
}

static void net_recv_set_controls(int client,
                                  uint8_t controls) {
    int i;

    if (game_mode != 1)
        return;                 /* and ignore the packet */

    for (i = 0; i < 4; i++) {
        if (controls_active_for_client[i] == client) {
            net_controls[i] = controls;
            break;
        }
    }
    /* else just ignore the packet */
}

/*
 * Possibly sets the controls for playernum from the network. This
 * function either leaves the arguments alone, or changes them to
 * contain values from the network.
 */
void network_controls_for_player(int playernum,
                                 int *down, int *up,
                                 int *power, int *roll,
                                 int *guns, int *bombs) {
    if (!network_host_active)
        return;

    if (controls_clientname[playernum][0] == '\0')
        return;              /* no network controls for this player */

    uint8_t controls = net_controls[playernum];
    *down =  (controls & 1)  ? 1 : 0;
    *up =    (controls & 2)  ? 1 : 0;
    *power = (controls & 4)  ? 1 : 0;
    *roll =  (controls & 8)  ? 1 : 0;
    *guns =  (controls & 16) ? 1 : 0;
    *bombs = (controls & 32) ? 1 : 0;
}

/* dispatcher to process an incoming packet from client */
static void net_receive_packet(int client,
                               uint32_t length, uint8_t type, void *data) {
    /*
     * Note that no endianness conversions are required, since all
     * data except the length is 8-bit.
     */
    if (clients[client].state == CS_CONNECTED) { /* has not greeted yet */
        if (type != NET_PKTTYPE_C_GREETING ||
            length != 48 ||
            ((uint8_t *)data)[0] != 1) {
            netinfo_printf(0, "Error: invalid first packet from client #%d", client);
            client_close(client);
            return;
        }

        char *c_password = &((char *)data)[22];
        c_password[20] = 0;
        if (strcmp(c_password, net_password) != 0) {
            netinfo_printf(0, "Error: invalid password from client #%d",
                           client);
            client_close(client);
            return;
        }

        char *c_playername = &((char *)data)[1];
        c_playername[20] = 0;
        if (!check_strict_string(c_playername, 21)) {
            netinfo_printf(0, "Error: invalid player name from client #%d",
                           client);
            client_close(client);
            return;
        }
        if (clientid_of_name(c_playername) != -1) {
            // c_playername is already in use!
            netinfo_printf(0, "Error: name of client #%d (%s) is already in use",
                           client, c_playername);
            client_close(client);
            return;
        }
        strcpy(clients[client].name, c_playername);

        netinfo_printf(0, "New client #%d is %s",
                       client, clients[client].name);

        clients[client].cbufend = 0;
        compress_init(&clients[client].zs);
        // Pretend the client has replied to a currently ongoing ping
        clients[client].last_pingid = last_pingid;
        clients[client].wanted_controls = -1;
        clients[client].state = CS_ACCEPTED;
        send_initial_data(client);
    } else if (type == NET_PKTTYPE_C_QUIT) {
        if (length != 5)
            return;
        net_recv_quit(client);
    } else if (type == NET_PKTTYPE_C_PONG) {
        if (length != 6)
            return;
        uint8_t pingid = ((uint8_t *)data)[0];
        net_recv_pong(client, pingid);
    } else if (type == NET_PKTTYPE_C_WANTCONTROLS) {
        if (length != 6)
            return;
        uint8_t playernum = ((uint8_t *)data)[0];
        if (playernum > 3)
            return;
        net_recv_want_controls(client, playernum);
    } else if (type == NET_PKTTYPE_C_DISABLECONTROLS) {
        if (length != 5)
            return;
        net_recv_disable_controls(client);
    } else if (type == NET_PKTTYPE_C_SETCONTROLS) {
        if (length != 6)
            return;
        uint8_t controls = *((uint8_t *)data);
        controls &= 0x3f;       /* only 6 lower bits are used */
        net_recv_set_controls(client, controls);
    } else return;              /* ignores unknown packet types */
}

/* process possible incoming data in clients[client].rbuffer */
static void net_maybe_receive(int client) {
    uint32_t length;
    uint8_t type;
    char *clientdata = clients[client].rbuffer;
    uint32_t left = clients[client].rbufend;

    while (left >= 5) {       /* 5 bytes is the minimum packet size */
        memcpy(&length, clientdata, 4);
        length = SDL_SwapBE32(length);
        if (length < 5 || length > MAXIMUM_C_PACKET_LENGTH) {
            netinfo_printf(0, "Error: invalid data from client #%d (packet length %u)",
                           client, length);
            client_close(client);
            return;
        }
        if (left < length)
            break;              /* not a full packet */

        memcpy(&type, &clientdata[4], 1);

        net_receive_packet(client, length, type, &clientdata[5]);
        if (clients[client].state == CS_UNUSED)
            return;

        clientdata += length;
        left -= length;
    }

    if (left == 0) {
        clients[client].rbufend = 0;
    } else {
        memmove(clients[client].rbuffer, clientdata, left);
        clients[client].rbufend = left;
    }
}

int network_display_enable(int enable) { // returns previous value
    int oldval = network_display_enabled;
    network_display_enabled = enable;
    return oldval;
}

/*
 * Finds the color that clientname wishes to control, if any.
 * Returns the color (0-3) or -1 if none.
 */
int network_find_preferred_color(const char *clientname) {
    int i = clientid_of_name(clientname);
    if (i == -1)
        return -1;
    else
        return clients[i].wanted_controls;
}

/*
 * Finds the next or previous client that wants to control something.
 * Finds a new clientname starting from clientname, and writes the
 * result back into the variable.
 *
 * A choice can be selected by giving the name to
 * network_set_allowed_controls. This function also cycles through the
 * special case of an empty client name.
 *
 * If previous = 1, finds the previous one, otherwise finds the next
 * one (in alphabetical order of client names). If the given
 * clientname is empty, finds the last or first one instead.
 *
 * clientname should have space for at least 21 bytes.
 */
void network_find_next_controls(int previous, char *clientname) {
    int i, clientid = -1;

    if (!network_host_active) {
        clientname[0] = '\0';
        return;
    }

    /*
     * Find a client that wants to control something and has the
     * next-smaller or next-larger clientname
     */
    for (clientid = -1, i = 0; i < MAX_CONNECTIONS; i++) {
        if (clients[i].state < CS_ACCEPTED)
            continue;
        if (clients[i].wanted_controls != -1 &&
            /* in the right direction from clientname */
            (clientname[0] == '\0' ||
             (previous ?
              strcmp(clients[i].name, clientname) < 0 :
              strcmp(clients[i].name, clientname) > 0)) &&
            (clientid == -1 || /* no existing candidate */
             /* closer to the existing candidate */
             (previous ?
              strcmp(clients[i].name, clients[clientid].name) > 0 :
              strcmp(clients[i].name, clients[clientid].name) < 0))) {
            /* i is a new candidate */
            clientid = i;
        }
    }

    if (clientid == -1) {       /* no next/previous client */
        clientname[0] = '\0';
        return;
    }

    strcpy(clientname, clients[clientid].name);
}

/*
 * Sets which client is allowed to control playernum (0-3).
 * clientname: client name to allow (NULL or "" = allow host only)
 *
 * Call network_reallocate_controls after this to have the current
 * situation reflect the change!
 */
void network_set_allowed_controls(int playernum, const char *clientname) {
    if (clientname == NULL || clientname[0] == '\0')
        controls_clientname[playernum][0] = '\0';
    else
        strcpy(controls_clientname[playernum], clientname);
}

/* Stores current values of allowed controls for playernum into *_ret */
void network_get_allowed_controls(int playernum, char *clientname_ret) {
    strcpy(clientname_ret, controls_clientname[playernum]);
}

/* Selects controlling players again using current "allowed" settings */
void network_reallocate_controls(void) {
    int i, shouldbe;

    if (!network_host_active)
        return;

    for (i = 0; i < 4; i++) {
        if (controls_clientname[i][0] == '\0')
            shouldbe = -1;
        else
            shouldbe = clientid_of_name(controls_clientname[i]);
        if (shouldbe != -1 && clients[shouldbe].wanted_controls == -1)
            shouldbe = -1;
        if (controls_active_for_client[i] != shouldbe) {
            controls_active_for_client[i] = shouldbe;
            net_controls[i] = 0;
            // FIXME notify the players of the change?
        }
    }
}

/* Returns a string describing who controls playernum */
const char *network_controlling_player_string(int playernum) {
    static char buf[100];

    if (!network_host_active)
        return "no network game";

    if (controls_clientname[playernum][0] == '\0') {
        return "local player";
    } else if (controls_active_for_client[playernum] == -1) {
        sprintf(buf, "remote player %s",
                controls_clientname[playernum]);
    } else {
        sprintf(buf, "remote player %s",
                clients[controls_active_for_client[playernum]].name);
    }
    return buf;
}

void network_change_game_mode(int newmode) {
    game_mode = newmode;

    if (!network_host_active)
        return;

    netsend_gamemode(newmode);
}

static void network_handle_timeouts() {
    static uint32_t last_periodic_ping = 0;
    int i;

    uint32_t time = SDL_GetTicks();

    if (time > last_periodic_ping + 2000) {
        for (i = 0; i < MAX_CONNECTIONS; i++) {
            /* Close old non-accepted connections */
            if (clients[i].state == CS_CONNECTED &&
                time > clients[i].connecttime + 15000) {
                netinfo_printf(0, "No data received from client #%d", i);
                client_close(i);
            }

            /* Close connections that haven't replied to the last 10 pings */
            if (clients[i].state >= CS_ACCEPTED &&
                (256 + last_pingid - clients[i].last_pingid) % 256 > 10) {
                netinfo_printf(0, "Ping timeout for %s",
                               clients[i].name);
                client_close(i);
            }
        }

        /* Send pings every 2 seconds */
        network_ping(0);
        last_periodic_ping = time;
    }
}

/* this is called periodically (once or twice every frame) */
void network_update(void) {
    fd_set readfds, writefds, exceptfds;
    struct timeval timeout;
    int i;
    int maxfd = listen_socket;
    int retryselect = 1, canretryselect = 1;

    if (!network_host_active)
        return;

    net_do_compress(Z_NO_FLUSH);

    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);

    if (listen_socket != -1)
        FD_SET(listen_socket, &readfds);

    for (i = 0; i < MAX_CONNECTIONS; i++) {
        if (clients[i].state > CS_UNUSED) {
            if (maxfd < clients[i].socket)
                maxfd = clients[i].socket;
            FD_SET(clients[i].socket, &readfds);
            FD_SET(clients[i].socket, &exceptfds);
            if (clients[i].state >= CS_ACCEPTED &&
                clients[i].cbufend > 0)
                FD_SET(clients[i].socket, &writefds);
        }
    }

    while (retryselect && canretryselect) {
        /*
         * FIXME It would be better to use the select timeout instead
         * of SDL_Delay in nopeuskontrolli() when network_host_active.
         */
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;

        if (select(maxfd + 1, &readfds, &writefds, &exceptfds, &timeout) < 0) {
            perror("select");
            exit(1);
        }

        retryselect = 0;

        if (FD_ISSET(listen_socket, &readfds)) {
            for (i = 0; i < MAX_CONNECTIONS; i++)
                if (clients[i].state == CS_UNUSED)
                    break;
            if (i == MAX_CONNECTIONS) {
                struct sockaddr_in saddr;
                socklen_t size = sizeof(struct sockaddr_in);
                int sock = accept(listen_socket,
                                  (struct sockaddr *)&saddr,
                                  &size);
                close(sock);
                netinfo_printf(0, "Error: too many connections, rejecting %s",
                               inetaddr_str(&saddr));
            } else {
                socklen_t size = sizeof(struct sockaddr_in);
                assert(clients[i].state == CS_UNUSED);
                clients[i].socket = accept(listen_socket,
                                           (struct sockaddr *)&clients[i].addr,
                                           &size);
                if (clients[i].socket < 0) {
                    netinfo_printf(0, "Error when accepting new client: %s",
                                   strerror(errno));
                    clients[i].state = CS_UNUSED;
                    clients[i].socket = -1;
                    /* but don't quit */
                } else {
                    clients[i].state = CS_CONNECTED;
                    clients[i].connecttime = SDL_GetTicks();
                    clients[i].rbufend = 0;
                    netinfo_printf(0, "New client #%d from %s",
                                   i, clientaddr_str(i));
                }
            }
        }

        for (i = 0; i < MAX_CONNECTIONS; i++) {
            if (clients[i].state > CS_UNUSED) {
                if (FD_ISSET(clients[i].socket, &exceptfds)) {
                    netinfo_printf(0, "Exception from client #%d", i);
                    client_close(i);
                    canretryselect = 0;
                    continue;
                }

                if (FD_ISSET(clients[i].socket, &readfds)) {
                    int r = read(clients[i].socket,
                                 &clients[i].rbuffer[clients[i].rbufend],
                                 RECEIVE_BUFFER_SIZE - clients[i].rbufend);
                    if (r == RECEIVE_BUFFER_SIZE - clients[i].rbufend)
                        retryselect = 1;
                    if (r < 0) {
                        netinfo_printf(0, "Read error from client #%d: %s",
                                       i, strerror(errno));
                        client_close(i);
                        canretryselect = 0;
                        continue;
                    } else if (r == 0) { /* EOF */
                        netinfo_printf(0, "EOF from client #%d", i);
                        client_close(i);
                        canretryselect = 0;
                        continue;
                    }
                    clients[i].rbufend += r;
                    net_maybe_receive(i);
                }

                if (FD_ISSET(clients[i].socket, &writefds) &&
                    clients[i].state >= CS_ACCEPTED &&
                    clients[i].cbufend > 0) {

                    int w = write(clients[i].socket,
                                  clients[i].cbuffer,
                                  clients[i].cbufend);
                    if (w < 0) {
                        netinfo_printf(0, "Write error to client #%d: %s",
                                       i, strerror(errno));
                        client_close(i);
                        canretryselect = 0;
                        continue;
                    } else if (w < clients[i].cbufend) {
                        retryselect = 1;
                        if (w > 0) {
                            memmove(clients[i].cbuffer,
                                    clients[i].cbuffer + w,
                                    clients[i].cbufend - w);
                            clients[i].cbufend -= w;
                        }
                    } else {    /* all sent */
                        clients[i].cbufend = 0;
                    }
                }
            }
        }
    }

    network_handle_timeouts();
}

/* prepare to quit */
void network_quit(void) {
    int i;

    if (!network_host_active)
        return;

    netinfo_printf(1, "Preparing to quit, closing all connections");

    write_to_net_hdr(5, NET_PKTTYPE_QUIT);
    network_update();

    if (listen_socket != -1) {
        close(listen_socket);
        listen_socket = -1;
    }

    for (i = 0; i < MAX_CONNECTIONS; i++)
        if (clients[i].state > CS_UNUSED)
            client_close(i);

    network_host_active = 0;
}

int network_is_active(void) {
    return network_host_active;
}

/*
 * Pings all current clients. If seconds > 0, after this
 * network_last_ping_done() will return 0 until either all clients
 * have replied or the given number of seconds has passed.
 */
void network_ping(int seconds) {
    if (!network_host_active)
        return;

    last_pingid++;
    if (last_pingid > 255)
        last_pingid = 0;

    netsend_ping(last_pingid);

    if (seconds > 0)
        last_ping_end = SDL_GetTicks() + 1000 * seconds;
}

/*
 * Has everyone replied to the latest ping?
 * Returns: 0 if a client has not replied to the latest ping and there
 * is still time to wait; 1 if all clients have replied and there is
 * still time to wait; 2 if the waiting time has run out.
 */
int network_last_ping_done(void) {
    int i;

    if (!network_host_active)
        return 1;               // safe answer for how this is used

    if (last_ping_end != 0 && SDL_GetTicks() > last_ping_end) {
        last_ping_end = 0;
        return 2;
    }

    for (i = 0; i < MAX_CONNECTIONS; i++)
        if (clients[i].state >= CS_ACCEPTED &&
            clients[i].last_pingid != last_pingid)
            break;              /* no reply yet */

    if (i == MAX_CONNECTIONS)   /* all have replied */
        return 1;

    if (last_ping_end == 0)
        return 2;

    return 0;
}
