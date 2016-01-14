#include "log.h"
#include "ft_text.h"

typedef struct {
    FT_Library ft_lib;
    FT_Face face;
    FT_Glyph glyph;

    int size;
} ft_ctx_t;

ret_code_t ft_text_init(ft_text_h *h)
{
    ft_ctx_t *ctx;

    ctx = (ft_ctx_t *)malloc(sizeof(ft_ctx_t));
    if (!ctx)
    {
        DBG_E("Could not allocate memory\n");
        return L_FAILED;
    }
    memset(ctx, 0, sizeof(ft_ctx_t));

    if(FT_Init_FreeType(&ctx->ft_lib))
    {
        ctx->ft_lib = 0;
        DBG_E("Could not initialize freetype library\n");
        goto Error;
    }

    if(FT_New_Face(ctx->ft_lib, "/usr/share/fonts/gnu-free/FreeSerif.ttf", 0, &ctx->face))
    {
        ctx->face = NULL;
        DBG_E("Could not initialize FreeSans.ttf\n");
        goto Error;
    }

    *h = ctx;    

    return L_OK;

Error:
    
    ft_text_uninit(ctx);
    *h = NULL;    
    return L_FAILED;
}

void ft_text_uninit(ft_text_h h)
{
    ft_ctx_t *ctx = (ft_ctx_t *)h;

    if (!ctx)
        return;
    
    if (ctx->face)
        FT_Done_Face(ctx->face);

    if (ctx->ft_lib)
        FT_Done_FreeType(ctx->ft_lib);

    free(ctx);
}

ret_code_t ft_text_set_size(ft_text_h *h, int size)
{
    ft_ctx_t *ctx = (ft_ctx_t *)h;

    if (!ctx || !ctx->face)
        return L_FAILED;

    ctx->size = size;

    if (FT_Set_Pixel_Sizes(ctx->face, 0, size))
    {
        DBG_E("Could not set font size\n");
        return L_FAILED;
    }

    return L_OK;
}

ret_code_t ft_text_get_size(ft_text_h *h, int *size)
{
    ft_ctx_t *ctx = (ft_ctx_t *)h;

    if (!ctx || !ctx->face)
        return L_FAILED;

    *size = ctx->size;

    return L_OK;
}

ret_code_t ft_load_char(ft_text_h *h, char ch)
{
    FT_UInt index;
    ft_ctx_t *ctx = (ft_ctx_t *)h;

    if (!ctx || !ctx->face)
        return L_FAILED;

    index = FT_Get_Char_Index(ctx->face, ch);

    if (FT_Load_Glyph(ctx->face, index, FT_LOAD_RENDER))
        return L_FAILED;

    return L_OK;
}

ret_code_t ft_load_stroker(ft_text_h *h, char ch, FT_Bitmap *bitmap)
{
    FT_Stroker stroker;
    ft_ctx_t *ctx = (ft_ctx_t *)h;

    if (!ctx || !ctx->face)
        return L_FAILED;

    FT_Load_Char(ctx->face, ch, FT_LOAD_DEFAULT);

    FT_Stroker_New(ctx->ft_lib, &stroker);
    FT_Stroker_Set(stroker, ctx->size, FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);

    FT_Get_Glyph(ctx->face->glyph, &ctx->glyph);
    FT_Glyph_Stroke(&ctx->glyph, stroker, 1);
    FT_Glyph_To_Bitmap(&ctx->glyph, FT_RENDER_MODE_NORMAL, 0, 1);

    *bitmap = ((FT_BitmapGlyph)ctx->glyph)->bitmap;

    FT_Stroker_Done(stroker);

    return L_OK;
}

void ft_done_stroker(ft_text_h *h)
{
    ft_ctx_t *ctx = (ft_ctx_t *)h;

    FT_Done_Glyph(ctx->glyph);
}

FT_GlyphSlot ft_text_get_glyph(ft_text_h *h)
{
    ft_ctx_t *ctx = (ft_ctx_t *)h;

    if (!ctx || !ctx->face)
        return NULL;

    return ctx->face->glyph;
}

