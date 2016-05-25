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
#endif

#include "log.h"
#include "decode.h"
#include "audio_player.h"
#include "video_player.h"
#include "hw_img_decode.h"

struct termios orig_termios;

static void hide_console_cursore(void)
{
	printf("\e[?25l");
}

static void show_console_cursore(void)
{
	printf("\e[?25h");
}

void reset_terminal_mode()
{
	DBG_I("Restore original terminal settings\n");
    tcsetattr(0, TCSANOW, &orig_termios);
}

void set_conio_terminal_mode()
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

int kbhit()
{
    struct timeval tv = { 0L, 0L };
    fd_set fds;

    FD_ZERO(&fds);
    FD_SET(0, &fds);
    return select(1, &fds, NULL, NULL, &tv);
}

int getch()
{
    int rc;
    uint8_t ch;
    
	if ((rc = read(0, &ch, sizeof(ch))) < 0) 
        return rc;
    else
        return ch;
}

int main(int argc, char **argv)
{
	demux_ctx_h demux_ctx = NULL;
	char *src_filename = NULL;
	audio_player_h aplayer_ctx = NULL;
#ifdef CONFIG_RASPBERRY_PI
    TV_DISPLAY_STATE_T tv_state;
    ilcore_comp_h clock = NULL;
    gui_h hgui = NULL;
    win_h hwin = NULL;
    win_h status_window = NULL;
    int show_info = 0;
    int is_muted, is_pause = 0;
#else
    void *clock = NULL;
#endif
#ifdef CONFIG_VIDEO
	video_player_context vplayer_ctx;
#endif

	logs_init(NULL);

	if (argc != 2)
	{
        DBG_F("usage: %s input_file\n", argv[0]);
        return -1;
    }
	src_filename = argv[1];
	hide_console_cursore();
	set_conio_terminal_mode();

    if (access(src_filename, F_OK | R_OK))
    {
        DBG_E("File %s not found\n", src_filename);

        show_console_cursore();
        return -1;
    }

#ifdef CONFIG_RASPBERRY_PI
    DBG_I("Init OMX components\n");

    bcm_host_init();
    if (OMX_Init() != OMX_ErrorNone)
    {
        DBG_E("OMX_Init failed\n");
        goto end;
    }
#endif

	if (decode_init(&demux_ctx, src_filename))
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

                if (is_pause)
                {
                    image_h h_img;

                    if (!status_window)
                        window_init(hgui, &status_window, 1742, 50, 128, 128);
                    gui_win_clear_window(status_window);

                    gui_image_load(status_window, "/usr/share/images/pause.png", &h_img);
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
                            gui_image_load(status_window, "/usr/share/images/muted.png", &h_img);
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
				break;
            case 'a':
                decode_next_audio_stream(demux_ctx);
                break;
            case 'm':
                if (is_pause)
                    break;

                if (audio_player_mute_toggle(aplayer_ctx, &is_muted) != L_OK)
                    break;

                if (is_muted)
                {
                    image_h h_img;

                    if (!status_window)
                        window_init(hgui, &status_window, 1742, 50, 128, 128);
                    gui_win_clear_window(status_window);

                    gui_image_load(status_window, "/usr/share/images/muted.png", &h_img);
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
                break;
            case 'i':
                {
                    show_info = !show_info;
                    if (show_info)
                    {
                        image_h h_img;

                        window_init(hgui, &hwin, 100, 100, 600, 600);
                        gui_win_clear_window(hwin);

                        gui_image_load(hwin, "/bin/test.jpg", &h_img);
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
	DBG_I("Stopping audio player... \n");
	audio_player_stop(aplayer_ctx);
	DBG_I("Done\n");
#ifdef CONFIG_VIDEO
	DBG_I("Stopping video player... \n");
#ifdef CONFIG_RASPBERRY_PI
    gui_uninit(hgui);
    hdmi_uninit_display(&tv_state);
#endif
	video_player_stop(&vplayer_ctx);
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

