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
   	Client of networked game

*******************************************************************************/

#include "triplane.h"
#include "util/wutil.h"
#include "io/network.h"
#include "io/netclient.h"
#include "io/joystick.h"
#include "io/sdl_compat.h"
#include "io/video.h"
#include "gfx/bitmap.h"
#include "gfx/fades.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#if !defined(_MSC_VER)
#include <unistd.h>
#endif
#include <fcntl.h>
#include <sys/types.h>
#if !defined(_MSC_VER)
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#else
#include "network_win.h"
#endif
#include <SDL.h>
#include <SDL_endian.h>
#include <zlib.h>

#define NETC_SEND_BUFFER_SIZE 512
#define NETC_RECVC_BUFFER_SIZE 100*1024
#define NETC_RECVU_BUFFER_SIZE 1024*1024

// must be >= 12+2400*200 and <= NETC_RECVU_BUFFER_SIZE
#define MAXIMUM_S_PACKET_LENGTH 512*1024

#define NETCINFO_LINES 8

static int8_t netc_controls_country = -1;

static int netc_socket = -1;
static char netc_sendbuf[NETC_SEND_BUFFER_SIZE];
static unsigned int netc_sendbufend = 0;
static char netc_recvcbuf[NETC_RECVC_BUFFER_SIZE];
static unsigned int netc_recvcbufend = 0;
static char netc_recvubuf[NETC_RECVU_BUFFER_SIZE];
static unsigned int netc_recvubufend = 0;
static int netc_game_mode = 0;
static uint32_t netc_last_endofframe = 0, netc_last_warn = 0;

static z_stream netc_recvzs;

static Bitmap *netc_bitmaps[65536];

static char netcinfo_texts[NETCINFO_LINES][400];
static uint32_t netcinfo_times[NETCINFO_LINES];
static int netcinfo_next = 0;

static void netc_printf(const char *fmt, ...) {
    char teksti[400];
    va_list arg;

    va_start(arg, fmt);
    vsprintf(teksti, fmt, arg);
    va_end(arg);

    printf("Net: %s\n", teksti);

    strcpy(netcinfo_texts[netcinfo_next], teksti);
    netcinfo_times[netcinfo_next] = SDL_GetTicks();
    netcinfo_next = (netcinfo_next + 1) % NETCINFO_LINES;
}

static void netc_print_texts(void) {
    int x = 12, y = 6, linesep = 10;
    int i;
    uint32_t oldesttime;

    if (netc_game_mode == 0)
        foverlay->printf(x, y, "Viewing screen from host");
    y += linesep;

    // don't display info older than 10 seconds
    oldesttime = SDL_GetTicks() - 10000;

    i = (netcinfo_next + 1) % NETCINFO_LINES;
    do {
        if (netcinfo_times[i] != 0 && netcinfo_times[i] > oldesttime) {
            foverlay->printf(x, y, "%s", netcinfo_texts[i]);
            y += linesep;
        }
        i = (i + 1) % NETCINFO_LINES;
    } while (i != (netcinfo_next + 1) % NETCINFO_LINES);
}

static char *netc_printed_text(const char *header) {
    static char result[(1+NETCINFO_LINES)*400];
    int i;

    strcpy(result, header);

    i = (netcinfo_next + 1) % NETCINFO_LINES;
    do {
        if (netcinfo_texts[i][0] != '\0') {
            strcat(result, netcinfo_texts[i]);
            strcat(result, "\n");
        }
        i = (i + 1) % NETCINFO_LINES;
    } while (i != (netcinfo_next + 1) % NETCINFO_LINES);

    return result;
}

static void netc_clear_printed_text(void) {
    int i;
    for (i = 0; i < NETCINFO_LINES; i++)
        netcinfo_texts[i][0] = '\0';
    netcinfo_next = 0;
}

static void netcbitmap_init(void) {
    memset(netc_bitmaps, 0, 65536 * sizeof(Bitmap *));
}

static void netcbitmap_free_all(void) {
    int bid;
    for (bid = 0; bid < 65536; bid++)
        if (netc_bitmaps[bid] != NULL)
            delete netc_bitmaps[bid];
    memset(netc_bitmaps, 0, 65536 * sizeof(Bitmap *));
}

static void netcbitmap_maybefree(uint16_t bitmapid) {
    if (netc_bitmaps[bitmapid] != NULL) {
        delete netc_bitmaps[bitmapid];
        netc_bitmaps[bitmapid] = NULL;
    }
}

static void netc_close(void) {
    if (netc_socket >= 0) {
        close(netc_socket);
        netc_socket = -1;
    }
}

static int16_t read_from_net_s16(const char *data) {
    int16_t val;
    memcpy(&val, data, sizeof(int16_t));
    val = SDL_SwapBE16(val);
    return val;
}

static uint16_t read_from_net_16(const char *data) {
    uint16_t val;
    memcpy(&val, data, sizeof(uint16_t));
    val = SDL_SwapBE16(val);
    return val;
}

static void netc_send_packet(uint32_t length, uint8_t type, void *data) {
    if (NETC_SEND_BUFFER_SIZE - netc_sendbufend < length) {
        netc_printf("Error: send buffer full (connection stalled?)");
        netc_close();
        return;
    }

    uint32_t length_c = SDL_SwapBE32(length);
    memcpy(&netc_sendbuf[netc_sendbufend], &length_c, 4);
    memcpy(&netc_sendbuf[netc_sendbufend + 4], &type, 1);
    if (length > 5)
        memcpy(&netc_sendbuf[netc_sendbufend + 5], data, length - 5);

    netc_sendbufend += length;
}

static void netc_send_greeting(uint8_t clientversion,
                               const char *playername,
                               const char *password) {
    char data[43];
    memset(data, 0, 43);
    memcpy(data, &clientversion, 1);
    strcpy(data + 1, playername);
    strcpy(data + 22, password);
    netc_send_packet(48, NET_PKTTYPE_C_GREETING, data);
}

static void netc_send_quit(void) {
    netc_send_packet(5, NET_PKTTYPE_C_QUIT, NULL);
}

static void netc_send_pong(uint8_t pingid) {
    netc_send_packet(6, NET_PKTTYPE_C_PONG, &pingid);
}

static void netc_send_wantcontrols(uint8_t playernum) {
    netc_send_packet(6, NET_PKTTYPE_C_WANTCONTROLS, &playernum);
}

static void netc_send_disablecontrols() {
    netc_send_packet(5, NET_PKTTYPE_C_DISABLECONTROLS, NULL);
}

static void netc_send_setcontrols(uint8_t controls) {
    netc_send_packet(6, NET_PKTTYPE_C_SETCONTROLS, &controls);
}

static void netc_recv_quit(void) {
    netc_printf("Server quitting");
    netc_close();
}

static void netc_recv_videomode(uint8_t new_mode) {
    // the palette is a dummy, it will be updated right after this packet
    if (new_mode == 0)
        init_vga("PALET5");
    else
        init_vesa("PALET5");
}

static void netc_recv_setpal_range(uint8_t firstcolor, uint16_t n,
                                   const char *palette) {
    int i;
    char pal[256][3];

    for (i = 0; i < n; i++) {
        pal[i][0] = *palette++ / 4;
        pal[i][1] = *palette++ / 4;
        pal[i][2] = *palette++ / 4;
    }

    setpal_range(pal, firstcolor, n, 0);
}

static void netc_recv_setpal_range_black(uint8_t firstcolor, uint16_t n) {
    setpal_range(NULL, firstcolor, n, 0);
}

static void netc_endframe(void) {
    netc_print_texts();
    do_all();
}

static void netc_recv_endofframe(void) {
    netc_endframe();
    netc_last_endofframe = SDL_GetTicks();
}

static void netc_recv_ping(uint8_t pingid) {
    netc_send_pong(pingid);
}

static void netc_recv_gamemode(uint8_t new_mode) {
    netc_game_mode = new_mode;
}

static void netc_recv_infomsg(const char *msg) {
    netc_printf("%s", msg);
}

static void netc_recv_fillrect(uint16_t x, uint16_t y,
                               uint16_t w, uint16_t h,
                               uint8_t color) {
    fillrect(x, y, w, h, color);
}

static void netc_recv_bitmapdata(uint16_t bitmapid,
                                 uint16_t width, uint16_t height,
                                 uint8_t hastransparency,
                                 char *new_image_data) {
    netcbitmap_maybefree(bitmapid);
    netc_bitmaps[bitmapid] = new Bitmap(width, height,
                                        (unsigned char *) new_image_data,
                                        "fromserver",
                                        hastransparency,
                                        1);
}

static void netc_recv_bitmapdel(uint16_t bitmapid) {
    netcbitmap_maybefree(bitmapid);
}

static void netc_recv_bitmapblitfs(uint16_t bitmapid) {
    if (netc_bitmaps[bitmapid] != NULL) {
        netc_bitmaps[bitmapid]->blit_fullscreen();
    }
}

static void netc_recv_bitmapblit(uint16_t bitmapid, int16_t xx, int16_t yy) {
    if (netc_bitmaps[bitmapid] != NULL) {
        netc_bitmaps[bitmapid]->blit(xx, yy);
    }
}

static void netc_recv_bitmapblitclipped(uint16_t bitmapid,
                                        int16_t xx, int16_t yy,
                                        uint16_t rx, uint16_t ry,
                                        uint16_t rx2, uint16_t ry2) {
    if (netc_bitmaps[bitmapid] != NULL) {
        netc_bitmaps[bitmapid]->blit(xx, yy, rx, ry, rx2, ry2);
    }
}

static void netc_recv_blittobitmap(uint16_t source_bitmapid,
                                   uint16_t target_bitmapid,
                                   int16_t xx,
                                   int16_t yy) {
    if (netc_bitmaps[source_bitmapid] != NULL &&
        netc_bitmaps[target_bitmapid] != NULL) {
        netc_bitmaps[source_bitmapid]->
            blit_to_bitmap(netc_bitmaps[target_bitmapid], xx, yy);
    }
}

static void netc_recv_fade_out(uint8_t type) {
    selected_fade_out(type);
}

static void netc_recv_play_music(char *modname) {
    sdl_play_music_named(modname);
}

static void netc_recv_stop_music(void) {
    sdl_stop_music();
}

static void netc_recv_play_sample(char *samplename,
                                  uint8_t leftvol, uint8_t rightvol,
                                  uint8_t looping) {
    sdl_play_sample_named(samplename, leftvol, rightvol, looping);
}

static void netc_recv_stop_all_samples(void) {
    sdl_stop_all_samples();
}

// dispatcher to process an incoming packet from server
// returns 1 if it was processed successfully, 0 if not
static int netc_receive_packet(uint32_t length, uint8_t type, void *data) {
    char *cdata = (char *)data;

    if (type == NET_PKTTYPE_QUIT && length == 5) {
        netc_recv_quit();
    } else if (type == NET_PKTTYPE_VIDEOMODE && length == 6) {
        uint8_t new_mode = *(uint8_t *)cdata;
        if (new_mode > 1)
            return 0;
        netc_recv_videomode(new_mode);
    } else if (type == NET_PKTTYPE_SETPAL_RANGE && length >= 8) {
        uint8_t firstcolor = *(uint8_t *)cdata;
        uint16_t n = read_from_net_16(cdata + 1);
        if (n == 0 || firstcolor + n > 256 || length != 8+((unsigned int)n)*3)
            return 0;
        netc_recv_setpal_range(firstcolor, n, cdata + 3);
    } else if (type == NET_PKTTYPE_SETPAL_RANGE_BLACK && length == 8) {
        uint8_t firstcolor = *(uint8_t *)cdata;
        uint16_t n = read_from_net_16(cdata + 1);
        if (n == 0 || firstcolor + n > 256)
            return 0;
        netc_recv_setpal_range_black(firstcolor, n);
    } else if (type == NET_PKTTYPE_ENDOFFRAME && length == 5) {
        netc_recv_endofframe();
    } else if (type == NET_PKTTYPE_PING && length == 6) {
        uint8_t pingid = *(uint8_t *)cdata;
        netc_recv_ping(pingid);
    } else if (type == NET_PKTTYPE_GAMEMODE && length == 6) {
        uint8_t new_mode = *(uint8_t *)cdata;
        if (new_mode > 1)
            return 0;
        netc_recv_gamemode(new_mode);
    } else if (type == NET_PKTTYPE_INFOMSG && length == 76) {
        if (!check_printable_string(cdata, 71))
            return 0;
        netc_recv_infomsg(cdata);
    } else if (type == NET_PKTTYPE_FILLRECT && length == 14) {
        uint16_t x = read_from_net_16(cdata);
        uint16_t y = read_from_net_16(cdata + 2);
        uint16_t w = read_from_net_16(cdata + 4);
        uint16_t h = read_from_net_16(cdata + 6);
        uint8_t color = *(uint8_t *)(cdata + 8);

        if (w == 0 || h == 0 ||
            x + w > ((current_mode == VGA_MODE) ? 320 : 800) ||
            y + h > ((current_mode == VGA_MODE) ? 200 : 600))
            return 0;
        netc_recv_fillrect(x, y, w, h, color);
    } else if (type == NET_PKTTYPE_BITMAPDATA && length >= 12) {
        uint16_t bitmapid = read_from_net_16(cdata);
        uint16_t width = read_from_net_16(cdata + 2);
        uint16_t height = read_from_net_16(cdata + 4);
        uint8_t hastransparency = *(uint8_t *)(cdata + 6);
        if (width > 2400 || height > 600 || hastransparency > 1 ||
            length != 12+((unsigned int)width)*((unsigned int)height))
            return 0;
        netc_recv_bitmapdata(bitmapid, width, height, hastransparency,
                             cdata + 7);
    } else if (type == NET_PKTTYPE_BITMAPDEL && length == 7) {
        uint16_t bitmapid = read_from_net_16(cdata);
        netc_recv_bitmapdel(bitmapid);
    } else if (type == NET_PKTTYPE_BITMAPBLITFS && length == 7) {
        uint16_t bitmapid = read_from_net_16(cdata);
        netc_recv_bitmapblitfs(bitmapid);
    } else if (type == NET_PKTTYPE_BITMAPBLIT && length == 11) {
        uint16_t bitmapid = read_from_net_16(cdata);
        int16_t xx = read_from_net_s16(cdata + 2);
        int16_t yy = read_from_net_s16(cdata + 4);
        if (xx < -4000 || xx > 4000 || yy < -4000 || yy > 4000)
            return 0;
        netc_recv_bitmapblit(bitmapid, xx, yy);
    } else if (type == NET_PKTTYPE_BITMAPBLITCLIPPED && length == 19) {
        uint16_t bitmapid = read_from_net_16(cdata);
        int16_t xx = read_from_net_s16(cdata + 2);
        int16_t yy = read_from_net_s16(cdata + 4);
        uint16_t rx = read_from_net_16(cdata + 6);
        uint16_t ry = read_from_net_16(cdata + 8);
        uint16_t rx2 = read_from_net_16(cdata + 10);
        uint16_t ry2 = read_from_net_16(cdata + 12);
        if (xx < -4000 || xx > 4000 || yy < -4000 || yy > 4000 ||
            rx > rx2 || ry > ry2 || rx2 > 799 || ry2 > 599)
            return 0;
        netc_recv_bitmapblitclipped(bitmapid, xx, yy, rx, ry, rx2, ry2);
    } else if (type == NET_PKTTYPE_BLITTOBITMAP && length == 13) {
        uint16_t source_bitmapid = read_from_net_16(cdata);
        uint16_t target_bitmapid = read_from_net_16(cdata + 2);
        int16_t xx = read_from_net_s16(cdata + 4);
        int16_t yy = read_from_net_s16(cdata + 6);
        if (xx < -4000 || xx > 4000 || yy < -4000 || yy > 4000)
            return 0;
        netc_recv_blittobitmap(source_bitmapid, target_bitmapid, xx, yy);
    } else if (type == NET_PKTTYPE_FADE_OUT && length == 6) {
        uint8_t type = *(uint8_t *)cdata;
        if (type > 4)
            return 0;
        netc_recv_fade_out(type);
    } else if (type == NET_PKTTYPE_PLAY_MUSIC && length == 12) {
        if (!check_strict_string(cdata, 7))
            return 0;
        netc_recv_play_music(cdata);
    } else if (type == NET_PKTTYPE_STOP_MUSIC && length == 5) {
        netc_recv_stop_music();
    } else if (type == NET_PKTTYPE_PLAY_SAMPLE && length == 15) {
        if (!check_strict_string(cdata, 7))
            return 0;
        uint8_t leftvol = *(uint8_t *)(cdata + 7);
        uint8_t rightvol = *(uint8_t *)(cdata + 8);
        uint8_t looping = *(uint8_t *)(cdata + 9);
        if (leftvol > 32 || rightvol > 32 || looping > 1)
            return 0;
        netc_recv_play_sample(cdata, leftvol, rightvol, looping);
    } else if (type == NET_PKTTYPE_STOP_ALL_SAMPLES && length == 5) {
        netc_recv_stop_all_samples();
    } else {
        return 0;
    }

    return 1;                   // ok
}

/* process possible incoming data in netc_recvubuf */
static void netc_maybe_receive(void) {
    uint32_t length;
    uint8_t type;
    char *data = netc_recvubuf;
    uint32_t left = netc_recvubufend;

    while (left >= 5) {       /* 5 bytes is the minimum packet size */
        memcpy(&length, data, 4);
        length = SDL_SwapBE32(length);
        if (length < 5 || length > MAXIMUM_S_PACKET_LENGTH) {
            netc_printf("Error: invalid data from server (packet length %u)",
                        length);
            netc_close();
            return;
        }
        if (left < length)
            break;              /* not a full packet */

        memcpy(&type, &data[4], 1);

        if (!netc_receive_packet(length, type, &data[5])) {
            netc_printf("Unknown data from server (type %u length %u)",
                        type, length);
            return;
        }
        if (netc_socket == -1)
            return;

        data += length;
        left -= length;
    }

    if (left == 0) {
        netc_recvubufend = 0;
    } else {
        memmove(netc_recvubuf, data, left);
        netc_recvubufend = left;
    }
}

static void netc_uncompress_init(void) {
    int r;

    netc_recvzs.next_in = Z_NULL;
    netc_recvzs.avail_in = 0;
    netc_recvzs.next_out = Z_NULL;
    netc_recvzs.avail_out = 0;
    netc_recvzs.zalloc = Z_NULL;
    netc_recvzs.zfree = Z_NULL;
    netc_recvzs.opaque = Z_NULL;

    r = inflateInit(&netc_recvzs);
    assert(r == Z_OK);
}

static void netc_uncompress_deinit(void) {
    inflateEnd(&netc_recvzs);
}

/*
 * Uncompresses the data in netc_recvcbuf into netc_recvubuf and calls
 * netc_maybe_receive() for any new uncompressed data.
 */
static void netc_uncompress_and_receive(void) {
    int r;

    do {
        netc_recvzs.next_in = (Bytef *) netc_recvcbuf;
        netc_recvzs.avail_in = netc_recvcbufend;
        netc_recvzs.next_out = (Bytef *) &netc_recvubuf[netc_recvubufend];
        netc_recvzs.avail_out = NETC_RECVU_BUFFER_SIZE - netc_recvubufend;

        r = inflate(&netc_recvzs, Z_SYNC_FLUSH);

        if (r != Z_OK && r != Z_STREAM_END && r != Z_BUF_ERROR) {
            netc_printf("Error %d uncompressing data from server", r);
            netc_close();
            return;
        }

        if (netc_recvzs.avail_in == 0) {
            netc_recvcbufend = 0;
        } else {
            memmove(netc_recvcbuf,
                    netc_recvzs.next_in,
                    netc_recvzs.avail_in);
            netc_recvcbufend = netc_recvzs.avail_in;
        }

        netc_recvubufend = NETC_RECVU_BUFFER_SIZE - netc_recvzs.avail_out;

        netc_maybe_receive();

        if (r == Z_STREAM_END) {
            inflateReset(&netc_recvzs);
        }
    } while (r == Z_STREAM_END || (r == Z_OK && netc_recvzs.avail_out == 0));
}

static int netc_connect(const char *host, int port) {
    struct sockaddr_in sin;
    struct hostent *he;
#ifndef _MSC_VER
    int flags;
#endif
    fd_set writefds;
    struct timeval timeout;

    netc_sendbufend = 0;
    netc_recvubufend = 0;
    netc_recvcbufend = 0;

    netc_printf("Connecting to %s port %d", host, port);

    he = gethostbyname(host);
    if (he == NULL) {
#ifndef _MSC_VER
        netc_printf("gethostbyname: %s", hstrerror(h_errno));
#else
        netc_printf("gethostbyname error");
#endif
        return 0;
    }

    netc_socket = socket(PF_INET, SOCK_STREAM, 0);
    if (netc_socket < 0) {
        perror("socket");
        exit(1);
    }

#ifndef _MSC_VER
    flags = fcntl(netc_socket, F_GETFL);
    flags |= O_NONBLOCK;
    if (fcntl(netc_socket, F_SETFL, flags) < 0) {
        perror("fcntl");
        exit(1);
    }
#endif

    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr = *(struct in_addr *)he->h_addr;

    if (connect(netc_socket,
                (struct sockaddr *)&sin,
                sizeof(struct sockaddr_in)) == 0)
        goto connect_ok;
    if (errno != EINPROGRESS) {
        netc_printf("connect: %s", strerror(errno));
        goto connect_error;
    }

    for (;;) {
        FD_ZERO(&writefds);
        FD_SET(netc_socket, &writefds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 40000; // 1/25 seconds (less than a frame)

        if (select(netc_socket + 1, NULL, &writefds, NULL, &timeout) < 0) {
            perror("select");
            exit(1);
        }

        if (FD_ISSET(netc_socket, &writefds)) {
            char val;
            socklen_t len = sizeof(int);
            if (getsockopt(netc_socket, SOL_SOCKET, SO_ERROR,
                           &val, &len) < 0) {
                perror("getsockopt");
                exit(1);
            }
            if (val == 0)
                goto connect_ok;
            else if (val == EINPROGRESS)
                continue;
            else {
                netc_printf("connect: %s", strerror(val));
                goto connect_error;
            }
        }

        netc_endframe();
        update_key_state();
        if (is_key(SDLK_ESCAPE)) {
            netc_printf("Aborted");
            wait_relase();
            goto connect_error;
        }
    }

 connect_ok:
    netc_printf("Connected to server");
    return 1;

 connect_error:
    close(netc_socket);
    netc_socket = -1;
    return 0;
}

static void netc_doselect(void) {
    fd_set readfds, writefds, exceptfds;
    struct timeval timeout;

    assert(netc_socket >= 0);

    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);
    FD_SET(netc_socket, &readfds);
    FD_SET(netc_socket, &exceptfds);
    if (netc_sendbufend > 0)
        FD_SET(netc_socket, &writefds);

    timeout.tv_sec = 0;
    timeout.tv_usec = 20000;  // 1/50 seconds (less than half a frame)

    if (select(netc_socket + 1,
               &readfds, &writefds, &exceptfds,
               &timeout) < 0) {
        perror("select");
        exit(1);
    }

    if (FD_ISSET(netc_socket, &exceptfds)) {
        netc_printf("Server connection exception");
        netc_close();
        return;
    }

    if (FD_ISSET(netc_socket, &readfds)) {
        int r = read(netc_socket,
                     &netc_recvcbuf[netc_recvcbufend],
                     NETC_RECVC_BUFFER_SIZE - netc_recvcbufend);
        if (r < 0) {
            netc_printf("Read error from server: %s", strerror(errno));
            netc_close();
            return;
        } else if (r == 0) {    /* EOF */
            netc_printf("EOF from server");
            netc_close();
            return;
        } else {
            netc_recvcbufend += r;
            netc_uncompress_and_receive();
            if (netc_socket == -1)
                return;
        }
    }

    if (FD_ISSET(netc_socket, &writefds) && netc_sendbufend > 0) {
        int w = write(netc_socket, netc_sendbuf, netc_sendbufend);
        if (w < 0) {
            netc_printf("Write error to server: %s", strerror(errno));
            netc_close();
            return;
        } else if (w == 0) {
            ;                   /* will retry later */
        } else if (((unsigned int)w) < netc_sendbufend) {
            memmove(netc_sendbuf, &netc_sendbuf[w], netc_sendbufend - w);
            netc_sendbufend -= w;
        } else {
            netc_sendbufend = 0;
        }
    }
}

static void netc_controls(void) {
    uint8_t controls;
    int down, up, roll, guns, bombs;
    static int power = 0;       // stored for on/off power
    static uint8_t last_sent_controls = 255;

    if (netc_game_mode != 1) {
        last_sent_controls = 255;
        return;
    }

    if (netc_controls_country >= 0) {
        get_controls_for_player(netc_controls_country,
                                &down, &up, &power,
                                &roll, &guns, &bombs);

        controls = 0;
        if (down)  controls |= 1;
        if (up)    controls |= 2;
        if (power) controls |= 4;
        if (roll)  controls |= 8;
        if (guns)  controls |= 16;
        if (bombs) controls |= 32;

        if (controls != last_sent_controls) {
            netc_send_setcontrols(controls);
            last_sent_controls = controls;
        }
    }
}

/*
 * Activates or deactivates (countrynum==-1) controls for countrynum,
 * using solo controls of player rosternum.
 */
void netclient_activate_controls(int countrynum, int rosternum) {
    if (countrynum >= 0) {
        set_keys_from_roster(countrynum, rosternum);
        if (netc_socket != -1 && netc_controls_country != countrynum)
            netc_send_wantcontrols(countrynum);
        netc_controls_country = countrynum;
    } else {
        if (netc_socket != -1 && netc_controls_country >= 0)
            netc_send_disablecontrols();
        netc_controls_country = -1;
    }
}

void netclient_loop(const char *host, int port,
                    const char *playername, const char *password) {
    int client_exit = 0;

    netc_game_mode = 0;
    netc_clear_printed_text();
    netcbitmap_init();
    netc_uncompress_init();

    if (!netc_connect(host, port))
        goto netclient_loop_end;

    netc_send_greeting(1, playername, password);

    if (netc_controls_country >= 0)
        netc_send_wantcontrols(netc_controls_country);

    netc_last_endofframe = SDL_GetTicks();

    while (netc_socket != -1) {
        netc_doselect();
        if (netc_socket == -1)
            break;
        if (netc_last_endofframe + 2000 < SDL_GetTicks()) {
            if (netc_last_warn + 3000 < SDL_GetTicks()) {
                netc_printf("No data from server for %d seconds",
                            (SDL_GetTicks() - netc_last_endofframe)/1000);
                netc_last_warn = SDL_GetTicks();
            }
            netc_endframe();    // draw things on screen
        }

        update_key_state();
        if (is_key(SDLK_ESCAPE)) {
            client_exit = 1;
            wait_relase();
            break;
        }
        netc_controls();
    }

    if (netc_socket != -1) {
        netc_printf("Client quitting");
        netc_send_quit();
        netc_doselect();
        netc_close();
    }

 netclient_loop_end:
    netc_uncompress_deinit();
    netcbitmap_free_all();

    if (!client_exit) {
        big_warning(netc_printed_text("Network game client:\n"));
    }
}
