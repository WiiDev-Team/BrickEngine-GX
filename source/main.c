#include <grrlib.h>
#include <fat.h>
#include <ogc/system.h>
#include <wiiuse/wpad.h>
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define BASE_DIR    "sd:/apps/wiigame/"
#define LEVEL_DIR   BASE_DIR "levels"

#define MAX_LEVELS  32
#define MAX_TEX     16
#define MAX_DIM     64
#define EYE_HEIGHT  0.6f
#define MOVE_SPEED  0.06f
#define TURN_SPEED  0.04f
#define PLAYER_R    0.25f
#define GRAVITY     0.012f  /* per frame^2 */
#define JUMP_VEL    0.173f  /* peak jump ~1.25 units: enough to hop onto height-1 blocks */
#define DRAW_DIST   24      /* cells; keeps 64x64 levels fast */
#define USE_FOG     1       /* fades distant geometry into the sky colour; set 0 to disable */

/* ------------------------------------------------------------------ */
/* Level data                                                          */
/* ------------------------------------------------------------------ */
typedef struct { u8 h, tex; } Cell;          /* h == 0 -> empty cell */

typedef struct {
    int   w, d;                              /* size in cells (x, z) */
    u32   sky;                               /* RGBA clear colour    */
    int   floorTex;
    float spawnX, spawnZ, spawnYaw;          /* yaw in degrees       */
    GRRLIB_texImg *tex[MAX_TEX];
    Cell  cells[MAX_DIM][MAX_DIM];           /* [z][x]               */
} Level;

static Level lvl;

/* Player state */
static float px, pz, yaw;                    /* position on the floor plane + heading */
static float py, vy;                         /* feet height and vertical velocity     */
static int   onGround = 1;
static int   curLevel = 0;

/* Level list (filled by ScanLevels) */
static char        levelFile[MAX_LEVELS][64];
static char        levelLabel[MAX_LEVELS][28];
static const char *levelLabelPtr[MAX_LEVELS];
static int         numLevels = 0;

static void Log(const char *msg)
{
    FILE *f = fopen(BASE_DIR "log.txt", "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}

/* Frees every loaded texture once (duplicate fallback pointers are only freed once) */
static void UnloadLevel(void)
{
    for (int i = 0; i < MAX_TEX; i++) {
        GRRLIB_texImg *t = lvl.tex[i];
        if (!t) continue;
        for (int j = i; j < MAX_TEX; j++)
            if (lvl.tex[j] == t) lvl.tex[j] = NULL;
        GRRLIB_FreeTexture(t);
    }
}

/* Returns 1 = ok, 0 = level file could not be opened, 2 = no texture could be loaded */
static int LoadLevel(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { Log("Cannot open level file"); Log(path); return 0; }

    memset(&lvl, 0, sizeof(lvl));
    lvl.w = lvl.d = 16;
    lvl.sky = RGBA(100, 150, 255, 255);
    lvl.spawnX = lvl.spawnZ = 1.5f;

    char line[256], file[200], full[300];
    while (fgets(line, sizeof(line), f)) {
        int a, b, c, d;
        float x, y, z;

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        if (sscanf(line, "SIZE %d %d", &a, &b) == 2) {
            lvl.w = a > MAX_DIM ? MAX_DIM : a;
            lvl.d = b > MAX_DIM ? MAX_DIM : b;
        } else if (sscanf(line, "SKY %d %d %d", &a, &b, &c) == 3) {
            lvl.sky = RGBA(a, b, c, 255);
        } else if (sscanf(line, "FLOORTEX %d", &a) == 1) {
            lvl.floorTex = (a >= 0 && a < MAX_TEX) ? a : 0;
        } else if (sscanf(line, "SPAWN %f %f %f", &x, &z, &y) == 3) {
            lvl.spawnX = x; lvl.spawnZ = z; lvl.spawnYaw = y;
        } else if (sscanf(line, "TEX %d %199s", &a, file) == 2) {
            if (a >= 0 && a < MAX_TEX) {
                snprintf(full, sizeof(full), BASE_DIR "%s", file);
                lvl.tex[a] = GRRLIB_LoadTextureFromFile(full);
                if (!lvl.tex[a]) Log(full);
            }
        } else if (sscanf(line, "CELL %d %d %d %d", &a, &b, &c, &d) == 4) {
            if (a >= 0 && a < lvl.w && b >= 0 && b < lvl.d) {
                lvl.cells[b][a].h   = (u8)c;
                lvl.cells[b][a].tex = (u8)((d >= 0 && d < MAX_TEX) ? d : 0);
            }
        }
    }
    fclose(f);

    /* Any texture that failed to load falls back to the first one that worked */
    GRRLIB_texImg *fallback = NULL;
    for (int i = 0; i < MAX_TEX; i++)
        if (lvl.tex[i]) { fallback = lvl.tex[i]; break; }
    if (!fallback) { Log("No textures could be loaded"); return 2; }
    for (int i = 0; i < MAX_TEX; i++)
        if (!lvl.tex[i]) lvl.tex[i] = fallback;

    return 1;
}

/* Sort so that level2 comes before level10 */
static int CmpNatural(const void *pa, const void *pb)
{
    const char *a = (const char *)pa, *b = (const char *)pb;
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            unsigned long na = strtoul(a, (char **)&a, 10);
            unsigned long nb = strtoul(b, (char **)&b, 10);
            if (na != nb) return na < nb ? -1 : 1;
        } else {
            int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
            if (ca != cb) return ca - cb;
            a++; b++;
        }
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static void ScanLevels(void)
{
    numLevels = 0;
    DIR *dir = opendir(LEVEL_DIR);
    if (!dir) { Log("Cannot open levels folder"); return; }

    struct dirent *e;
    while ((e = readdir(dir)) != NULL && numLevels < MAX_LEVELS) {
        size_t len = strlen(e->d_name);
        if (e->d_name[0] == '.' || len < 5 || len >= sizeof(levelFile[0])) continue;
        if (strcasecmp(e->d_name + len - 4, ".lvl") != 0) continue;
        strcpy(levelFile[numLevels++], e->d_name);
    }
    closedir(dir);

    qsort(levelFile, numLevels, sizeof(levelFile[0]), CmpNatural);

    for (int i = 0; i < numLevels; i++) {
        size_t n = strlen(levelFile[i]) - 4;                 /* strip ".lvl" */
        if (n > sizeof(levelLabel[i]) - 1) n = sizeof(levelLabel[i]) - 1;
        for (size_t k = 0; k < n; k++)
            levelLabel[i][k] = (char)toupper((unsigned char)levelFile[i][k]);
        levelLabel[i][n] = '\0';
        levelLabelPtr[i] = levelLabel[i];
    }
}

/* ------------------------------------------------------------------ */
/* Collision                                                           */
/* ------------------------------------------------------------------ */
static int CellHeight(int x, int z)
{
    if (x < 0 || z < 0 || x >= lvl.w || z >= lvl.d) return 0;
    return lvl.cells[z][x].h;
}

static int Solid(float x, float z)
{
    int cx = (int)floorf(x), cz = (int)floorf(z);
    if (cx < 0 || cz < 0 || cx >= lvl.w || cz >= lvl.d) return 1; /* outside = wall */
    return lvl.cells[cz][cx].h > py + 0.01f;   /* walkable if you stand on/above it */
}

static int Blocked(float x, float z)
{
    return Solid(x - PLAYER_R, z - PLAYER_R) || Solid(x + PLAYER_R, z - PLAYER_R) ||
           Solid(x - PLAYER_R, z + PLAYER_R) || Solid(x + PLAYER_R, z + PLAYER_R);
}

/* Height of the highest block under the player's footprint (0 = bare floor) */
static float GroundHeight(float x, float z)
{
    float g = 0;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            int h = CellHeight((int)floorf(x + (i ? PLAYER_R : -PLAYER_R)),
                               (int)floorf(z + (j ? PLAYER_R : -PLAYER_R)));
            if (h > g) g = (float)h;
        }
    return g;
}

/* ------------------------------------------------------------------ */
/* 3D rendering                                                        */
/* ------------------------------------------------------------------ */
static void Quad(const float p[4][3], const float uv[4][2])
{
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    for (int i = 0; i < 4; i++) {
        GX_Position3f32(p[i][0], p[i][1], p[i][2]);
        GX_Color1u32(0xFFFFFFFF);
        GX_TexCoord2f32(uv[i][0], uv[i][1]);
    }
    GX_End();
}

/* Vertical face between (x0,z0) and (x1,z1), from y0 up to y1 */
static void Wall(float x0, float z0, float x1, float z1, float y0, float y1)
{
    float v = y1 - y0;
    float p[4][3]  = { {x0, y1, z0}, {x1, y1, z1}, {x1, y0, z1}, {x0, y0, z0} };
    float uv[4][2] = { {0, 0}, {1, 0}, {1, v}, {0, v} };
    Quad(p, uv);
}

static void Top(float x, float z, float y)
{
    float p[4][3]  = { {x, y, z}, {x + 1, y, z}, {x + 1, y, z + 1}, {x, y, z + 1} };
    float uv[4][2] = { {0, 0}, {1, 0}, {1, 1}, {0, 1} };
    Quad(p, uv);
}

static void Floor(void)
{
    float w = (float)lvl.w, d = (float)lvl.d;
    float p[4][3]  = { {0, 0, 0}, {w, 0, 0}, {w, 0, d}, {0, 0, d} };
    float uv[4][2] = { {0, 0}, {w, 0}, {w, d}, {0, d} };
    Quad(p, uv);
}

/* cx,cz = camera position, fx,fz = camera forward (used to skip hidden cells) */
static void DrawLevel(float cx, float cz, float fx, float fz)
{
    /* Texture repeat (rep = true) needs power-of-two PNG sizes (e.g. 64x64) */
    GRRLIB_SetTexture(lvl.tex[lvl.floorTex], true);
    Floor();

    int x0 = (int)floorf(cx) - DRAW_DIST, x1 = (int)floorf(cx) + DRAW_DIST;
    int z0 = (int)floorf(cz) - DRAW_DIST, z1 = (int)floorf(cz) + DRAW_DIST;
    if (x0 < 0) x0 = 0;
    if (z0 < 0) z0 = 0;
    if (x1 >= lvl.w) x1 = lvl.w - 1;
    if (z1 >= lvl.d) z1 = lvl.d - 1;

    int last = -1;
    for (int z = z0; z <= z1; z++) {
        for (int x = x0; x <= x1; x++) {
            int h = lvl.cells[z][x].h;
            if (!h) continue;

            float ddx = x + 0.5f - cx, ddz = z + 0.5f - cz;
            if (ddx * ddx + ddz * ddz > (float)(DRAW_DIST * DRAW_DIST)) continue; /* too far     */
            if (ddx * fx + ddz * fz < -1.0f) continue;                            /* behind view */

            int t = lvl.cells[z][x].tex;
            if (t != last) { GRRLIB_SetTexture(lvl.tex[t], true); last = t; }

            Top(x, z, h);
            int n;
            if ((n = CellHeight(x, z - 1)) < h) Wall(x,     z,     x + 1, z,     n, h); /* north */
            if ((n = CellHeight(x, z + 1)) < h) Wall(x + 1, z + 1, x,     z + 1, n, h); /* south */
            if ((n = CellHeight(x - 1, z)) < h) Wall(x,     z + 1, x,     z,     n, h); /* west  */
            if ((n = CellHeight(x + 1, z)) < h) Wall(x + 1, z,     x + 1, z + 1, n, h); /* east  */
        }
    }
}

/* ------------------------------------------------------------------ */
/* Tiny built-in 5x7 bitmap font (uppercase, digits, - _ . : , and space) */
/* ------------------------------------------------------------------ */
static const char FONT_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.:, ";
static const u8 FONT[][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* A B */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* C D */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* E F */
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* G H */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* I J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* K L */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* M N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* O P */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* Q R */
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* S T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* U V */
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* W X */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Y Z */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 0 1 */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}, /* 2 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 4 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 6 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 8 9 */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, /* - _ */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, /* . : */
    {0x00,0x00,0x00,0x00,0x0C,0x04,0x08},                                       /* , */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},                                       /* space */
};

static int TextWidth(int scale, const char *t)
{
    int n = (int)strlen(t);
    return n ? n * 6 * scale - scale : 0;
}

static void DrawText(int x, int y, int scale, u32 color, const char *t)
{
    const int space = (int)sizeof(FONT_CHARS) - 2;
    for (; *t; t++, x += 6 * scale) {
        const char *p = strchr(FONT_CHARS, toupper((unsigned char)*t));
        int g = p ? (int)(p - FONT_CHARS) : space;
        for (int r = 0; r < 7; r++) {
            u8 bits = FONT[g][r];
            for (int c = 0; c < 5; ) {
                if (!(bits & (0x10 >> c))) { c++; continue; }
                int end = c;
                while (end < 5 && (bits & (0x10 >> end))) end++;   /* draw runs, not pixels */
                GRRLIB_Rectangle(x + c * scale, y + r * scale, (end - c) * scale, scale, color, true);
                c = end;
            }
        }
    }
}

static void DrawTextCentered(int y, int scale, u32 color, const char *t)
{
    DrawText((640 - TextWidth(scale, t)) / 2, y, scale, color, t);
}

/* ------------------------------------------------------------------ */
/* Menus                                                               */
/* ------------------------------------------------------------------ */
/* Returns the chosen index, or -1 if the player pressed B or HOME. */
static int Menu(const char *title, const char **items, int n, int sel)
{
    const int VISIBLE = 9;
    if (sel < 0 || sel >= n) sel = 0;

    while (1) {
        WPAD_ScanPads();
        u32 down = WPAD_ButtonsDown(0);
        if (down & (WPAD_BUTTON_HOME | WPAD_BUTTON_B)) return -1;
        if (down & WPAD_BUTTON_A)    return sel;
        if (down & WPAD_BUTTON_UP)   sel = (sel + n - 1) % n;
        if (down & WPAD_BUTTON_DOWN) sel = (sel + 1) % n;

        int first = sel - VISIBLE / 2;
        if (first > n - VISIBLE) first = n - VISIBLE;
        if (first < 0) first = 0;

        GRRLIB_2dMode();
        GRRLIB_FillScreen(RGBA(16, 16, 32, 255));
        DrawTextCentered(40, 4, RGBA(255, 255, 255, 255), title);

        for (int i = 0; i < VISIBLE && first + i < n; i++) {
            int y = 130 + i * 36;
            int isSel = (first + i == sel);
            if (isSel) GRRLIB_Rectangle(70, y - 8, 500, 36, RGBA(60, 60, 140, 255), true);
            DrawTextCentered(y, 3, isSel ? RGBA(255, 220, 0, 255) : RGBA(170, 170, 190, 255),
                             items[first + i]);
        }
        if (first > 0)             DrawTextCentered(108, 2, RGBA(170, 170, 190, 255), "...");
        if (first + VISIBLE < n)   DrawTextCentered(130 + VISIBLE * 36, 2, RGBA(170, 170, 190, 255), "...");

        DrawTextCentered(440, 2, RGBA(150, 150, 170, 255), "A: SELECT   B: BACK");
        GRRLIB_Render();
    }
}

/* Shows a solid colour until HOME is pressed (no font needed to report errors) */
static void ErrorScreen(u32 color)
{
    while (1) {
        WPAD_ScanPads();
        if (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME) break;
        GRRLIB_FillScreen(color);
        GRRLIB_Render();
    }
    GRRLIB_Exit();
    exit(1);
}

static void QuitToMenu(void)
{
    UnloadLevel();
    GRRLIB_Exit();
    SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
    exit(0);
}

static void StartLevel(int idx)
{
    char path[160];
    snprintf(path, sizeof(path), LEVEL_DIR "/%s", levelFile[idx]);

    UnloadLevel();
    int r = LoadLevel(path);
    if (r == 0) ErrorScreen(RGBA(255, 0, 0, 255));   /* red:  level file not found / unreadable */
    if (r == 2) ErrorScreen(RGBA(0, 0, 255, 255));   /* blue: level found, but no PNG texture loaded */

    px  = lvl.spawnX;
    pz  = lvl.spawnZ;
    yaw = lvl.spawnYaw * (float)M_PI / 180.0f;
    py  = GroundHeight(px, pz);
    vy  = 0;
    onGround = 1;
    curLevel = idx;
}

static void PauseMenu(void)
{
    const char *opts[] = { "LAUNCH DIFFERENT LEVEL", "RETURN TO WII MENU" };
    int c = Menu("PAUSED", opts, 2, 0);
    if (c == 1) QuitToMenu();
    if (c == 0) {
        int l = Menu("SELECT LEVEL", levelLabelPtr, numLevels, curLevel);
        if (l >= 0) StartLevel(l);
    }                                              /* B/HOME in a menu = resume */
}

static int NunchukConnected(void)
{
    u32 ext = 0;
    return WPAD_Probe(0, &ext) == WPAD_ERR_NONE && ext == WPAD_EXP_NUNCHUK;
}

/* Shown whenever no Nunchuk is attached. Returns once one is connected AND C is pressed.
   HOME still opens the pause menu so the player can always leave. */
static void NunchukScreen(void)
{
    while (1) {
        WPAD_ScanPads();
        u32 down = WPAD_ButtonsDown(0);
        int ok = NunchukConnected();

        if (down & WPAD_BUTTON_HOME) { PauseMenu(); continue; }
        if (ok && (down & WPAD_NUNCHUK_BUTTON_C)) return;

        GRRLIB_2dMode();
        GRRLIB_FillScreen(RGBA(60, 10, 10, 255));
        DrawTextCentered(150, 3, RGBA(255, 255, 255, 255), "PLEASE CONNECT A NUNCHUCK,");
        DrawTextCentered(200, 3, RGBA(255, 255, 255, 255), "THEN PRESS C");
        if (ok) DrawTextCentered(300, 2, RGBA(120, 255, 120, 255), "NUNCHUCK DETECTED - PRESS C");
        DrawTextCentered(440, 2, RGBA(170, 150, 150, 255), "HOME: MENU");
        GRRLIB_Render();
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    if (GRRLIB_Init()) return 1;
    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);

    if (!fatInitDefault()) ErrorScreen(RGBA(255, 200, 0, 255));   /* yellow: no SD card */

    ScanLevels();
    if (numLevels == 0) ErrorScreen(RGBA(255, 0, 0, 255));        /* red: no .lvl files found */

    int first = 0;
    if (numLevels > 1) {
        first = Menu("SELECT LEVEL", levelLabelPtr, numLevels, 0);
        if (first < 0) QuitToMenu();
    }
    StartLevel(first);

    while (1) {
        WPAD_ScanPads();

        /* ---- pause menu / Nunchuk required ---- */
        if (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME) { PauseMenu(); continue; }
        if (!NunchukConnected())                    { NunchukScreen(); continue; }

        /* ---- input ---- */
        u32 held = WPAD_ButtonsHeld(0);
        u32 down = WPAD_ButtonsDown(0);
        float fwd = 0, strafe = 0, turn = 0;
        if (held & WPAD_BUTTON_UP)    fwd  += 1;
        if (held & WPAD_BUTTON_DOWN)  fwd  -= 1;
        if (held & WPAD_BUTTON_LEFT)  turn -= 1;
        if (held & WPAD_BUTTON_RIGHT) turn += 1;

        WPADData *wd = WPAD_Data(0);
        if (wd->exp.type == WPAD_EXP_NUNCHUK) {
            float mag = wd->exp.nunchuk.js.mag;
            float ang = wd->exp.nunchuk.js.ang * (float)M_PI / 180.0f; /* 0 = up */
            if (mag > 0.15f) {
                fwd    += cosf(ang) * mag;
                strafe += sinf(ang) * mag;
            }
        }

        /* Forward = (sin yaw, -cos yaw), right = (cos yaw, sin yaw) in the X/Z plane */
        yaw += turn * TURN_SPEED;
        float fx = sinf(yaw), fz = -cosf(yaw);
        float rx = cosf(yaw), rz = sinf(yaw);
        float dx = (fx * fwd + rx * strafe) * MOVE_SPEED;
        float dz = (fz * fwd + rz * strafe) * MOVE_SPEED;

        /* Axis-separated movement gives wall sliding */
        if (!Blocked(px + dx, pz)) px += dx;
        if (!Blocked(px, pz + dz)) pz += dz;

        /* Jump (Nunchuk Z) + gravity; blocks up to your jump height can be climbed onto */
        if ((down & WPAD_NUNCHUK_BUTTON_Z) && onGround) { vy = JUMP_VEL; onGround = 0; }
        vy -= GRAVITY;
        py += vy;
        float gnd = GroundHeight(px, pz);
        if (py <= gnd) { py = gnd; vy = 0; onGround = 1; }
        else           { onGround = 0; }

        /* ---- draw ---- */
        GRRLIB_2dMode();
        GRRLIB_FillScreen(lvl.sky);

        GRRLIB_Camera3dSettings(px, py + EYE_HEIGHT, pz,  0, 1, 0,  px + fx, py + EYE_HEIGHT, pz + fz);
        const float farZ = DRAW_DIST + 6.0f;
        GRRLIB_3dMode(0.1f, farZ, 60.0f, true, false);
        GX_SetCullMode(GX_CULL_NONE);
#if USE_FOG
        GXColor fogCol = { (u8)(lvl.sky >> 24), (u8)(lvl.sky >> 16), (u8)(lvl.sky >> 8), 255 };
        GX_SetFog(GX_FOG_PERSP_LIN, DRAW_DIST - 8.0f, (float)DRAW_DIST, 0.1f, farZ, fogCol);
#endif
        GRRLIB_ObjectView(0, 0, 0,  0, 0, 0,  1, 1, 1);
        DrawLevel(px, pz, fx, fz);
#if USE_FOG
        GX_SetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.1f, farZ, fogCol);   /* menus/2D must not be fogged */
#endif

        GRRLIB_2dMode();
        GRRLIB_Render();
    }
    return 0;
}
