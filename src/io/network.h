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

#ifndef NETWORK_H
#define NETWORK_H

#include "gfx/font.h"

/*
 * Network traffic is a stream of packets (not the underlying IP
 * packets), each of which contains:
 *  1. Length (uint32_t)
 *  2. Type (uint8_t)
 *  3. Data (length - 5 bytes)
 *
 * All numbers are in network byte order (i.e., big-endian), most of
 * them unsigned. Below, char foo[N] denotes an ASCII string padded to
 * the right with NULs to fill N bytes (the Nth byte must be NUL).
 *
 * All data sent from the server to the clients is compressed using
 * zlib (the above-mentioned packets are in the decompressed data).
 * Data sent from the clients to the server is not compressed.
 *
 * If the compressed data stream contains metadata that indicates the
 * end of the stream (i.e., zlib's inflate() returns Z_STREAM_END),
 * the client needs to restart the decompression. The server may use
 * this to reset the compression (though currently it is never used).
 *
 * When sending NET_PKTTYPE_ENDOFFRAME and NET_PKTTYPE_PING packets,
 * the server ensures that enough compressed data to be able to
 * decompress the packet is sent immediately. The client does not need
 * to care about this.
 *
 * The valid packets sent from the server to the clients are:
 *
 * type=NET_PKTTYPE_QUIT:          the server is quitting now
 * length=5
 *
 * type=NET_PKTTYPE_VIDEOMODE:
 *   uint8_t new_mode              VGA_MODE=0=320x200 SVGA_MODE=1=800x600
 * length=6
 * (always followed by a SETPAL_RANGE packet containing all colors
 * and BITMAPDATA packets for all bitmaps)
 *
 * type=NET_PKTTYPE_SETPAL_RANGE:  sets palette[firstcolor .. firstcolor+n-1]
 *   uint8_t firstcolor
 *   uint16_t n
 *   uint8_t palette[n][3]         r,g,b values 0..255
 * length=8+n*3
 *
 * type=NET_PKTTYPE_SETPAL_RANGE_BLACK:
 *   uint8_t firstcolor
 *   uint16_t n
 * length=8
 * (sets palette[firstcolor .. firstcolor+n-1] to black)
 *
 * type=NET_PKTTYPE_ENDOFFRAME:    end of frame (please display it now)
 * length=5
 *
 * type=NET_PKTTYPE_PING:          should reply with C_PONG packet
 *   uint8_t pingid                copy this into your reply
 * length=6
 * (this is used to check network connectivity and to wait for all
 * clients just before starting a game; if you don't reply, the server
 * will disconnect you after about 20 seconds)
 *
 * type=NET_PKTTYPE_GAMEMODE:
 *   uint8_t new_mode              0=in menus now, 1=a game is in progress
 * length=6
 *
 * type=NET_PKTTYPE_INFOMSG:       informational message from host
 *   char msg[71]                  only [a-zA-Z0-9] characters
 * length=76
 * (should be displayed somewhere on screen for a while)
 *
 * type=NET_PKTTYPE_FILLRECT:
 *   uint16_t x, y, w, h           coordinates are always inside the screen
 *   uint8_t color
 * length=14
 *
 * type=NET_PKTTYPE_BITMAPDATA:    new or changed bitmap data for bitmapid
 *   uint16_t bitmapid
 *   uint16_t width
 *   uint16_t height
 *   uint8_t hastransparency               0=no, 1=yes
 *   uint8_t new_image_data[width*height]  row-major order
 * length=12+width*height
 *
 * type=NET_PKTTYPE_BITMAPDEL:     can delete bitmap with bitmapid now
 *   uint16_t bitmapid
 * length=7
 *
 * type=NET_PKTTYPE_BITMAPBLITFS:  Bitmap::blit_fullscreen()
 *   uint16_t bitmapid
 * length=7
 *
 * type=NET_PKTTYPE_BITMAPBLIT:    Bitmap::blit() with no clipping
 *   uint16_t bitmapid
 *   int16_t xx, yy
 * length=11
 *
 * type=NET_PKTTYPE_BITMAPBLITCLIPPED:   Bitmap::blit() with a clip rectangle
 *   uint16_t bitmapid
 *   int16_t xx, yy
 *   uint16_t rx, ry, rx2, ry2
 * length=19
 *
 * type=NET_PKTTYPE_BLITTOBITMAP:  Bitmap::blit_to_bitmap()
 *   uint16_t source_bitmapid
 *   uint16_t target_bitmapid
 *   int16_t xx, yy                top-left position in target
 * length=13
 *
 * type=NET_PKTTYPE_FADE_OUT:      do a screen fade effect
 *   uint8_t type                  0-4 as in random_fade_out()
 * length=6
 * (this packet can safely just be ignored)
 *
 * type=NET_PKTTYPE_PLAY_MUSIC:
 *   char modname[7]
 * length=12
 *
 * type=NET_PKTTYPE_STOP_MUSIC:
 * length=5
 *
 * type=NET_PKTTYPE_PLAY_SAMPLE:
 *   char samplename[7]
 *   uint8_t leftvol, rightvol     both 0 to 32
 *   uint8_t looping               0=no, 1=yes
 * length=15
 *
 * type=NET_PKTTYPE_STOP_ALL_SAMPLES:
 * length=5
 *
 * The valid packets sent from the clients to the server are:
 *
 * type=NET_PKTTYPE_C_GREETING:    must be sent as the first packet
 *   uint8_t clientversion         always 1 for now
 *   char playername[21]           only [a-zA-Z0-9] characters are accepted
 *   char password[21]             must be the one set by the server
 * length=48
 * (the server starts sending game data after receiving this packet)
 *
 * type=NET_PKTTYPE_C_QUIT:        this client will quit now
 * length=5
 * (it is not mandatory to send this before closing the connection)
 *
 * type=NET_PKTTYPE_C_PONG:        a reply to NET_PKTTYPE_PING
 *   uint8_t pingid                same as in the ping
 * length=6
 *
 * type=NET_PKTTYPE_C_WANTCONTROLS:
 *   uint8_t playernum             0-3
 * length=6
 * (this client wants to set controls for playernum)
 * FIXME there is currently no confirmation for this
 *
 * type=NET_PKTTYPE_C_DISABLECONTROLS:
 * length=5
 * (this client no longer wants to set controls)
 *
 * type=NET_PKTTYPE_C_SETCONTROLS:
 *   uint8_t controls    bits 6&7 = 0, bits 0-5 = the controls
 * length=6
 * (set the player controls for the current frame;
 * bits of controls: 0=down, 1=up, 2=power, 3=roll, 4=guns, 5=bombs;
 * zero bit = not pressed, one bit = pressed;
 * the client should send these packets only when the game mode set by
 * NET_PKTTYPE_GAMEMODE is 1)
 *
 * A very simple dummy client for debugging and seeing how much data
 * is sent (the echo command sends a C_GREETING packet using the default
 * password):
 * echo -ne "\0\0\0\x30\xc8\x01debugtestingxtesting\0triplane\0\0\0\0\0\0\0\0\0\0\0\0\0" | nc localhost 9763 | pv >/dev/null
 * or to see some of the data:
 * echo -ne "\0\0\0\x30\xc8\x01debugtestingxtesting\0triplane\0\0\0\0\0\0\0\0\0\0\0\0\0" | nc localhost 9763 | perl -e 'use IO::Uncompress::Inflate qw(inflate); inflate("-" => "-") while !eof();' | hd | head -100
 * However, these dummy clients do not reply to pings, so they will
 * only receive about 20 seconds of data before the server disconnects
 * them.
 */

#define NET_PKTTYPE_QUIT                1
#define NET_PKTTYPE_VIDEOMODE           2
#define NET_PKTTYPE_SETPAL_RANGE        3
#define NET_PKTTYPE_SETPAL_RANGE_BLACK  4
#define NET_PKTTYPE_ENDOFFRAME          5
#define NET_PKTTYPE_PING                6
#define NET_PKTTYPE_GAMEMODE            7
#define NET_PKTTYPE_INFOMSG             8
//#define NET_PKTTYPE_CHATMSG             9

#define NET_PKTTYPE_FILLRECT           10
#define NET_PKTTYPE_BITMAPDATA         11
#define NET_PKTTYPE_BITMAPDEL          12
#define NET_PKTTYPE_BITMAPBLITFS       13
#define NET_PKTTYPE_BITMAPBLIT         14
#define NET_PKTTYPE_BITMAPBLITCLIPPED  15
#define NET_PKTTYPE_BLITTOBITMAP       16

#define NET_PKTTYPE_FADE_OUT           21

#define NET_PKTTYPE_PLAY_MUSIC         31
#define NET_PKTTYPE_STOP_MUSIC         32
#define NET_PKTTYPE_PLAY_SAMPLE        33
#define NET_PKTTYPE_STOP_ALL_SAMPLES   34

#define NET_PKTTYPE_C_GREETING         200
#define NET_PKTTYPE_C_QUIT             201
#define NET_PKTTYPE_C_PONG             202

#define NET_PKTTYPE_C_WANTCONTROLS     211
#define NET_PKTTYPE_C_DISABLECONTROLS  212
#define NET_PKTTYPE_C_SETCONTROLS      213

//#define NET_PKTTYPE_C_CHATMSG          220

void netsend_videomode(char new_mode);
void netsend_setpal_range(const char pal[][3],
                          int firstcolor, int n, int oper);
void netsend_setpal_range_black(int firstcolor, int n);
void netsend_endofframe(void);
void netsend_ping(int pingid);
void netsend_fillrect(int x, int y, int w, int h, int c);
void netsend_infomsg(const char *msg);
void netsend_chatmsg(const char *sender, const char *msg);
int netsend_bitmapdata(int bitmapid,
                       int width, int height, int hastransparency,
                       const unsigned char *image_data);
void netsend_bitmapdel(int bitmapid);
void netsend_bitmapblitfs(int bitmapid);
void netsend_bitmapblit(int bitmapid, int xx, int yy);
void netsend_bitmapblitclipped(int bitmapid, int xx, int yy,
                               int rx, int ry, int rx2, int ry2);
void netsend_blittobitmap(int source_bitmapid, int target_bitmapid,
                          int xx, int yy);

void netsend_fade_out(int type);

void netsend_play_music(const char *modname);
void netsend_stop_music();
void netsend_play_sample(const char *samplename, int leftvol,
                         int rightvol, int looping);
void netsend_stop_all_samples(void);

void network_controls_for_player(int playernum,
                                 int *down, int *up,
                                 int *power, int *roll,
                                 int *guns, int *bombs);

void network_print_serverinfo(void);
int network_display_enable(int enable);
int network_find_preferred_color(const char *clientname);
void network_find_next_controls(int previous, char *clientname);
void network_set_allowed_controls(int playernum, const char *clientname);
void network_get_allowed_controls(int playernum, char *clientname_ret);
void network_reallocate_controls(void);
const char *network_controlling_player_string(int playernum);
void network_change_game_mode(int newmode);
void network_activate_host(const char *listenaddr,
                           int port,
                           const char *password,
                           Font *info_font);
void network_update(void);
void network_quit(void);
int network_is_active(void);
void network_ping(int seconds);
int network_last_ping_done(void);

#endif
