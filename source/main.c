// Viewport origin tests for GPUREG_VIEWPORT_XY. D-pad left and right pick one of three:
//  1  A negative origin: an 800x480 picture scrolled behind the top screen by moving the viewport origin.
//  2  A position past 511: a picture drawn at 512,768 in a 1024x1024 buffer.
//  3  Straddling the 1024 edge: the same picture at 960,960, which only fits if the origin wraps.
// Mode A uses the origin under test. Mode B draws the same thing with the viewport at 0,0, as the
// reference. X switches between them. Every test starts in mode A.
// L, SELECT and the rest of the D-pad do nothing, so a screenshot shortcut does not disturb a test.

#include <3ds.h>
#include <citro3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vshader_shbin.h"

#define DISPLAY_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
	GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

#define WORLD_W  800
#define WORLD_H  480
#define SCREEN_W 400
#define SCREEN_H 240
#define CELL     40
#define COLS     (WORLD_W / CELL)
#define ROWS     (WORLD_H / CELL)

#define BUF 1024 // the off-screen buffer, square
#define PIC 128  // the picture drawn into it, square
#define POS_X    512 // where test 2 draws it
#define POS_Y    768
#define STRADDLE 960 // where test 3 draws it, on both axes

#define WORLD_VERTS  (COLS * ROWS * 12)
#define TARGET_VERTS 12
#define HUD_VERTS    24
#define PIC_VERTS    (16 * 12 + 24 + 3)
#define REF3_MAX     (PIC_VERTS * 4)
#define VIEW2_VERTS  12
#define VIEW3_VERTS  30
#define FRAME_VERTS  30

enum
{
	V_WORLD  = 0,
	V_TARGET = V_WORLD + WORLD_VERTS,
	V_HUD    = V_TARGET + TARGET_VERTS,
	V_PIC    = V_HUD + HUD_VERTS,
	V_REF2   = V_PIC + PIC_VERTS,
	V_REF3   = V_REF2 + PIC_VERTS,
	V_VIEW2  = V_REF3 + REF3_MAX,
	V_VIEW3  = V_VIEW2 + VIEW2_VERTS,
	V_FRAME  = V_VIEW3 + VIEW3_VERTS,
	TOTAL_VERTS = V_FRAME + FRAME_VERTS,
};

enum { TEST_NEGATIVE, TEST_POSITION, TEST_STRADDLE, TEST_COUNT };

typedef struct { float x, y, z, r, g, b, a, u, v; } vertex;

static DVLB_s* vshader_dvlb;
static shaderProgram_s program;
static int uLoc_projection;
static vertex* vbo;
static C3D_Tex bufTex;
static C3D_RenderTarget* top;
static C3D_RenderTarget* bufTarget;
static bool haveBuffer;
static int ref3Verts;

static vertex* quad(vertex* v, float x0, float y0, float x1, float y1, float r, float g, float b)
{
	const float xs[6] = { x0, x1, x1, x0, x1, x0 };
	const float ys[6] = { y0, y0, y1, y0, y1, y1 };
	for (int i = 0; i < 6; i++)
		*v++ = (vertex){ xs[i], ys[i], 0.5f, r, g, b, 1.0f, 0.0f, 0.0f };
	return v;
}

// A piece of the buffer on the screen: screen rectangle, then buffer rectangle in pixels.
static vertex* view(vertex* v, float x0, float y0, float x1, float y1, float bx0, float by0, float bx1, float by1)
{
	const float xs[6] = { x0, x1, x1, x0, x1, x0 };
	const float ys[6] = { y0, y0, y1, y0, y1, y1 };
	const float us[6] = { bx0, bx1, bx1, bx0, bx1, bx0 };
	const float vs[6] = { by0, by0, by1, by0, by1, by1 };
	for (int i = 0; i < 6; i++)
		*v++ = (vertex){ xs[i], ys[i], 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, us[i] / BUF, vs[i] / BUF };
	return v;
}

static vertex* diamond(vertex* v, float cx, float cy, float size, float r, float g, float b)
{
	const float xs[6] = { cx - size, cx, cx + size, cx - size, cx + size, cx };
	const float ys[6] = { cy, cy - size, cy, cy, cy, cy + size };
	for (int i = 0; i < 6; i++)
		*v++ = (vertex){ xs[i], ys[i], 0.5f, r, g, b, 1.0f, 0.0f, 0.0f };
	return v;
}

// Every cell has its own colour, so any part of the picture tells where it is.
static void buildWorld(void)
{
	vertex* v = vbo + V_WORLD;
	for (int row = 0; row < ROWS; row++)
	{
		for (int col = 0; col < COLS; col++)
		{
			const float shade = ((col + row) & 1) ? 1.0f : 0.7f;
			const float r = shade * (0.25f + 0.75f * col / (COLS - 1));
			const float g = shade * (0.25f + 0.75f * row / (ROWS - 1));
			const float b = shade * (1.0f - 0.75f * col / (COLS - 1));
			const float x = col * CELL, y = row * CELL;
			v = quad(v, x, y, x + CELL, y + CELL, r, g, b);
			v = quad(v, x + 14, y + 14, x + CELL - 14, y + CELL - 14, 1.0f - r, 1.0f - g, 1.0f - b);
		}
	}
}

static void buildTarget(float tx, float ty)
{
	vertex* v = vbo + V_TARGET;
	v = diamond(v, tx, ty, 16, 0.0f, 0.0f, 0.0f);
	diamond(v, tx, ty, 12, 1.0f, 0.9f, 0.1f);
}

// Four bars around the middle of the screen, in screen coordinates.
static void buildHud(void)
{
	vertex* v = vbo + V_HUD;
	const float cx = SCREEN_W / 2, cy = SCREEN_H / 2;
	v = quad(v, cx - 26, cy - 1, cx - 10, cy + 1, 1, 1, 1);
	v = quad(v, cx + 10, cy - 1, cx + 26, cy + 1, 1, 1, 1);
	v = quad(v, cx - 1, cy - 26, cx + 1, cy - 10, 1, 1, 1);
	quad(v, cx - 1, cy + 10, cx + 1, cy + 26, 1, 1, 1);
}

// A rectangle of the picture, moved by ox,oy and cut to the buffer. The far edges stop a quarter of a
// pixel short of 1024: a console drops a triangle that touches the far edge of a 1024 viewport.
static vertex* piece(vertex* v, float x0, float y0, float x1, float y1, float r, float g, float b, float ox, float oy)
{
	const float edge = BUF - 0.25f;
	x0 += ox; x1 += ox; y0 += oy; y1 += oy;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > edge) x1 = edge;
	if (y1 > edge) y1 = edge;
	if (x0 >= x1 || y0 >= y1) return v;
	return quad(v, x0, y0, x1, y1, r, g, b);
}

// The 128x128 picture: 16 cells, a white border and a white corner mark at the top left, so a quarter
// of it, or a flipped copy, cannot be mistaken for the whole.
static vertex* picture(vertex* v, float ox, float oy)
{
	for (int row = 0; row < 4; row++)
	{
		for (int col = 0; col < 4; col++)
		{
			const float shade = ((col + row) & 1) ? 1.0f : 0.6f;
			const float r = shade * (0.2f + 0.8f * col / 3);
			const float g = shade * (0.2f + 0.8f * row / 3);
			const float b = shade * (1.0f - 0.8f * col / 3);
			const float x = col * 32, y = row * 32;
			v = piece(v, x, y, x + 32, y + 32, r, g, b, ox, oy);
			v = piece(v, x + 10, y + 10, x + 22, y + 22, 1.0f - r, 1.0f - g, 1.0f - b, ox, oy);
		}
	}
	v = piece(v, 0, 0, PIC, 2, 1, 1, 1, ox, oy);
	v = piece(v, 0, PIC - 2, PIC, PIC, 1, 1, 1, ox, oy);
	v = piece(v, 0, 0, 2, PIC, 1, 1, 1, ox, oy);
	v = piece(v, PIC - 2, 0, PIC, PIC, 1, 1, 1, ox, oy);

	const float mx = ox + 6, my = oy + PIC - 6;
	if (mx >= 0 && mx + 24 <= BUF && my - 24 >= 0 && my <= BUF)
	{
		*v++ = (vertex){ mx, my, 0.5f, 1, 1, 1, 1, 0, 0 };
		*v++ = (vertex){ mx + 24, my, 0.5f, 1, 1, 1, 1, 0, 0 };
		*v++ = (vertex){ mx, my - 24, 0.5f, 1, 1, 1, 1, 0, 0 };
	}
	return v;
}

// Mode A draws the picture through its viewport. The references hold it where it must land in the
// buffer: once for test 2, and for test 3 once per corner, each copy cut to the buffer.
static void buildPictures(void)
{
	picture(vbo + V_PIC, 0, 0);
	picture(vbo + V_REF2, POS_X, POS_Y);

	vertex* v = vbo + V_REF3;
	for (int i = 0; i < 4; i++)
		v = picture(v, STRADDLE - ((i & 1) ? BUF : 0), STRADDLE - ((i & 2) ? BUF : 0));
	ref3Verts = v - (vbo + V_REF3);
}

// What the top screen shows of the buffer: all of it on the left, the picture's place at 1:1 on the right.
static void buildViews(void)
{
	const float x = 264, y = 56, h = PIC / 2;

	vertex* v = vbo + V_VIEW2;
	v = view(v, 0, 0, 240, 240, 0, 0, BUF, BUF);
	view(v, x, y, x + PIC, y + PIC, POS_X, POS_Y, POS_X + PIC, POS_Y + PIC);

	// At 960,960 the picture's four quarters wrap into the four corners of the buffer.
	v = vbo + V_VIEW3;
	v = view(v, 0, 0, 240, 240, 0, 0, BUF, BUF);
	v = view(v, x,     y,     x + h,   y + h,   BUF - h, BUF - h, BUF, BUF);
	v = view(v, x + h, y,     x + PIC, y + h,   0,       BUF - h, h,   BUF);
	v = view(v, x,     y + h, x + h,   y + PIC, BUF - h, 0,       BUF, h);
	view(v, x + h, y + h, x + PIC, y + PIC, 0, 0, h, h);

	v = vbo + V_FRAME;
	v = quad(v, x - 2, y - 2, x, y + PIC + 2, 1, 1, 1);
	v = quad(v, x + PIC, y - 2, x + PIC + 2, y + PIC + 2, 1, 1, 1);
	v = quad(v, x, y - 2, x + PIC, y, 1, 1, 1);
	v = quad(v, x, y + PIC, x + PIC, y + PIC + 2, 1, 1, 1);
	quad(v, 240, 0, 242, 240, 1, 1, 1);
}

static void useColour(void)
{
	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
}

static void useTexture(void)
{
	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, 0, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
}

static void draw(const C3D_Mtx* projection, int first, int count)
{
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_projection, projection);
	C3D_DrawArrays(GPU_TRIANGLES, first, count);
}

static void sceneInit(void)
{
	vshader_dvlb = DVLB_ParseFile((u32*)vshader_shbin, vshader_shbin_size);
	shaderProgramInit(&program);
	shaderProgramSetVsh(&program, &vshader_dvlb->DVLE[0]);
	C3D_BindProgram(&program);
	uLoc_projection = shaderInstanceGetUniformLocation(program.vertexShader, "projection");

	C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3); // v0=position
	AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 4); // v1=color
	AttrInfo_AddLoader(attrInfo, 2, GPU_FLOAT, 2); // v2=texcoord

	vbo = linearAlloc(sizeof(vertex) * TOTAL_VERTS);
	buildWorld();
	buildHud();
	buildPictures();
	buildViews();

	C3D_BufInfo* bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, vbo, sizeof(vertex), 3, 0x210);

	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	C3D_CullFace(GPU_CULL_NONE);
}

static u32 seed = 20261123;
static int rnd(int range)
{
	seed = seed * 1103515245u + 12345u;
	return (int)((seed >> 16) % (u32)range);
}

static int step(int from, int to, int speed)
{
	if (to > from + speed) return from + speed;
	if (to < from - speed) return from - speed;
	return to;
}

static void staticText(int test)
{
	consoleClear();
	printf("\x1b[1;1HViewport origin test              %d/%d", test + 1, TEST_COUNT);
	if (test == TEST_NEGATIVE)
	{
		printf("\x1b[3;1HA negative origin.");
		printf("\x1b[4;1HAn 800x480 picture behind the 400x240");
		printf("\x1b[5;1Htop screen. Reach the diamond to score.");
		printf("\x1b[15;1HA and B must look the same on a console");
		printf("\x1b[16;1Hif a negative viewport origin works.");
		printf("\x1b[17;1HIn B the viewport is 240x400 at 0,0 and");
		printf("\x1b[18;1Hthe projection is shifted instead.");
		printf("\x1b[21;1HCircle pad: scroll");
		printf("\x1b[22;1HX: mode A/B   Y: auto/manual");
	}
	else if (test == TEST_POSITION)
	{
		printf("\x1b[3;1HA position past 511.");
		printf("\x1b[4;1HA 128x128 picture drawn at 512,768 in");
		printf("\x1b[5;1Ha 1024x1024 buffer.");
		printf("\x1b[13;1HLeft: the whole buffer.");
		printf("\x1b[14;1HRight: where the picture belongs, 1:1.");
		printf("\x1b[16;1HA must match B on a console.");
		printf("\x1b[17;1HIn B the viewport is the whole buffer");
		printf("\x1b[18;1Hat 0,0 and the vertices are moved.");
		printf("\x1b[22;1HX: mode A/B   Y: scissor");
	}
	else
	{
		printf("\x1b[3;1HStraddling the 1024 edge.");
		printf("\x1b[4;1HA 128x128 picture drawn at 960,960 in");
		printf("\x1b[5;1Ha 1024x1024 buffer. If the origin wraps");
		printf("\x1b[6;1Hits quarters land in the four corners.");
		printf("\x1b[13;1HLeft: the whole buffer.");
		printf("\x1b[14;1HRight: the four corners put together.");
		printf("\x1b[16;1HA matches B only if the origin wraps.");
		printf("\x1b[17;1HIn B the picture is drawn four times,");
		printf("\x1b[18;1Honce per corner, viewport at 0,0.");
		printf("\x1b[22;1HX: mode A/B   Y: scissor");
	}
	if (test != TEST_NEGATIVE && !haveBuffer)
		printf("\x1b[20;1HNo memory for the 1024x1024 buffer.");
	printf("\x1b[23;1HD-pad left/right: test   START: exit");
}

// Test 1. The scroll is the picture coordinate of the screen's lower left corner. It starts where the
// viewport origin is 0,0: the one place every reading agrees on.
static int sx = WORLD_W - SCREEN_W, sy = 0;
static int tx = 0, ty = 0, score = 0, hold = 0;
static bool automatic = true, newTarget = true;

static void frameNegative(bool originScroll, u32 kDown, u32 kHeld)
{
	if (kDown & KEY_Y) automatic = !automatic;

	if (newTarget)
	{
		tx = SCREEN_W / 2 + rnd(WORLD_W - SCREEN_W + 1);
		ty = SCREEN_H / 2 + rnd(WORLD_H - SCREEN_H + 1);
		buildTarget(tx, ty);
		newTarget = false;
	}

	if (kHeld & (KEY_CPAD_LEFT | KEY_CPAD_RIGHT | KEY_CPAD_UP | KEY_CPAD_DOWN)) automatic = false;
	if (automatic)
	{
		sx = step(sx, tx - SCREEN_W / 2, 2);
		sy = step(sy, ty - SCREEN_H / 2, 2);
	}
	else
	{
		if (kHeld & KEY_CPAD_LEFT)  sx -= 3;
		if (kHeld & KEY_CPAD_RIGHT) sx += 3;
		if (kHeld & KEY_CPAD_DOWN)  sy -= 3;
		if (kHeld & KEY_CPAD_UP)    sy += 3;
	}
	if (sx < 0) sx = 0;
	if (sx > WORLD_W - SCREEN_W) sx = WORLD_W - SCREEN_W;
	if (sy < 0) sy = 0;
	if (sy > WORLD_H - SCREEN_H) sy = WORLD_H - SCREEN_H;

	if (abs(tx - (sx + SCREEN_W / 2)) < 4 && abs(ty - (sy + SCREEN_H / 2)) < 4)
	{
		if (++hold == 45) { score++; hold = 0; newTarget = true; }
	}
	else hold = 0;

	// The screen is turned: the framebuffer's x runs up the screen and its y runs from right to left.
	const int originX = -sy, originY = sx - (WORLD_W - SCREEN_W);
	const u32 regX = (u32)originX & 0x3FF, regY = (u32)originY & 0x3FF;

	printf("\x1b[7;1Hmode   %s", originScroll ? "A  origin scroll    " : "B  projection scroll");
	printf("\x1b[8;1Hscroll x=%-4d y=%-4d  %s", sx, sy, automatic ? "auto  " : "manual");
	if (originScroll)
	{
		printf("\x1b[9;1Hviewport  480x800 at x=%-5d y=%-5d", originX, originY);
		printf("\x1b[10;1HGPUREG_VIEWPORT_XY = 0x%08lX", (unsigned long)((regY << 16) | regX));
		printf("\x1b[11;1Hfields    x=%-5lu y=%-5lu", (unsigned long)regX, (unsigned long)regY);
	}
	else
	{
		printf("\x1b[9;1Hviewport  240x400 at x=0     y=0    ");
		printf("\x1b[10;1HGPUREG_VIEWPORT_XY = 0x00000000");
		printf("\x1b[11;1Hfields    x=0     y=0    ");
	}
	printf("\x1b[13;1Hscore  %d", score);

	C3D_Mtx projection;
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		C3D_RenderTargetClear(top, C3D_CLEAR_ALL, 0x400000FF, 0);
		C3D_FrameDrawOn(top);
		useColour();

		if (originScroll)
		{
			Mtx_OrthoTilt(&projection, 0, WORLD_W, 0, WORLD_H, 0, 1, true);
			C3D_SetViewport(regX, regY, WORLD_H, WORLD_W);
		}
		else
		{
			Mtx_OrthoTilt(&projection, sx, sx + SCREEN_W, sy, sy + SCREEN_H, 0, 1, true);
			C3D_SetViewport(0, 0, 240, 400);
		}
		C3D_SetScissor(GPU_SCISSOR_NORMAL, 0, 0, 240, 400);
		draw(&projection, V_WORLD, WORLD_VERTS + TARGET_VERTS);

		Mtx_OrthoTilt(&projection, 0, SCREEN_W, 0, SCREEN_H, 0, 1, true);
		C3D_SetViewport(0, 0, 240, 400);
		C3D_SetScissor(GPU_SCISSOR_NORMAL, 0, 0, 240, 400);
		draw(&projection, V_HUD, HUD_VERTS);
	C3D_FrameEnd(0);
}

// Tests 2 and 3. The picture goes into the buffer, then the top screen shows the buffer.
static void frameBuffer(int test, bool atOrigin, bool scissor)
{
	const int origin[2] = { test == TEST_POSITION ? POS_X : STRADDLE, test == TEST_POSITION ? POS_Y : STRADDLE };

	printf("\x1b[8;1Hmode   %s", atOrigin ? "A  viewport at the position" : "B  vertices at the position");
	if (atOrigin)
	{
		printf("\x1b[9;1Hviewport  128x128 at x=%-4d y=%-4d  ", origin[0], origin[1]);
		printf("\x1b[10;1HGPUREG_VIEWPORT_XY = 0x%08lX", (unsigned long)(((u32)origin[1] << 16) | (u32)origin[0]));
	}
	else
	{
		printf("\x1b[9;1Hviewport  1024x1024 at x=0    y=0   ");
		printf("\x1b[10;1HGPUREG_VIEWPORT_XY = 0x00000000");
	}
	printf("\x1b[11;1Hscissor   %s", scissor ? "0,0 to 1024,1024" : "off             ");

	C3D_Mtx projection;
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		C3D_RenderTargetClear(bufTarget, C3D_CLEAR_COLOR, 0x2104, 0);
		C3D_RenderTargetClear(top, C3D_CLEAR_ALL, 0x400000FF, 0);

		C3D_FrameDrawOn(bufTarget);
		useColour();
		if (atOrigin)
		{
			Mtx_Ortho(&projection, 0, PIC, 0, PIC, 0, 1, true);
			C3D_SetViewport(origin[0], origin[1], PIC, PIC);
			if (scissor) C3D_SetScissor(GPU_SCISSOR_NORMAL, 0, 0, BUF, BUF);
			draw(&projection, V_PIC, PIC_VERTS);
		}
		else
		{
			Mtx_Ortho(&projection, 0, BUF, 0, BUF, 0, 1, true);
			C3D_SetViewport(0, 0, BUF, BUF);
			if (scissor) C3D_SetScissor(GPU_SCISSOR_NORMAL, 0, 0, BUF, BUF);
			if (test == TEST_POSITION) draw(&projection, V_REF2, PIC_VERTS);
			else draw(&projection, V_REF3, ref3Verts);
		}

		C3D_FrameDrawOn(top);
		Mtx_OrthoTilt(&projection, 0, SCREEN_W, 0, SCREEN_H, 0, 1, true);
		C3D_SetScissor(GPU_SCISSOR_NORMAL, 0, 0, 240, 400);
		useTexture();
		C3D_TexBind(0, &bufTex);
		if (test == TEST_POSITION) draw(&projection, V_VIEW2, VIEW2_VERTS);
		else draw(&projection, V_VIEW3, VIEW3_VERTS);
		useColour();
		draw(&projection, V_FRAME, FRAME_VERTS);
	C3D_FrameEnd(0);
}

static void frameEmpty(void)
{
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		C3D_RenderTargetClear(top, C3D_CLEAR_ALL, 0x400000FF, 0);
		C3D_FrameDrawOn(top);
	C3D_FrameEnd(0);
}

int main()
{
	gfxInitDefault();
	consoleInit(GFX_BOTTOM, NULL);
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);

	top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(top, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

	if (C3D_TexInitVRAM(&bufTex, BUF, BUF, GPU_RGB565))
	{
		C3D_TexSetFilter(&bufTex, GPU_NEAREST, GPU_NEAREST);
		C3D_TexSetWrap(&bufTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
		bufTarget = C3D_RenderTargetCreateFromTex(&bufTex, GPU_TEXFACE_2D, 0, -1);
		haveBuffer = bufTarget != NULL;
	}

	sceneInit();

	int test = TEST_NEGATIVE;
	bool modeA = true, scissor = false;
	staticText(test);

	while (aptMainLoop())
	{
		hidScanInput();
		const u32 kDown = hidKeysDown(), kHeld = hidKeysHeld();
		if (kDown & KEY_START) break;
		if (kDown & KEY_X) modeA = !modeA;
		if (kDown & (KEY_DLEFT | KEY_DRIGHT))
		{
			test = (test + ((kDown & KEY_DRIGHT) ? 1 : TEST_COUNT - 1)) % TEST_COUNT;
			modeA = true;
			scissor = false;
			staticText(test);
		}

		if (test == TEST_NEGATIVE) frameNegative(modeA, kDown, kHeld);
		else if (haveBuffer)
		{
			if (kDown & KEY_Y) scissor = !scissor;
			frameBuffer(test, modeA, scissor);
		}
		else frameEmpty();
	}

	linearFree(vbo);
	if (haveBuffer)
	{
		C3D_RenderTargetDelete(bufTarget);
		C3D_TexDelete(&bufTex);
	}
	shaderProgramFree(&program);
	DVLB_Free(vshader_dvlb);
	C3D_Fini();
	gfxExit();
	return 0;
}
