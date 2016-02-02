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
    int width, height;

#ifdef TEXT_RENDERER
    ft_text_h ft_lib;
    GLuint tex_osd;
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

    demux_ctx_h demux;
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
    DBG_I("Vertix copmile status is %s\n", status ? "OK" : "FAILED");
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

#ifdef TEXT_RENDERER
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

static void render_text(player_ctx_t *ctx, const char *text, float x, float y, float red, float green, float blue)
{
    const char *p;
    FT_GlyphSlot g;
    FT_Bitmap bitmap;

    float sx = 2.0 / glutGet(GLUT_WINDOW_WIDTH);
    float sy = 2.0 / glutGet(GLUT_WINDOW_HEIGHT);

    glUniform1i(glGetUniformLocation(ctx->sp, "text_render"), 1);

    glGenTextures(1, &ctx->tex_osd);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ctx->tex_osd);
    glUniform1i(glGetUniformLocation(ctx->sp, "tex"), 1);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    for(p = text; *p; p++)
    {
        if(ft_load_char(ctx->ft_lib, *p))
            continue;

        set_text_color(ctx, red, green, blue, 1.0);
 
        g = ft_text_get_glyph(ctx->ft_lib);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g->bitmap.width, g->bitmap.rows, 0, GL_ALPHA, GL_UNSIGNED_BYTE,
            g->bitmap.buffer);

        float x2 = x + g->bitmap_left * sx;
        float y2 = -y - g->bitmap_top * sy;
        float w = g->bitmap.width * sx;
        float h = g->bitmap.rows * sy;

        vertices[0] = x2; vertices[1] = -y2;
        vertices[4] = x2 + w; vertices[5] = -y2;
        vertices[8] = x2 + w; vertices[9] = -y2 - h;
        vertices[12] = x2; vertices[13] = -y2 - h;
    
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        set_text_color(ctx, 0.0, 0.0, 0.0, 1.0);
        
        if (ft_load_stroker(ctx->ft_lib, *p, &bitmap) == L_OK)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bitmap.width, bitmap.rows, 0, GL_ALPHA, GL_UNSIGNED_BYTE,
                bitmap.buffer);

            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
            ft_done_stroker(ctx->ft_lib);
        }
    
        x += (g->advance.x >> 6) * sx;
        y += (g->advance.y >> 6) * sy;
    }
    glDeleteTextures(1, &ctx->tex_osd);

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

    decode_start_read(ctx->demux);

    return L_OK;
}

static void gl_uninit(video_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    glDeleteTextures(1, &ctx->tex_frame);
#ifdef TEXT_RENDERER
    glDeleteTextures(1, &ctx->tex_osd);
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

static ret_code_t gl_draw_frame(video_player_h h, video_buffer_t *buff)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    gl_set_viewport(ctx);
      
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(ctx->sp, "tex"), 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ctx->width, ctx->height, GL_RGBA, GL_UNSIGNED_BYTE, buff->buffer[0]);

#ifdef TEXT_RENDERER
    vertices[0] = -1.0; vertices[1] = 1.0;
    vertices[4] = 1.0; vertices[5] = 1.0;
    vertices[8] = 1.0; vertices[9] = -1.0;
    vertices[12] = -1.0; vertices[13] = -1.0;
    
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
#endif

    glClear(GL_COLOR_BUFFER_BIT /*| GL_DEPTH_BUFFER_BIT*/);
       glEnable(GL_TEXTURE_2D);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

#ifdef TEXT_RENDERER
    ft_text_set_size(ctx->ft_lib, 48);    
    render_text(ctx, "The Quick Brown Fox Jumps Over The Lazy Dog", -1.0,  0.0, 1.0, 1.0, 1.0);
#endif

    gl_flush_buffers();

    glDisable(GL_TEXTURE_2D);

    return L_OK;
}

static void gl_idle(video_player_h h)
{
    glutMainLoopEvent();
}

ret_code_t video_player_start(video_player_context *player_ctx, demux_ctx_h h, void *clock)
{
    player_ctx_t *ctx;
    ret_code_t rc = L_OK;
    int width, height;
    pthread_attr_t attr;
    struct sched_param param;

    memset(player_ctx, 0, sizeof(video_player_context));
    player_ctx->demux_ctx = h;

    ctx = (player_ctx_t *)malloc(sizeof(player_ctx_t));
    if (!ctx)
    {
        DBG_E("Memory allocation failed\n");
        return L_FAILED;
    }

    memset(ctx, 0, sizeof(player_ctx_t));
    player_ctx->priv = ctx;

    if (devode_get_video_size(h, &width, &height))
    {
        DBG_E("Can not get video size\n");
        return L_FAILED;
    }

    ctx->width = width;
    ctx->height = height;
    ctx->demux = h;

    player_ctx->init = gl_init;
    player_ctx->uninit = gl_uninit;
    player_ctx->draw_frame = gl_draw_frame;
    player_ctx->idle = gl_idle;

    /* Use default scheduler. Set SCHED_RR or SCHED_FIFO request root access */
    pthread_attr_init(&attr);
    param.sched_priority = 2;
    pthread_attr_setschedparam(&attr, &param);
    if (pthread_create(&player_ctx->task, &attr, player_main_routine, player_ctx))
    {
        DBG_E("Create thread falled\n");
        rc = L_FAILED;
    }
    pthread_attr_destroy(&attr);

    return rc;
}

