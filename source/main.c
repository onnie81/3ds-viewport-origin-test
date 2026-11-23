// Viewport origin test: an 800x480 picture seen through the 400x240 top screen.
// Mode A scrolls by moving the viewport origin, which is negative everywhere but at the start.
// Mode B draws the same view with a plain 240x400 viewport at 0,0 and a shifted projection.
// On a console the two modes match only if GPUREG_VIEWPORT_XY takes a negative origin.

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

#define WORLD_VERTS  (COLS * ROWS * 12)
#define TARGET_VERTS 12
#define HUD_VERTS    24
#define TOTAL_VERTS  (WORLD_VERTS + TARGET_VERTS + HUD_VERTS)

typedef struct { float x, y, z, r, g, b, a; } vertex;

static DVLB_s* vshader_dvlb;
static shaderProgram_s program;
static int uLoc_projection;
static vertex* vbo;

static vertex* quad(vertex* v, float x0, float y0, float x1, float y1, float r, float g, float b)
{
	const float xs[6] = { x0, x1, x1, x0, x1, x0 };
	const float ys[6] = { y0, y0, y1, y0, y1, y1 };
	for (int i = 0; i < 6; i++)
		*v++ = (vertex){ xs[i], ys[i], 0.5f, r, g, b, 1.0f };
	return v;
}

static vertex* diamond(vertex* v, float cx, float cy, float size, float r, float g, float b)
{
	const float xs[6] = { cx - size, cx, cx + size, cx - size, cx + size, cx };
	const float ys[6] = { cy, cy - size, cy, cy, cy, cy + size };
	for (int i = 0; i < 6; i++)
		*v++ = (vertex){ xs[i], ys[i], 0.5f, r, g, b, 1.0f };
	return v;
}

// Every cell has its own colour, so any part of the picture tells where it is.
static void buildWorld(void)
{
	vertex* v = vbo;
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
	vertex* v = vbo + WORLD_VERTS;
	v = diamond(v, tx, ty, 16, 0.0f, 0.0f, 0.0f);
	diamond(v, tx, ty, 12, 1.0f, 0.9f, 0.1f);
}

// Four bars around the middle of the screen, in screen coordinates.
static void buildHud(void)
{
	vertex* v = vbo + WORLD_VERTS + TARGET_VERTS;
	const float cx = SCREEN_W / 2, cy = SCREEN_H / 2;
	v = quad(v, cx - 26, cy - 1, cx - 10, cy + 1, 1, 1, 1);
	v = quad(v, cx + 10, cy - 1, cx + 26, cy + 1, 1, 1, 1);
	v = quad(v, cx - 1, cy - 26, cx + 1, cy - 10, 1, 1, 1);
	quad(v, cx - 1, cy + 10, cx + 1, cy + 26, 1, 1, 1);
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

	vbo = linearAlloc(sizeof(vertex) * TOTAL_VERTS);
	buildWorld();
	buildHud();

	C3D_BufInfo* bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, vbo, sizeof(vertex), 2, 0x10);

	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

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

int main()
{
	gfxInitDefault();
	consoleInit(GFX_BOTTOM, NULL);
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);

	C3D_RenderTarget* top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(top, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

	sceneInit();

	// The scroll is the picture coordinate of the screen's lower left corner.
	// It starts where the viewport origin is 0,0: the one place every reading agrees on.
	int sx = WORLD_W - SCREEN_W, sy = 0;
	int tx = 0, ty = 0, score = 0, hold = 0;
	bool originScroll = true, automatic = true, newTarget = true;

	printf("\x1b[1;1HViewport origin test");
	printf("\x1b[3;1HAn 800x480 picture behind the 400x240");
	printf("\x1b[4;1Htop screen. Put the cross on the diamond.");
	printf("\x1b[15;1HA and B must look the same on a console");
	printf("\x1b[16;1Hif a negative viewport origin works.");
	printf("\x1b[17;1HIn B the viewport is 240x400 at 0,0 and");
	printf("\x1b[18;1Hthe projection is shifted instead.");
	printf("\x1b[21;1HD-pad or circle pad: scroll");
	printf("\x1b[22;1HX: mode A/B   Y: auto/manual");
	printf("\x1b[23;1HSTART: exit");

	while (aptMainLoop())
	{
		hidScanInput();
		const u32 kDown = hidKeysDown(), kHeld = hidKeysHeld();
		if (kDown & KEY_START) break;
		if (kDown & KEY_X) originScroll = !originScroll;
		if (kDown & KEY_Y) automatic = !automatic;

		if (newTarget)
		{
			tx = SCREEN_W / 2 + rnd(WORLD_W - SCREEN_W + 1);
			ty = SCREEN_H / 2 + rnd(WORLD_H - SCREEN_H + 1);
			buildTarget(tx, ty);
			newTarget = false;
		}

		if (kHeld & (KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN)) automatic = false;
		if (automatic)
		{
			sx = step(sx, tx - SCREEN_W / 2, 2);
			sy = step(sy, ty - SCREEN_H / 2, 2);
		}
		else
		{
			if (kHeld & KEY_LEFT)  sx -= 3;
			if (kHeld & KEY_RIGHT) sx += 3;
			if (kHeld & KEY_DOWN)  sy -= 3;
			if (kHeld & KEY_UP)    sy += 3;
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

		printf("\x1b[6;1Hmode   %s", originScroll ? "A  origin scroll    " : "B  projection scroll");
		printf("\x1b[7;1Hscroll x=%-4d y=%-4d  %s", sx, sy, automatic ? "auto  " : "manual");
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
			C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_projection, &projection);
			C3D_DrawArrays(GPU_TRIANGLES, 0, WORLD_VERTS + TARGET_VERTS);

			Mtx_OrthoTilt(&projection, 0, SCREEN_W, 0, SCREEN_H, 0, 1, true);
			C3D_SetViewport(0, 0, 240, 400);
			C3D_SetScissor(GPU_SCISSOR_NORMAL, 0, 0, 240, 400);
			C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_projection, &projection);
			C3D_DrawArrays(GPU_TRIANGLES, WORLD_VERTS + TARGET_VERTS, HUD_VERTS);
		C3D_FrameEnd(0);
	}

	linearFree(vbo);
	shaderProgramFree(&program);
	DVLB_Free(vshader_dvlb);
	C3D_Fini();
	gfxExit();
	return 0;
}
