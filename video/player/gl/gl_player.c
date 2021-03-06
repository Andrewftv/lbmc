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
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glut.h>
#include <GL/freeglut_ext.h>

#include <libavutil/avutil.h>

#include "log.h"
#include "decode.h"
#include "video_player.h"
#include "timeutils.h"
#include "ft_text.h"

static const char *shader_vert =
    "#version 300 es\n"
    "in vec2 position;"
    "in vec2 texcoord;"
    "out vec2 texCoord;"
    "void main() {"
    "   texCoord = texcoord;"
    "   gl_Position = vec4(position, 0.0, 1.0);"
    "}";

static const char *shader_frag =
    "#version 300 es\n"
    "in highp vec2 texCoord;"
    "out highp vec4 outColor;"
    "uniform sampler2D tex;"
    "uniform highp vec4 textcolor;"
    "uniform int text_render;"
    "void main() {"
    "    if (text_render == 1)"
    "    {"
    "        outColor = vec4(texture(tex, texCoord).aaaa)*textcolor;"
    "    }"
    "    else"
    "    {"
    "         outColor = texture(tex, texCoord);"
    "    }"
    "}";

static GLfloat vertices[] = {
    /* Position   Texcoords */
    -1.0f,  1.0f, 0.0f, 0.0f, /* Top-left */
     1.0f,  1.0f, 1.0f, 0.0f, /* Top-right */
     1.0f, -1.0f, 1.0f, 1.0f, /* Bottom-right */
    -1.0f, -1.0f, 0.0f, 1.0f  /* Bottom-left */
};

typedef struct {
    video_player_common_ctx_t common;

    int width, height;

#ifdef CONFIG_GL_TEXT_RENDERER
    ft_text_h ft_lib;
    GLuint tex_subs;
    char *last_text;
    int tex_inited;
    int tex_width;
    int tex_height;
#endif

    GLuint vs; /* Vertex Shader */
    GLuint fs; /* Fragment Shader */
    GLuint sp; /* Shader Program */
    GLuint vao;
    GLuint vbo;
    GLuint ebo;
    GLint tex_attrib;

    GLuint tex_frame;
    int win;
} player_ctx_t;

static int gl_flush_buffers(void)
{
    glFinish();
    glutSwapBuffers();

    glutMainLoopEvent();

    return 0;
}

static void display(void)
{
}

static void reshape(int w, int h)
{
}

static void print_log(GLuint obj)
{
    int buff_len = 0;
    char buff[1024];
 
    if (glIsShader(obj))
        glGetShaderInfoLog(obj, 1024, &buff_len, buff);
    else
        glGetProgramInfoLog(obj, 1024, &buff_len, buff);
 
    if (buff_len > 0)
        DBG_I("Shader: %s\n", buff);
}

static ret_code_t create_shader(player_ctx_t *ctx)
{
    GLint status;
    GLenum glew_status;

    GLuint elements[] = {
        0, 1, 2,
        2, 3, 0
    };

    glewExperimental = GL_TRUE;
    glew_status = glewInit();
    if (GLEW_OK != glew_status)
    {
        DBG_E("%s\n", glewGetErrorString(glew_status));
        return 1;
    }

    if (!GLEW_VERSION_2_0)
    {
        DBG_E("No sharers support\n");
        return L_FAILED;
    }

    glGenVertexArrays(1, &ctx->vao);
    glBindVertexArray(ctx->vao);

    glGenBuffers(1, &ctx->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, ctx->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glGenBuffers(1, &ctx->ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ctx->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(elements), elements, GL_STATIC_DRAW);

    ctx->vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(ctx->vs, 1, &shader_vert, NULL);
    glCompileShader(ctx->vs);

    glGetShaderiv(ctx->vs, GL_COMPILE_STATUS, &status);
    DBG_I("Vertix copmile status is %s\n", status ? "OK" : "FAILED");
    print_log(ctx->vs);
 
    ctx->fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(ctx->fs, 1, &shader_frag, NULL);
    glCompileShader(ctx->fs);
    
    glGetShaderiv(ctx->fs, GL_COMPILE_STATUS, &status);
    DBG_I("Fragment copmile status is %s\n", status ? "OK" : "FAILED");
    print_log(ctx->fs); 

    ctx->sp = glCreateProgram();
    glAttachShader(ctx->sp, ctx->vs);
    glAttachShader(ctx->sp, ctx->fs);
    glBindFragDataLocation(ctx->sp, 0, "outColor");
    glLinkProgram(ctx->sp);

    glGetShaderiv(ctx->fs, GL_COMPILE_STATUS, &status);
    DBG_I("Link status is %s\n", status ? "OK" : "FAILED");
    print_log(ctx->sp);
 
    glUseProgram(ctx->sp);

    GLint posAttrib = glGetAttribLocation(ctx->sp, "position");
    glEnableVertexAttribArray(posAttrib);
    glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), 0);

    ctx->tex_attrib = glGetAttribLocation(ctx->sp, "texcoord");
    glEnableVertexAttribArray(ctx->tex_attrib);
    glVertexAttribPointer(ctx->tex_attrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));

    return L_OK;
}

static void delete_shader(player_ctx_t *ctx)
{
    glDeleteShader(ctx->vs);
    glDeleteShader(ctx->fs);
    glDeleteProgram(ctx->sp);

    glDeleteBuffers(1, &ctx->ebo);
    glDeleteBuffers(1, &ctx->vbo);

    glDeleteVertexArrays(1, &ctx->vao);
}

#ifdef CONFIG_GL_TEXT_RENDERER
static void set_text_color(player_ctx_t *ctx, float r, float g, float b, float a)
{
    GLint textcolor;
    GLfloat color[4];

    color[0] = r;
    color[1] = g;
    color[2] = b;
    color[3] = a;

    textcolor = glGetUniformLocation(ctx->sp, "textcolor");
    glUniform4fv(textcolor, 1, color);    
}

static void calc_texture_size(player_ctx_t *ctx, const char *text, int *w, int *h, int *origin)
{
    const char *p;
    FT_GlyphSlot g;

    *w = 0;
    *origin = 0;

    for (p = text; *p; p++)
    {
        if(ft_load_char(ctx->ft_lib, *p))
            continue;

        g = ft_text_get_glyph(ctx->ft_lib);
    
        *w += (g->advance.x >> 6);
        if (g->bitmap.rows - (g->metrics.horiBearingY >> 6) > *origin)
            *origin = g->bitmap.rows - (g->metrics.horiBearingY >> 6);
    }
    ft_text_get_size(ctx->ft_lib, h);
}

static void render_text(player_ctx_t *ctx, const char *text, float x, float y, float red, float green, float blue)
{
    const char *p;
    FT_GlyphSlot g;
    //FT_Bitmap bitmap;
    int xxx = x;
    int origin;

    float sx = 2.0 / glutGet(GLUT_WINDOW_WIDTH);
    float sy = 2.0 / glutGet(GLUT_WINDOW_HEIGHT);

    int fx, fy, fw, fh;

    glUniform1i(glGetUniformLocation(ctx->sp, "text_render"), 1);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ctx->tex_subs);
    glUniform1i(glGetUniformLocation(ctx->sp, "tex"), 1);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (!ctx->last_text || strcmp(text, ctx->last_text))
    {
        if (ctx->tex_inited)
        {
            ctx->tex_inited = 0;
            if (ctx->last_text)
                free(ctx->last_text);
        }

        DBG_I("Create new subtitles texture\n");

        calc_texture_size(ctx, text, &ctx->tex_width, &ctx->tex_height, &origin);
        //origin++;

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ctx->tex_width, ctx->tex_height/* + 2*/, 0, GL_ALPHA, GL_UNSIGNED_BYTE,
            NULL);

        for(p = text; *p; p++)
        {
            if(ft_load_char(ctx->ft_lib, *p))
                continue;

            set_text_color(ctx, red, green, blue, 1.0);
 
            g = ft_text_get_glyph(ctx->ft_lib);
            fx = xxx + (g->metrics.horiBearingX >> 6);
            fy = ctx->tex_height - origin - (g->metrics.horiBearingY >> 6);
            fw = g->bitmap.width;
            fh = g->bitmap.rows;
            glTexSubImage2D(GL_TEXTURE_2D, 0, fx, fy, fw, fh, GL_ALPHA, GL_UNSIGNED_BYTE, g->bitmap.buffer);
    
            //set_text_color(ctx, 0.0, 0.0, 0.0, 1.0);

            //if (ft_load_stroker(ctx->ft_lib, *p, &bitmap) == L_OK)
            //{
            //    glTexSubImage2D(GL_TEXTURE_2D, 0, fx, fy, bitmap.width, bitmap.rows, GL_ALPHA,
            //        GL_UNSIGNED_BYTE, bitmap.buffer);

            //    ft_done_stroker(ctx->ft_lib);
            //}
            xxx += (g->advance.x >> 6);  
        }
        ctx->last_text = strdup(text);
        ctx->tex_inited = 1;
    }
    float w = ctx->tex_width * sx;
    float h = (ctx->tex_height /*+ 2*/) * sy;

    vertices[0] = x; vertices[1] = y;
    vertices[4] = x + w; vertices[5] = y;
    vertices[8] = x + w; vertices[9] = y - h;
    vertices[12] = x; vertices[13] = y - h;

    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    glUniform1i(glGetUniformLocation(ctx->sp, "text_render"), 0);
}
#endif

static ret_code_t gl_init(video_player_h h)
{
    int argc = 1;
    char *argv[] = {""};
    player_ctx_t *ctx = (player_ctx_t *)h;

    glutInit(&argc, argv);
    glutInitContextVersion(3,0);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA /*| GLUT_DEPTH*/);

    glutInitWindowSize(ctx->width, ctx->height);
    ctx->win = glutCreateWindow("LBMC GL player");

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);

    create_shader(ctx);

    glClearColor(0.0, 0.0, 0.0, 1.0);
    glShadeModel(GL_SMOOTH);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glGenTextures(1, &ctx->tex_frame);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ctx->tex_frame);
    glUniform1i(glGetUniformLocation(ctx->sp, "tex"), 0);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ctx->width, ctx->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

#ifdef CONFIG_GL_TEXT_RENDERER
    if (ft_text_init(&ctx->ft_lib))
        ctx->ft_lib = NULL;
    glGenTextures(1, &ctx->tex_subs);
#endif
    
    if (decode_setup_video_buffers(ctx->common.demux_ctx, VIDEO_BUFFERS, 1, 80 * 1024) != L_OK)
        return L_FAILED;

    ctx->common.first_pkt = 1;
    msleep_init(&ctx->common.sched);

    decode_start_read(ctx->common.demux_ctx);

    return L_OK;
}

static void gl_uninit(video_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    msleep_uninit(ctx->common.sched);

    glDeleteTextures(1, &ctx->tex_frame);
#ifdef CONFIG_GL_TEXT_RENDERER
    if (ctx->last_text)
        free(ctx->last_text);

    if (ctx->ft_lib)
        ft_text_uninit(ctx->ft_lib);
    glDeleteTextures(1, &ctx->tex_subs);
#endif
    glutDestroyWindow(ctx->win);

    delete_shader(ctx);
}

static void gl_set_viewport(player_ctx_t *ctx)
{
    int w, h, wpic, hpic;
    double xscale, yscale;

    w = glutGet(GLUT_WINDOW_WIDTH);
    h = glutGet(GLUT_WINDOW_HEIGHT);

    xscale = (double)w / (double)ctx->width;
    yscale = (double)h / (double)ctx->height;
    if (xscale > yscale)
    {
        wpic = ctx->width * yscale;
        hpic = ctx->height * yscale;
        glViewport((w - wpic) / 2, 0, wpic, hpic);
    }
    else
    {
        wpic = ctx->width * xscale;
        hpic = ctx->height * xscale;
        glViewport(0, (h - hpic) / 2, wpic, hpic);
    }
}

#ifdef CONFIG_GL_TEXT_RENDERER
static int frames = 0;
#endif

static ret_code_t gl_draw_frame(video_player_h h, media_buffer_t *buff)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    decode_set_current_playing_pts(ctx->common.demux_ctx, buff->pts_ms);

    gl_set_viewport(ctx);
      
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(ctx->sp, "tex"), 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ctx->width, ctx->height, GL_RGBA, GL_UNSIGNED_BYTE,
        buff->s.video.buffer[0]);

#ifdef CONFIG_GL_TEXT_RENDERER
    vertices[0] = -1.0; vertices[1] = 1.0;
    vertices[4] = 1.0; vertices[5] = 1.0;
    vertices[8] = 1.0; vertices[9] = -1.0;
    vertices[12] = -1.0; vertices[13] = -1.0;
    
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
#endif

    glClear(GL_COLOR_BUFFER_BIT /*| GL_DEPTH_BUFFER_BIT*/);
    glEnable(GL_TEXTURE_2D);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

#ifdef CONFIG_GL_TEXT_RENDERER
    frames++;
    ft_text_set_size(ctx->ft_lib, 48);
    if (frames < 100)
        render_text(ctx, "The Quick Brown Fox Jumps Over The Lazy Dog", -1.0,  0.0, 1.0, 1.0, 1.0);
    else
        render_text(ctx, "Hello !!!", -1.0,  0.0, 1.0, 1.0, 1.0);
#endif

    gl_flush_buffers();

    glDisable(GL_TEXTURE_2D);

    decode_release_video_buffer(ctx->common.demux_ctx, buff);

    return L_OK;
}

static void gl_idle(video_player_h h)
{
    glutMainLoopEvent();
}

static int gl_pause_toggle(video_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    if (!ctx)
        return 0;

    if (ctx->common.state == PLAYER_PAUSE)
    {
        struct timespec end_pause;
        uint32_t diff;

        ctx->common.state = PLAYER_PLAY;
        clock_gettime(CLOCK_MONOTONIC, &end_pause);
        diff = util_time_diff(&end_pause, &ctx->common.start_pause);
        util_time_add(&ctx->common.base_time, diff);
    }
    else
    {
        ctx->common.state = PLAYER_PAUSE;
        clock_gettime(CLOCK_MONOTONIC, &ctx->common.start_pause);
    }

    return (ctx->common.state == PLAYER_PAUSE);
}

static ret_code_t gl_seek(video_player_h h, seek_direction_t dir, int32_t seek)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    if (dir == L_SEEK_FORWARD)
        util_time_sub(&ctx->common.base_time, seek);
    else if (dir == L_SEEK_BACKWARD)
        util_time_add(&ctx->common.base_time, seek);

    return L_OK;
}

static ret_code_t gl_schedule(video_player_h h, media_buffer_t *buf)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    if (ctx->common.first_pkt)
    {
        clock_gettime(CLOCK_MONOTONIC, &ctx->common.base_time);
        ctx->common.first_pkt = 0;
    }
    else if (buf->pts_ms != AV_NOPTS_VALUE)
    {
        struct timespec curr_time;
        int diff;

        clock_gettime(CLOCK_MONOTONIC, &curr_time);
        diff = util_time_diff(&curr_time, &ctx->common.base_time);
        DBG_V("Current PTS=%lld time diff=%d\n", buf->pts_ms, diff);
        if (diff > 0 && buf->pts_ms > diff)
        {
            diff = buf->pts_ms - diff;
            if (diff > 5000)
            {
                DBG_W("The frame requests %d msec wait. Drop it and continue\n", diff);
                decode_release_video_buffer(ctx->common.demux_ctx, buf);
                return L_FAILED;
            }
            DBG_V("Going to sleep for %d ms\n", diff);
            msleep_wait(ctx->common.sched, diff);
        }
    }
    return L_OK;
}

ret_code_t video_player_start(video_player_h *player_ctx, demux_ctx_h h, void *clock)
{
    player_ctx_t *ctx;
    ret_code_t rc = L_OK;
    int width, height;
    pthread_attr_t attr;
    struct sched_param param;

    ctx = (player_ctx_t *)malloc(sizeof(player_ctx_t));
    if (!ctx)
    {
        DBG_E("Memory allocation failed\n");
        return L_FAILED;
    }
    memset(ctx, 0, sizeof(player_ctx_t));
    ctx->common.demux_ctx = h;

    if (devode_get_video_size(h, &width, &height))
    {
        DBG_E("Can not get video size\n");
        return L_FAILED;
    }

    ctx->width = width;
    ctx->height = height;

    ctx->common.init = gl_init;
    ctx->common.uninit = gl_uninit;
    ctx->common.draw_frame = gl_draw_frame;
    ctx->common.idle = gl_idle;
    ctx->common.pause = gl_pause_toggle;
    ctx->common.seek = gl_seek;
    ctx->common.schedule = gl_schedule;

    /* Use default scheduler. Set SCHED_RR or SCHED_FIFO request root access */
    pthread_attr_init(&attr);
    param.sched_priority = 2;
    pthread_attr_setschedparam(&attr, &param);
    if (pthread_create(&ctx->common.task, &attr, player_main_routine, ctx))
    {
        DBG_E("Create thread falled\n");
        rc = L_FAILED;
    }
    pthread_attr_destroy(&attr);

    *player_ctx = ctx;

    return rc;
}
