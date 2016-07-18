/*
 *      Copyright (C) 2016  Andrew Fateyev
 *      andrew.ftv@gmail.com
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/select.h>
#include <termios.h>
#include <string.h>
#include <stdlib.h>
#ifdef CONFIG_RASPBERRY_PI
#include "bcm_host.h"
#include <IL/OMX_Broadcom.h>
#include "ilcore.h"
#include "omxclock.h"
#include "guiapi.h"
#include "hw_img_decode.h"
#endif

#include "log.h"
#include "decode.h"
#include "audio_player.h"
#include "video_player.h"

#define CMDOPT_SHOW_INFO    "--show-info"
#define CMDOPT_HELP         "--help"

struct termios orig_termios;

static void hide_console_cursore(void)
{
    printf("\e[?25l");
}

static void show_console_cursore(void)
{
    printf("\e[?25h");
}

static void reset_terminal_mode()
{
    DBG_I("Restore original terminal settings\n");
    tcsetattr(0, TCSANOW, &orig_termios);
}

static void set_conio_terminal_mode()
{
    struct termios new_termios;

    DBG_I("Set non-blocking terminal mode\n");
    /* take two copies - one for now, one for later */
    tcgetattr(0, &orig_termios);
    memcpy(&new_termios, &orig_termios, sizeof(new_termios));

    /* register cleanup handler, and set the new terminal mode */
    atexit(reset_terminal_mode);
    cfmakeraw(&new_termios);
    new_termios.c_oflag |= OPOST;
    tcsetattr(0, TCSANOW, &new_termios);
}

static int kbhit()
{
    struct timeval tv = { 0L, 0L };
    fd_set fds;

    FD_ZERO(&fds);
    FD_SET(0, &fds);
    return select(1, &fds, NULL, NULL, &tv);
}

static int getch()
{
    int rc;
    uint8_t ch;
    
    if ((rc = read(0, &ch, sizeof(ch))) < 0) 
        return rc;
    else
        return ch;
}

static void show_usage(void)
{
    printf("Usage: lbmc <options> input_file\n");
    printf("Options:\n");
    printf("\t"CMDOPT_SHOW_INFO" - print buffers state and current PTS\n");
    printf("\t"CMDOPT_HELP"      - print this text\n");
}

static ret_code_t parse_command_line(int argc, char **argv, char **file, int *show_info)
{
    int i;

    if (argc < 2 || !strcmp(argv[1], CMDOPT_HELP))
    {
        show_usage();
        return L_FAILED;
    }
    if (access(argv[argc - 1], F_OK | R_OK))
    {
        DBG_E("File %s not found\n", argv[argc - 1]);
        return L_NOT_FOUND;
    }
    *file = argv[argc - 1];

    if (argc == 2)
        return L_OK;    

    for (i = 1; i < argc - 1; i++)
    {
        if (!strcmp(argv[i], CMDOPT_SHOW_INFO))
            *show_info = 1;
        else if (!strcmp(argv[1], CMDOPT_HELP))
            continue; /* Ignore it */
        else
            printf("Unknown option: %s\n", argv[i]);
    }
    
    return L_OK;
}

int main(int argc, char **argv)
{
    demux_ctx_h demux_ctx = NULL;
    char *src_filename = NULL;
    audio_player_h aplayer_ctx = NULL;
    int stop = 0;
    int is_muted = 0;
    int is_pause = 0;
    int show_info = 0;
#ifdef CONFIG_RASPBERRY_PI
    TV_DISPLAY_STATE_T tv_state;
    ilcore_comp_h clock = NULL;
    gui_h hgui = NULL;
    win_h hwin = NULL;
    win_h status_window = NULL;
    int show_pic = 0;
#else
    void *clock = NULL;
#endif
#ifdef CONFIG_VIDEO
    video_player_context vplayer_ctx;
#endif

    logs_init(NULL);
    if (parse_command_line(argc, argv, &src_filename, &show_info) != L_OK)
        return -1;

    hide_console_cursore();
    set_conio_terminal_mode();

#ifdef CONFIG_RASPBERRY_PI
    DBG_I("Init OMX components\n");

    bcm_host_init();
    if (OMX_Init() != OMX_ErrorNone)
    {
        DBG_E("OMX_Init failed\n");
        goto end;
    }
#endif

    if (decode_init(&demux_ctx, src_filename, show_info))
        goto end;

#ifdef CONFIG_RASPBERRY_PI
    hdmi_init_display(&tv_state);
    gui_init(&hgui);
    clock = create_omx_clock();
    if (!clock)
        goto end;

    omx_clock_hdmi_clock_sync(clock);
#endif
    if (decode_is_audio(demux_ctx))
        audio_player_start(&aplayer_ctx, demux_ctx, clock);
#ifdef CONFIG_VIDEO  
    if (decode_is_video(demux_ctx))
        video_player_start(&vplayer_ctx, demux_ctx, clock);
    else
        memset(&vplayer_ctx, 0, sizeof(video_player_context));
#endif
    
    if (decode_start(demux_ctx))
        goto end;

    /* Main loop */
    while (decode_is_task_running(demux_ctx))
    {
    if (kbhit())
    {
            int ch;

            ch = getch();
            switch(ch)
            {
            case 'q':
                stop = 1;
                decode_stop(demux_ctx);
                usleep(100000);
                break;
            case ' ':
#ifdef CONFIG_VIDEO
                if (decode_is_video(demux_ctx))
                    is_pause = video_player_pause_toggle(&vplayer_ctx);
#endif
                if (decode_is_audio(demux_ctx))
                    is_pause = audio_player_pause_toggle(aplayer_ctx);
#ifdef CONFIG_RASPBERRY_PI
                if (is_pause)
                {
                    image_h h_img;

                    if (!status_window)
                        window_init(hgui, &status_window, 1742, 50, 128, 128);
                    gui_win_clear_window(status_window);

                    gui_image_load(status_window, "/usr/share/images/pause.png", -1, -1, &h_img);
                    gui_image_draw(status_window, h_img, 0, 0, 0xff);
                    gui_image_unload(status_window, h_img);

                    gui_window_flush(status_window);
                }
                else
                {
                    if (status_window)
                    {
                        if (is_muted)
                        {
                            image_h h_img;

                            gui_win_clear_window(status_window);
                            gui_image_load(status_window, "/usr/share/images/muted.png", -1, -1, &h_img);
                            gui_image_draw(status_window, h_img, 0, 0, 0xff);
                            gui_image_unload(status_window, h_img);

                            gui_window_flush(status_window);
                        }
                        else
                        {
                            window_uninit(status_window);
                            status_window = NULL;
                        }
                    }
                }
#endif
                break;
            case 'a':
                decode_next_audio_stream(demux_ctx);
                break;
            case 'm':
                if (is_pause)
                    break;

                if (audio_player_mute_toggle(aplayer_ctx, &is_muted) != L_OK)
                    break;
#ifdef CONFIG_RASPBERRY_PI
                if (is_muted)
                {
                    image_h h_img;

                    if (!status_window)
                        window_init(hgui, &status_window, 1742, 50, 128, 128);
                    gui_win_clear_window(status_window);

                    gui_image_load(status_window, "/usr/share/images/muted.png", -1, -1, &h_img);
                    gui_image_draw(status_window, h_img, 0, 0, 0xff);
                    gui_image_unload(status_window, h_img);

                    gui_window_flush(status_window);
                }
                else
                {
                    if (status_window)
                    {
                        window_uninit(status_window);
                        status_window = NULL;
                    }
                }
#endif
                break;
#ifdef CONFIG_RASPBERRY_PI
            case 'i':
                {
                    show_pic = !show_pic;
                    if (show_pic)
                    {
                        image_h h_img;

                        window_init(hgui, &hwin, 100, 100, 600, 600);
                        gui_win_clear_window(hwin);

                        gui_image_load(hwin, "/bin/test.jpg", 151, 223, &h_img);
                        gui_image_draw(hwin, h_img, 0, 0, 0x80);
                        gui_image_unload(hwin, h_img);

                        gui_window_flush(hwin);
                    }
                    else
                    {
                        window_uninit(hwin);
                    }
                }
                break;
#endif
            }
        }
        else
        {
            usleep(100000);
        }
    }

end:
    DBG_I("Leave main loop\n");
    show_console_cursore();
    release_all_buffers(demux_ctx);
    DBG_I("Stopping audio player... \n");
    audio_player_stop(aplayer_ctx, stop);
    DBG_I("Done\n");
#ifdef CONFIG_VIDEO
    DBG_I("Stopping video player... \n");
#ifdef CONFIG_RASPBERRY_PI
    gui_uninit(hgui);
    hdmi_uninit_display(&tv_state);
#endif
    video_player_stop(&vplayer_ctx, stop);
    DBG_I("Done\n");
#endif
    decode_uninit(demux_ctx);
#ifdef CONFIG_RASPBERRY_PI
    DBG_I("Deinit OMX components\n");

    if (clock)
        destroy_omx_clock(clock);

    if (OMX_Deinit() != OMX_ErrorNone)
        DBG_E("OMX_deinit failed\n");
    bcm_host_deinit();
#endif
    logs_uninit();

    return 0;
}
