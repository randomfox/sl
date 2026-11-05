/*
 * Additions Copyright (c) 2026 Micah Fox, released under the MIT License (see LICENSE in this
 * directory): the steam whistle, the random option stacking, -f and the piped-text cars, and this
 * port to C.
 *
 * Derived from sl 5.02 and the kerker00/sl fork. Those parts remain under the original terms:
 * the Toyoda Masashi notice below, and the LICENSE in the repository root, must appear in all
 * copies. See ../CREDITS.md.
 */

/*
 * sl with a steam whistle, in C: plays a whistle at full system volume, and when run without
 * options, three times in four adds random options from -a, -l, -F, -G, -i, -3, -c, -b and -L,
 * stacking them, each at most once, at most one train, and only one of -F and -L (each draw has a
 * one-in-four chance of stopping). -f can only come first.
 * Self-contained: needs only the C library, a terminal that understands ANSI escapes, and
 * PulseAudio/PipeWire's paplay and pactl for the whistle.
 *
 * Build: gcc -O2 -Wall -o sl sl.c -lm
 *
 * Options from the original sl (Debian's version adds -e):
 *   -a, --accident   An accident seems to happen. You'll feel pity for people who cry for help.
 *   -l, --little     Shows little one.
 *   -F, --fly        It flies.
 *   -c, --C51        Shows the C51 locomotive.
 *   -e, --interrupt  Allow interrupt by Ctrl+C.
 *
 * Options from the kerker00/sl fork:
 *   -G, --TGV        A French TGV (yellow).
 *   -i, --ICE        A German ICE 1 (red on white), with three middle cars including the bistro car.
 *   -3, --ICE3RD     A German ICE 3 Redesign (red on white), with three middle cars.
 *   -b, --backwards  The train runs backwards, from left to right.
 *   -L, --land       The train lands: it comes down as it crosses the screen (the opposite of -F).
 *
 * Added here:
 *   -f, --files      The names of the files in the current directory ride along as cars (hidden
 *                    ones too with -a, like ls -a).
 *   -h, --help       Show the options, but only after the train and the whistle are done.
 *
 * Long names work in any case (--tgv, --TGV). Which train runs if several are given: -l, then -c,
 * -i, -3, -G, and the D51 by default.
 *
 * Piped text: each line becomes a passenger car on the end of the train with the text on its side,
 * e.g.  ls | sl   or   echo -e "Hello\nworld!" | sl
 *
 * The animation and ASCII art are ported from sl 5.02:
 *   Copyright 1993,1998 Toyoda Masashi (toyoda@is.titech.ac.jp)
 *   Everyone is permitted to do anything on this program including copying,
 *   modifying, and improving, unless you try to pretend that you wrote it.
 *   i.e., the above copyright notice has to appear in all copies.
 *   THE AUTHOR DISCLAIMS ANY RESPONSIBILITY WITH REGARD TO THIS SOFTWARE.
 * TGV and ICE art from the kerker00/sl fork (Markus Mueller et al.; ICE art via ascii.co.uk).
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* ---- ASCII art (padded to fixed widths) ---- */

static const char *const D51_BODY[] = {
    "      ====        ________                ___________ ",
    "  _D _|  |_______/        \\__I_I_____===__|_________| ",
    "   |(_)---  |   H\\________/ |   |        =|___ ___|   ",
    "   /     |  |   H  |  |     |   |         ||_| |_||   ",
    "  |      |  |   H  |__--------------------| [___] |   ",
    "  | ________|___H__/__|_____/[][]~\\_______|       |   ",
    "  |/ |   |-----------I_____I [][] []  D   |=======|__ ",
};
static const char *const D51_WHEELS[] = {
    "__/ =| o |=-~~\\  /~~\\  /~~\\  /~~\\ ____Y___________|__ ",
    " |/-=|___|=    ||    ||    ||    |_____/~\\___/        ",
    "  \\_/      \\O=====O=====O=====O_/      \\_/            ",
    "__/ =| o |=-~~\\  /~~\\  /~~\\  /~~\\ ____Y___________|__ ",
    " |/-=|___|=O=====O=====O=====O   |_____/~\\___/        ",
    "  \\_/      \\__/  \\__/  \\__/  \\__/      \\_/            ",
    "__/ =| o |=-O=====O=====O=====O \\ ____Y___________|__ ",
    " |/-=|___|=    ||    ||    ||    |_____/~\\___/        ",
    "  \\_/      \\__/  \\__/  \\__/  \\__/      \\_/            ",
    "__/ =| o |=-~O=====O=====O=====O\\ ____Y___________|__ ",
    " |/-=|___|=    ||    ||    ||    |_____/~\\___/        ",
    "  \\_/      \\__/  \\__/  \\__/  \\__/      \\_/            ",
    "__/ =| o |=-~~\\  /~~\\  /~~\\  /~~\\ ____Y___________|__ ",
    " |/-=|___|=   O=====O=====O=====O|_____/~\\___/        ",
    "  \\_/      \\__/  \\__/  \\__/  \\__/      \\_/            ",
    "__/ =| o |=-~~\\  /~~\\  /~~\\  /~~\\ ____Y___________|__ ",
    " |/-=|___|=    ||    ||    ||    |_____/~\\___/        ",
    "  \\_/      \\_O=====O=====O=====O/      \\_/            ",
};
static const char *const C51_BODY[] = {
    "        ___                                            ",
    "       _|_|_  _     __       __             ___________",
    "    D__/   \\_(_)___|  |__H__|  |_____I_Ii_()|_________|",
    "     | `---'   |:: `--'  H  `--'         |  |___ ___|  ",
    "    +|~~~~~~~~++::~~~~~~~H~~+=====+~~~~~~|~~||_| |_||  ",
    "    ||        | ::       H  +=====+      |  |::  ...|  ",
    "|    | _______|_::-----------------[][]-----|       |  ",
};
static const char *const C51_WHEELS[] = {
    "| /~~ ||   |-----/~~~~\\  /[I_____I][][] --|||_______|__",
    "------'|oOo|=[]=-      ||      ||      |  ||=======_|__",
    "/~\\____|___|/~\\_|  O=======O=======O   |__|+-/~\\_|     ",
    "\\_/         \\_/  \\____/  \\____/  \\____/      \\_/       ",
    "| /~~ ||   |-----/~~~~\\  /[I_____I][][] --|||_______|__",
    "------'|oOo|=[]=- O=======O=======O    |  ||=======_|__",
    "/~\\____|___|/~\\_|      ||      ||      |__|+-/~\\_|     ",
    "\\_/         \\_/  \\____/  \\____/  \\____/      \\_/       ",
    "| /~~ ||   |-----/~~~~\\  /[I_____I][][] --|||_______|__",
    "------'|oOo|==[]=- O=======O=======O   |  ||=======_|__",
    "/~\\____|___|/~\\_|      ||      ||      |__|+-/~\\_|     ",
    "\\_/         \\_/  \\____/  \\____/  \\____/      \\_/       ",
    "| /~~ ||   |-----/~~~~\\  /[I_____I][][] --|||_______|__",
    "------'|oOo|===[]=- O=======O=======O  |  ||=======_|__",
    "/~\\____|___|/~\\_|      ||      ||      |__|+-/~\\_|     ",
    "\\_/         \\_/  \\____/  \\____/  \\____/      \\_/       ",
    "| /~~ ||   |-----/~~~~\\  /[I_____I][][] --|||_______|__",
    "------'|oOo|===[]=-    ||      ||      |  ||=======_|__",
    "/~\\____|___|/~\\_|    O=======O=======O |__|+-/~\\_|     ",
    "\\_/         \\_/  \\____/  \\____/  \\____/      \\_/       ",
    "| /~~ ||   |-----/~~~~\\  /[I_____I][][] --|||_______|__",
    "------'|oOo|==[]=-     ||      ||      |  ||=======_|__",
    "/~\\____|___|/~\\_|   O=======O=======O  |__|+-/~\\_|     ",
    "\\_/         \\_/  \\____/  \\____/  \\____/      \\_/       ",
};
static const char *const COAL[] = {
    "                              ",
    "                              ",
    "    _________________         ",
    "   _|                \\_____A  ",
    " =|                        |  ",
    " -|                        |  ",
    "__|________________________|_ ",
    "|__________________________|_ ",
    "   |_D__D__D_|  |_D__D__D_|   ",
    "    \\_/   \\_/    \\_/   \\_/    ",
};
static const char *const LOGO_BODY[] = {
    "     ++      +------ ",
    "     ||      |+-+ |  ",
    "   /---------|| | |  ",
    "  + ========  +-+ |  ",
};
static const char *const LOGO_WHEELS[] = {
    " _|--O========O~\\-+  ",
    "//// \\_/      \\_/    ",
    " _|--/O========O\\-+  ",
    "//// \\_/      \\_/    ",
    " _|--/~O========O-+  ",
    "//// \\_/      \\_/    ",
    " _|--/~\\------/~\\-+  ",
    "//// \\_O========O    ",
    " _|--/~\\------/~\\-+  ",
    "//// \\O========O/    ",
    " _|--/~\\------/~\\-+  ",
    "//// O========O_/    ",
};
static const char *const LOGO_COAL[] = {
    "____                 ",
    "|   \\@@@@@@@@@@@     ",
    "|    \\@@@@@@@@@@@@@_ ",
    "|                  | ",
    "|__________________| ",
    "   (O)       (O)     ",
    "                     ",
};
static const char *const LOGO_CAR[] = {
    "____________________ ",
    "|  ___ ___ ___ ___ | ",
    "|  |_| |_| |_| |_| | ",
    "|                  | ",
    "|__________________| ",
    "   (O)        (O)    ",
    "                     ",
};
static const char *const TGV_BODY[] = {
    "                             ______________   /          ",
    "                ____________/______________|__\\_______   ",
    "          _____/  |IIIIIIIIIII|_______________________|  ",
    "       __/__/_|_|       ______________________________|  ",
    "    __/       | |      /         ||||||||||||  TGV || |  ",
    "   />  []     | |    /___________||||||||||||______|| |  ",
    "  \\_==================================================|  ",
};
static const char *const TGV_WHEELS[] = {
    "    |___/==0==0==|_________________________/===0==0==\\|  ",
    "    |___/0==0==0=|_________________________/=0==0==0=\\|  ",
};
static const char *const TGV_CAR[] = {
    "  ___________                                            ",
    "  |_====____|\\________________________________________   ",
    "o|  ||||         ___                                  |  ",
    "H|______________|___|_________________________________|  ",
    "H|  |||| |[]|   |[]1|    []   [  ] [  ] [  ] [  ]     |  ",
    "H|__||||_|__|___|___|_________________________________|  ",
    "D|==============|===|=================================|  ",
    " _/ 0==0  |____________________________________/    0=|  ",
};
static const char *const ICE1_FRONT[] = {
    "                                               \"-._      ",
    "                     ______________________---\"__,-'___  ",
    "                 _,-'/| #### | |              ####     | ",
    "             _-'             | |                       | ",
    "             (_--------------------------------DB------| ",
    "              \\------(o)~~~~(o)------------(o)~~~~(o)--' ",
    "              \\-------(O)~~~~(O)------------(O)~~~~(O)-' ",
};
static const char *const ICE1_WAGON[] = {
    "                                                         ",
    "  _____________________________________________________  ",
    "*| _  _____________________________________________  _ | ",
    "*||O||____||____||____||____||____||____||____||___||O|| ",
    "*||_|----DB---------------------------------DB------|_|| ",
    " `---(o)~~~(o)-----------------------------(o)~~~(o)---' ",
    " `----(O)~~~(O)-----------------------------(O)~~~(O)--' ",
};
static const char *const ICE1_BISTRO[] = {
    "      _____________________________________________      ",
    "  _.-' == == ===     ###              === == == == '-._  ",
    "*|    __________                   _  _____________    | ",
    "*|   |__________|  ~~~~ICE~~~~    |O| |____________|   | ",
    "*|-------DB-----------------------|_|-------DB---------| ",
    " `---(o)~~~(o)-----------------------------(o)~~~(o)---' ",
    " `----(O)~~~(O)-----------------------------(O)~~~(O)--' ",
};
static const char *const ICE1_REAR[] = {
    "      _.-\"                                               ",
    " ___'-,__\"---______________________                      ",
    "*|     ####              | | #### |\\'-,_                 ",
    "*|                       | |            '-_              ",
    "*|------DB-------------------------------_)              ",
    " '--(o)~~~~(o)------------(o)~~~~(o)-----/               ",
    " '---(O)~~~~(O)------------(O)~~~~(O)----/               ",
};
static const char *const ICE3_FRONT[] = {
    "                                                              ",
    "              _ ..------_-----------------------------'**`---,",
    "         . _`,.---,---,|-| ,----,----,----,----,----,----,   |",
    "      .' `..--'---'---'|=| '----'----'----'----'----'----'   |",
    "    (_,..----- ICE ----|_|-----------------------------------|",
    "      ````*(o)*~~*(o)*````````````````````````*(o)*~~*(o)*''' ",
    "      `````*(O)*~~*(O)*````````````````````````*(O)*~~*(O)*'' ",
};
static const char *const ICE3_WAGON[] = {
    "                                                              ",
    " ,-_-'**`---------------------------------------------'**`-_-,",
    "#||-|    `` ,----,----,----,----,----,----,----,----,     |-||",
    "#||=|       '----'----'----'----'----'----'----'----'     |=||",
    "#||_|-----------------------------------------------------|_||",
    "  ```*(o)*~~*(o)*`````````````````````````````*(o)*~~*(o)*''' ",
    "  ```*(O)*~~*(O)*`````````````````````````````*(O)*~~*(O)*''' ",
};
static const char *const ICE3_REAR[] = {
    "                                                              ",
    ",---'**`-----------------------------_------.. _              ",
    "|    ,----,----,----,----,----,----,|-|,---,---,.`_ .         ",
    "|    '----'----'----'----'----'----'|=|'---'---'--..` '.      ",
    "|-----------------------------------|_|---- ICE -----..,_)    ",
    " '''*(o)*~~*(o)*````````````````````````*(o)*~~*(o)*````      ",
    " ''*(O)*~~*(O)*````````````````````````*(O)*~~*(O)*`````      ",
};

#define D51HEIGHT 10
#define D51FUNNEL 7
#define D51LENGTH 83
#define C51HEIGHT 11
#define C51FUNNEL 7
#define C51LENGTH 87
#define LOGOHEIGHT 6
#define LOGOFUNNEL 4
#define LOGOLENGTH 84
#define TGVHEIGHT 8
#define TGVLENGTH 114
#define ICEHEIGHT 6
#define ICE_CARS 3                                /* middle cars between the two power cars */
#define ICE1_STEP 56
#define ICE1_LENGTH (56 * (ICE_CARS + 1) + 44)    /* the rear power car is 44 columns wide */
#define ICE3_STEP 61
#define ICE3_LENGTH (61 * (ICE_CARS + 1) + 62)

/* Where -a puts two people in each ICE car: the row for "Help!" (they wave from the window row
 * below it), then the two window columns. */
static const int ICE1_FRONT_MEN[] = {1, 24, 46}, ICE1_WAGON_MEN[] = {2, 13, 37};
static const int ICE1_BISTRO_MEN[] = {2, 9, 42}, ICE1_REAR_MEN[] = {1, 8, 30};
static const int ICE3_FRONT_MEN[] = {2, 15, 38}, ICE3_WAGON_MEN[] = {2, 18, 38};
static const int ICE3_REAR_MEN[] = {2, 16, 40};

static const char *const SMOKE[2][16] = {
    {"(   )", "(    )", "(    )", "(   )", "(  )", "(  )", "( )", "( )", "()", "()",
     "O", "O", "O", "O", "O", " "},
    {"(@@@)", "(@@@@)", "(@@@@)", "(@@@)", "(@@)", "(@@)", "(@)", "(@)", "@@", "@@",
     "@", "@", "@", "@", "@", " "}};
static const int SMOKE_DY[16] = {2, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const int SMOKE_DX[16] = {-2, -1, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3};

#define BLANK "                                                                "  /* 64 spaces */

/* ---- State ---- */

static int ACCIDENT, FLY, LAND, BACK, LOGO, C51, TGV, ICE, ICE3, INTR, FILES, HELP;
static const char *prog = "sl";
static pid_t whistle_pid = -1;
enum train { T_D51, T_SL, T_C51, T_TGV, T_ICE, T_ICE3 };
static enum train TRAIN;
static long FRAME_US;
static const char *COLOR = "", *ICE_COLOR = "";

static int ROWS, COLS;
static uint32_t *scr;            /* ROWS * COLS cells, one Unicode code point each */
#define MAXSPANS 32
static int (*spans)[2 * MAXSPANS]; /* per row: painted column ranges for the ICE's white */
static int *nspans;

static int Y, DY;                /* set by place() */

struct smoke { int y, x, p, k; };
static struct smoke *smokes;
static int nsmokes, capsmokes;

struct car { uint32_t *rows[7]; int x, w, man; };
static struct car *cars;
static int ncars, capcars, LABEL_LEN, first_car;

static volatile sig_atomic_t quit_code, resized;
static int tty = -1, have_saved, screen_on;
static struct termios saved;

static int mod(int a, int m) { return (a % m + m) % m; }
static int rnd(int n) { return (int)(drand48() * n); }

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n);
    if (!p) { perror("sl"); exit(1); }
    return p;
}

/* ---- Running other programs (the whistle) ---- */

/* Run argv quietly and wait for it; with out, capture its output there. */
static int run(char *const argv[], char *out, size_t outsize)
{
    int fds[2];
    if (out && pipe(fds) < 0) return -1;
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int null = open("/dev/null", O_RDWR);
        dup2(null, 0);
        dup2(out ? fds[1] : null, 1);
        dup2(null, 2);
        execvp(argv[0], argv);
        _exit(127);
    }
    if (out) {
        size_t len = 0;
        ssize_t n;
        close(fds[1]);
        while ((n = read(fds[0], out + len, outsize - 1 - len)) > 0 || (n < 0 && errno == EINTR))
            if (n > 0) len += n;
        out[len] = '\0';
        close(fds[0]);
    }
    int status;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void mkdirs(char *path)
{
    for (char *p = path + 1; *p; p++)
        if (*p == '/') { *p = '\0'; mkdir(path, 0755); *p = '/'; }
}

static void le(FILE *f, unsigned long v, int bytes)
{
    for (int i = 0; i < bytes; i++) { fputc(v & 255, f); v >>= 8; }
}

/* Synthesize the whistle WAV: a three-chime chord with steam hiss, a short toot then a long one,
 * soft-clipped to full scale for maximum loudness. Written to a temp file, then moved into place,
 * so a half-written file is never played. */
static int make_whistle(const char *path)
{
    const double sr = 44100, f[3] = {369.0, 466.2, 554.4}, drive = 4.0;
    int n1 = (int)(sr * 0.45), gap = (int)(sr * 0.12), n2 = (int)(sr * 1.6), ns = n1 + gap + n2;
    double *x = calloc(ns, sizeof *x), peak = 0;
    if (!x) return -1;
    for (int b = 0, at = 0; b < 2; b++) {
        double dur = b ? 1.6 : 0.45, ph[3] = {0, 0, 0}, lp = 0;
        int n = b ? n2 : n1;
        if (b) at += gap;
        for (int i = 0; i < n; i++) {
            double t = i / sr, v = 0;
            double env = (t / 0.08 < 1 ? t / 0.08 : 1) * ((dur - t) / 0.25 < 1 ? (dur - t) / 0.25 : 1);
            double bend = 1 - 0.06 * exp(-t / 0.12);         /* pitch scoops up as steam builds */
            double vib = 1 + 0.004 * sin(2 * M_PI * 5.5 * t);
            for (int k = 0; k < 3; k++) {
                ph[k] += 2 * M_PI * f[k] * bend * vib / sr;
                v += sin(ph[k]) + 0.3 * sin(2 * ph[k]) + 0.1 * sin(3 * ph[k]);
            }
            lp += 0.15 * ((2 * drand48() - 1) - lp);           /* breathy steam hiss */
            x[at++] = env * (v / 4.2 + 0.35 * lp + 0.08 * (2 * drand48() - 1));
        }
    }
    for (int i = 0; i < ns; i++) if (fabs(x[i]) > peak) peak = fabs(x[i]);

    char tmp[4096 + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *out = fopen(tmp, "wb");
    if (!out) { free(x); return -1; }
    fputs("RIFF", out); le(out, 36 + ns * 2, 4); fputs("WAVEfmt ", out);   /* 16-bit mono PCM */
    le(out, 16, 4); le(out, 1, 2); le(out, 1, 2); le(out, 44100, 4); le(out, 88200, 4);
    le(out, 2, 2); le(out, 16, 2);
    fputs("data", out); le(out, ns * 2, 4);
    for (int i = 0; i < ns; i++) {
        int s = (int)(tanh(drive * x[i] / peak) / tanh(drive) * 0.99 * 32767);
        le(out, (unsigned)(s & 0xffff), 2);
    }
    free(x);
    if (fclose(out) != 0 || rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

/* Play the whistle in the background with the system volume cranked to 100%, then restore the
 * volume (per channel) and the mute state. It runs in its own process group, so keyboard signals
 * (Ctrl+C etc.), which go only to the terminal's foreground group, never reach it; paplay installs
 * its own Ctrl+C handler, so ignoring the signal wouldn't be enough. */
static void whistle(void)
{
    if ((whistle_pid = fork()) != 0) return;
    setpgid(0, 0);
    signal(SIGINT, SIG_IGN); signal(SIGQUIT, SIG_IGN); signal(SIGTSTP, SIG_IGN);
    signal(SIGHUP, SIG_DFL); signal(SIGTERM, SIG_DFL);

    /* The lock keeps overlapping runs from saving 100% as the "original" volume. */
    char path[4096];
    const char *rt = getenv("XDG_RUNTIME_DIR");
    snprintf(path, sizeof path, "%s/sl-whistle.lock", rt && *rt ? rt : "/tmp");
    int lock = open(path, O_WRONLY | O_CREAT, 0644);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) < 0) _exit(0);

    const char *home = getenv("HOME");
    snprintf(path, sizeof path, "%s/.local/share/sounds/steam-whistle.wav", home ? home : ".");
    if (access(path, F_OK) != 0) {
        mkdirs(path);
        if (make_whistle(path) != 0) _exit(0);
    }

    char sink[512] = "", vol[1024] = "", mute[256] = "", *vols[32];
    int nvols = 0, muted = 0;
    run((char *[]){"pactl", "get-default-sink", NULL}, sink, sizeof sink);
    sink[strcspn(sink, "\n")] = '\0';
    if (*sink) {
        run((char *[]){"pactl", "get-sink-volume", sink, NULL}, vol, sizeof vol);
        run((char *[]){"pactl", "get-sink-mute", sink, NULL}, mute, sizeof mute);
        muted = strstr(mute, "yes") != NULL;
        for (char *p = vol; *p && nvols < 30; p++) {   /* every "NN%" in order, one per channel */
            if (*p >= '0' && *p <= '9' && (p == vol || !(p[-1] >= '0' && p[-1] <= '9'))) {
                char *e = p;
                while (*e >= '0' && *e <= '9') e++;
                if (*e == '%') { vols[nvols++] = p; p = e; *e = '\0'; }
            }
        }
        run((char *[]){"pactl", "set-sink-mute", sink, "0", NULL}, NULL, 0);
        run((char *[]){"pactl", "set-sink-volume", sink, "100%", NULL}, NULL, 0);
    }
    run((char *[]){"paplay", "--volume=65536", path, NULL}, NULL, 0);
    if (*sink && nvols) {
        char *argv[40] = {"pactl", "set-sink-volume", sink};
        char pct[32][16];
        for (int i = 0; i < nvols; i++) {
            snprintf(pct[i], sizeof pct[i], "%s%%", vols[i]);
            argv[3 + i] = pct[i];
        }
        argv[3 + nvols] = NULL;
        run(argv, NULL, 0);
        if (muted) run((char *[]){"pactl", "set-sink-mute", sink, "1", NULL}, NULL, 0);
    }
    _exit(0);
}

/* ---- Drawing ---- */

/* Write n characters of an ASCII string into the screen buffer at row y, column x, clipped. */
static void put_n(int y, int x, const char *s, int n)
{
    if (y < 0 || y >= ROWS) return;
    int i = x < 0 ? -x : 0;
    uint32_t *row = scr + (size_t)y * COLS;
    for (; i < n && x + i < COLS; i++) row[x + i] = (unsigned char)s[i];
}
static void put(int y, int x, const char *s) { put_n(y, x, s, (int)strlen(s)); }

static void put_cells(int y, int x, const uint32_t *s, int n)
{
    if (y < 0 || y >= ROWS) return;
    int i = x < 0 ? -x : 0;
    uint32_t *row = scr + (size_t)y * COLS;
    for (; i < n && x + i < COLS; i++) row[x + i] = s[i];
}

static void add_man(int y, int x)
{
    if ((LOGOLENGTH + x) / 12 % 2) { put(y, x, "Help!"); put(y + 1, x, "\\O/"); }
    else put(y + 1, x, "(O)");
}

/* Mark the part of row y that a line of train art covers, from its first to its last non-space
 * character, so only the area under the train gets the ICE's white background. */
static void paint(int y, int x, const char *s)
{
    int n = (int)strlen(s), lead = 0, trail = 0;
    if (y < 0 || y >= ROWS) return;
    while (lead < n && s[lead] == ' ') lead++;
    if (lead == n) return;
    while (s[n - 1 - trail] == ' ') trail++;
    int a = x + lead, b = x + n - trail;
    if (a < 0) a = 0;
    if (b > COLS) b = COLS;
    if (a < b && nspans[y] < MAXSPANS) {
        spans[y][2 * nspans[y]] = a;
        spans[y][2 * nspans[y] + 1] = b;
        nspans[y]++;
    }
}

/* Every 4 columns, age the existing puffs (drifting up and back) and emit a new one at the funnel. */
static void add_smoke(int y, int x)
{
    if (x % 4 == 0) {
        for (int i = 0; i < nsmokes; i++) {
            struct smoke *s = &smokes[i];
            s->y -= SMOKE_DY[s->p];
            s->x += SMOKE_DX[s->p];
            if (s->p < 15) s->p++;
        }
        if (nsmokes == capsmokes) {
            capsmokes = capsmokes ? 2 * capsmokes : 64;
            smokes = xrealloc(smokes, capsmokes * sizeof *smokes);
        }
        smokes[nsmokes] = (struct smoke){y, x, 0, nsmokes % 2};
        nsmokes++;
    }
    for (int i = 0; i < nsmokes; i++)
        if (smokes[i].p != 15)  /* fully dissipated */
            put(smokes[i].y, smokes[i].x, SMOKE[smokes[i].k][smokes[i].p]);
}

/* ---- Labeled cars (piped text and -f) ---- */

static uint32_t *row_new(int w)
{
    uint32_t *r = xrealloc(NULL, w * sizeof *r);
    for (int i = 0; i < w; i++) r[i] = ' ';
    return r;
}
static void row_put(uint32_t *r, int at, const char *s) { while (*s) r[at++] = (unsigned char)*s++; }
static void row_fill(uint32_t *r, int at, int n, char c) { while (n-- > 0) r[at++] = (unsigned char)c; }

/* Turn a line of text into a passenger car for the end of the train, the text on its side.
 * Little-train cars match its own cars; big-train cars match the height of the coal tender. */
static void add_label_car(const char *line)
{
    size_t len = strlen(line);
    uint32_t *text = xrealloc(NULL, (len * 4 + 4) * sizeof *text);
    int n = 0, w, W, left, i;
    const unsigned char *p = (const unsigned char *)line;

    while (*p) {
        if (*p == '\t') { for (i = 0; i < 4; i++) text[n++] = ' '; p++; continue; }
        if (*p == 0x1b && p[1] == '[') {  /* terminal color/formatting codes */
            const unsigned char *q = p + 2;
            while (*q >= 0x30 && *q <= 0x3f) q++;
            while (*q >= 0x20 && *q <= 0x2f) q++;
            if (*q >= 0x40 && *q <= 0x7e) { p = q + 1; continue; }
        }
        if (*p < 0x20 || *p == 0x7f) { p++; continue; }  /* other control characters */
        if (*p < 0x80) { text[n++] = *p++; continue; }
        int extra = *p >= 0xf0 ? 3 : *p >= 0xe0 ? 2 : *p >= 0xc0 ? 1 : -1;  /* UTF-8 */
        uint32_t c = extra == 3 ? *p & 7 : extra == 2 ? *p & 15 : *p & 31;
        for (i = 1; extra > 0 && i <= extra; i++) {
            if ((p[i] & 0xc0) != 0x80) { extra = -1; break; }
            c = c << 6 | (p[i] & 63);
        }
        if (extra < 0) { text[n++] = '?'; p++; continue; }
        p += extra + 1;
        if (c >= 0x80 && c < 0xa0) continue;  /* C1 control characters */
        text[n++] = c;
    }
    for (i = 0; i < n && text[i] == ' '; i++) {}
    if (i == n) { free(text); return; }  /* skip blank lines */
    if (n > 60) { n = 57; text[n++] = '.'; text[n++] = '.'; text[n++] = '.'; }

    if (ncars == capcars) {
        capcars = capcars ? 2 * capcars : 16;
        cars = xrealloc(cars, capcars * sizeof *cars);
    }
    struct car *car = &cars[ncars];
    if (LOGO) {
        w = n + 2 > 18 ? n + 2 : 18;
        W = w + 3;
        int nw = (w - 2) / 4;
        for (i = 0; i < 6; i++) car->rows[i] = row_new(W);
        row_fill(car->rows[0], 0, w + 2, '_');
        for (i = 1; i <= 4; i++) { car->rows[i][0] = '|'; car->rows[i][w + 1] = '|'; }
        for (i = 0; i < nw; i++) { row_put(car->rows[1], 3 + 4 * i, "___"); row_put(car->rows[2], 3 + 4 * i, "|_|"); }
        row_fill(car->rows[4], 1, w, '_');
        row_put(car->rows[5], 3, "(O)");
        row_put(car->rows[5], W - 7, "(O)");
        car->man = 3;  /* column of the first window, where -a puts people */
        left = 1 + (w - n) / 2;
        memcpy(car->rows[3] + left, text, n * sizeof *text);
    } else {
        /* Windows centered along the side, the text on the panel below them */
        w = n + 6 > 20 ? n + 6 : 20;
        W = w + 4;
        int nw = (w - 2) / 4, pad = (w - (4 * nw - 1)) / 2;
        for (i = 0; i < 7; i++) car->rows[i] = row_new(W);
        row_fill(car->rows[0], 1, w + 2, '_');
        for (i = 1; i <= 4; i++) { car->rows[i][1] = '|'; car->rows[i][w + 2] = '|'; }
        for (i = 0; i < nw; i++) {
            row_put(car->rows[1], 2 + pad + 4 * i, "___");
            row_put(car->rows[2], 2 + pad + 4 * i, "|_|");
        }
        car->rows[4][0] = '_';
        row_fill(car->rows[4], 2, w, '_');
        car->rows[4][w + 3] = '_';
        row_put(car->rows[5], 3, "|_D__D_|");
        row_put(car->rows[5], W - 11, "|_D__D_|");
        row_put(car->rows[6], 4, "\\_/\\_/");
        row_put(car->rows[6], W - 10, "\\_/\\_/");
        car->man = 2 + pad;
        left = 2 + (w - n) / 2;
        memcpy(car->rows[3] + left, text, n * sizeof *text);
    }
    free(text);
    car->x = LABEL_LEN;
    car->w = W;
    LABEL_LEN += W;
    ncars++;
}

/* Draw the labeled cars behind the train: start is the first car's column offset from x, y the row
 * of each car's top line; when flying, dy is how much lower the first car is and step each next one.
 * Running backwards (-b) the order is mirrored: the first car rides at the far (leading) end and
 * each later one between it and the train, so k counts right to left. */
static void draw_label_cars(int x, int start, int y, int dy, int step)
{
    int rows = LOGO ? 6 : 7;
    for (int k = first_car; k < ncars; k++) {
        int cx, cy;
        if (BACK) {
            cx = x + start + LABEL_LEN - cars[k].x - cars[k].w;
            cy = y + dy + (ncars - 1 - k) * step;
            if (cx >= COLS) continue;            /* already gone off the right */
            if (cx + cars[k].w <= 0) break;      /* this and all later cars not on screen yet */
        } else {
            cx = x + start + cars[k].x;
            cy = y + dy + k * step;
            if (cx + cars[k].w <= 0) { first_car = k + 1; continue; }  /* gone for good */
            if (cx >= COLS) break;
        }
        for (int j = 0; j < rows; j++) put_cells(cy + j, cx, cars[k].rows[j], cars[k].w);
        if (ACCIDENT) {  /* people in the 1st and 3rd windows, like the original's cars */
            add_man(cy + 1, cx + cars[k].man);
            add_man(cy + 1, cx + cars[k].man + 8);
        }
    }
}

/* ---- Trains ---- */

/* Set Y, the top row of a train h rows tall: mid (mid-screen) normally, or on a slope of one row per
 * s columns when flying (-F, rising as it goes left) or landing (-L, coming down). Also set DY, how
 * many rows lower each following car rides: 1 flying, -1 landing, 0 otherwise. */
static void place(int x, int h, int s, int mid)
{
    Y = mid;
    DY = 0;
    if (FLY) { Y = x / s + ROWS - COLS / s - h; DY = 1; }
    else if (LAND) { Y = (3 * ROWS - 2 * (x / s) - 2 * (COLS / s) - 2 * h) / 2; DY = -1; }
}

/* Each add_ function draws its train at column x, and returns 0 once it has gone off the left. */
static int add_D51(int x)
{
    if (x < -D51LENGTH - LABEL_LEN) return 0;
    place(x, D51HEIGHT, 7, ROWS / 2 - 5);
    int y = Y, dy = DY, p = mod(D51LENGTH + x, 6);
    for (int i = 0; i <= D51HEIGHT; i++) {
        if (i < 7) put(y + i, x, D51_BODY[i]);
        else if (i < 10) put(y + i, x, D51_WHEELS[p * 3 + i - 7]);
        else put_n(y + i, x, BLANK, 54);
        if (i < 10) put(y + i + dy, x + 53, COAL[i]);
        else put_n(y + i + dy, x + 53, BLANK, 30);
    }
    draw_label_cars(x, D51LENGTH, y + 3, dy * 2, dy);
    if (ACCIDENT) { add_man(y + 2, x + 43); add_man(y + 2, x + 47); }
    add_smoke(y - 1, x + D51FUNNEL);
    return 1;
}

static int add_C51(int x)
{
    if (x < -C51LENGTH - LABEL_LEN) return 0;
    place(x, C51HEIGHT, 7, ROWS / 2 - 5);
    int y = Y, dy = DY, p = mod(C51LENGTH + x, 6);
    for (int i = 0; i <= C51HEIGHT; i++) {
        if (i < 7) put(y + i, x, C51_BODY[i]);
        else if (i < 11) put(y + i, x, C51_WHEELS[p * 4 + i - 7]);
        else put_n(y + i, x, BLANK, 55);
        if (i == 0 || i == 11) put_n(y + i + dy, x + 55, BLANK, 30);
        else put(y + i + dy, x + 55, COAL[i - 1]);
    }
    draw_label_cars(x, 85, y + 4, dy * 2, dy);  /* right behind the tender (55 + 30) */
    if (ACCIDENT) { add_man(y + 3, x + 45); add_man(y + 3, x + 49); }
    add_smoke(y - 1, x + C51FUNNEL);
    return 1;
}

static int add_sl(int x)
{
    if (x < -LOGOLENGTH - LABEL_LEN) return 0;
    place(x, LOGOHEIGHT, 6, ROWS / 2 - 3);
    int y = Y, py1 = 2 * DY, py2 = 4 * DY, py3 = 6 * DY, p = mod((LOGOLENGTH + x) / 3, 6);
    for (int i = 0; i <= LOGOHEIGHT; i++) {
        if (i < 4) put(y + i, x, LOGO_BODY[i]);
        else if (i < 6) put(y + i, x, LOGO_WHEELS[p * 2 + i - 4]);
        else put_n(y + i, x, BLANK, 21);
        if (i < 6) {
            put(y + i + py1, x + 21, LOGO_COAL[i]);
            put(y + i + py2, x + 42, LOGO_CAR[i]);
            put(y + i + py3, x + 63, LOGO_CAR[i]);
        } else {
            put_n(y + i + py1, x + 21, BLANK, 21);
            put_n(y + i + py2, x + 42, BLANK, 21);
            put_n(y + i + py3, x + 63, BLANK, 21);
        }
    }
    draw_label_cars(x, LOGOLENGTH, y, py3 + py1, py1);
    if (ACCIDENT) {
        add_man(y + 1, x + 14);
        add_man(y + 1 + py2, x + 45); add_man(y + 1 + py2, x + 53);
        add_man(y + 1 + py3, x + 66); add_man(y + 1 + py3, x + 74);
    }
    add_smoke(y - 1, x + LOGOFUNNEL);
    return 1;
}

/* The electric trains make no smoke. */
static int add_TGV(int x)
{
    if (x < -TGVLENGTH - LABEL_LEN) return 0;
    place(x, TGVHEIGHT, 7, ROWS / 2 - 5);
    int y = Y, dy = DY, p = mod((TGVLENGTH + x) / 2, 2), i;
    for (i = 0; i < 7; i++) put(y + i, x, TGV_BODY[i]);
    put(y + 7, x, TGV_WHEELS[p]);
    for (i = 0; i < 8; i++) put(y + i + dy, x + 55, TGV_CAR[i]);
    draw_label_cars(x, 112, y + 1, dy * 2, dy);
    if (ACCIDENT) {
        add_man(y + 2, x + 14);
        for (i = 85; i <= 100; i += 5) add_man(y + dy + 3, x + i);
    }
    return 1;
}

/* Draw one ICE car (5 body rows plus the wheel row for frame p) at row y, column x. */
static void ice_car(const char *const *car, const int *men, int y, int x, int p)
{
    for (int i = 0; i < 6; i++) {
        const char *s = i < 5 ? car[i] : car[5 + p];
        put(y + i, x, s);
        if (*ICE_COLOR) paint(y + i, x, s);
    }
    if (ACCIDENT) { add_man(y + men[0], x + men[1]); add_man(y + men[0], x + men[2]); }
}

/* A power car at each end with ICE_CARS middle cars between them. The fork drew the rear power car
 * on top of the front one unless -n was given, and left it level when flying; here it always
 * follows the middle cars, on the same slope. */
static int add_ICE(int x)
{
    if (x < -ICE1_LENGTH - LABEL_LEN) return 0;
    place(x, ICEHEIGHT, 7, ROWS / 2 - 5);
    int y = Y, dy = DY, p = mod((ICE1_LENGTH + x) / 5, 2), j;
    ice_car(ICE1_FRONT, ICE1_FRONT_MEN, y, x, p);
    for (j = 1; j <= ICE_CARS; j++) {
        if (j == (ICE_CARS + 1) / 2)  /* the bistro car in the middle */
            ice_car(ICE1_BISTRO, ICE1_BISTRO_MEN, y + dy * j, x + ICE1_STEP * j, p);
        else
            ice_car(ICE1_WAGON, ICE1_WAGON_MEN, y + dy * j, x + ICE1_STEP * j, p);
    }
    ice_car(ICE1_REAR, ICE1_REAR_MEN, y + dy * j, x + ICE1_STEP * j, p);
    draw_label_cars(x, ICE1_LENGTH, y - 1, dy * (j + 1), dy);
    return 1;
}

static int add_ICE3(int x)
{
    if (x < -ICE3_LENGTH - LABEL_LEN) return 0;
    place(x, ICEHEIGHT, 7, ROWS / 2 - 5);
    int y = Y, dy = DY, p = mod((ICE3_LENGTH + x) / 5, 2), j;
    ice_car(ICE3_FRONT, ICE3_FRONT_MEN, y, x, p);
    for (j = 1; j <= ICE_CARS; j++)
        ice_car(ICE3_WAGON, ICE3_WAGON_MEN, y + dy * j, x + ICE3_STEP * j, p);
    ice_car(ICE3_REAR, ICE3_REAR_MEN, y + dy * j, x + ICE3_STEP * j, p);
    draw_label_cars(x, ICE3_LENGTH, y - 1, dy * (j + 1), dy);
    return 1;
}

static int add_train(int x)
{
    switch (TRAIN) {
    case T_SL: return add_sl(x);
    case T_C51: return add_C51(x);
    case T_TGV: return add_TGV(x);
    case T_ICE: return add_ICE(x);
    case T_ICE3: return add_ICE3(x);
    default: return add_D51(x);
    }
}

/* ---- Terminal output ---- */

static char *out;
static size_t outlen, outcap;

static void emit(const char *s, size_t n)
{
    if (outlen + n > outcap) {
        outcap = (outlen + n) * 2 + 4096;
        out = xrealloc(out, outcap);
    }
    memcpy(out + outlen, s, n);
    outlen += n;
}
static void emits(const char *s) { emit(s, strlen(s)); }

static void emit_cells(const uint32_t *c, int n)
{
    char buf[4];
    for (int i = 0; i < n; i++) {
        uint32_t u = c[i];
        if (u < 0x80) { buf[0] = (char)u; emit(buf, 1); }
        else if (u < 0x800) { buf[0] = 0xc0 | u >> 6; buf[1] = 0x80 | (u & 63); emit(buf, 2); }
        else if (u < 0x10000) {
            buf[0] = 0xe0 | u >> 12; buf[1] = 0x80 | (u >> 6 & 63); buf[2] = 0x80 | (u & 63);
            emit(buf, 3);
        } else {
            buf[0] = 0xf0 | u >> 18; buf[1] = 0x80 | (u >> 12 & 63);
            buf[2] = 0x80 | (u >> 6 & 63); buf[3] = 0x80 | (u & 63);
            emit(buf, 4);
        }
    }
}

static void flush_out(void)
{
    size_t done = 0;
    while (done < outlen) {
        ssize_t n = write(1, out + done, outlen - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        done += n;
    }
    outlen = 0;
}

/* Send the whole screen. Rows with painted spans get them wrapped in ICE_COLOR; spans arrive left
 * to right (cars are drawn front to back), and neighboring cars overlap by a column, so they merge. */
static void write_frame(void)
{
    char pos[32];
    emits(COLOR);
    for (int r = 0; r < ROWS; r++) {
        const uint32_t *line = scr + (size_t)r * COLS;
        snprintf(pos, sizeof pos, "\x1b[%d;1H", r + 1);
        emits(pos);
        int at = 0, start = -1, end = -1;
        for (int i = 0; i < nspans[r]; i++) {
            int a = spans[r][2 * i], b = spans[r][2 * i + 1];
            if (a <= end) { if (b > end) end = b; continue; }
            if (start >= 0) {
                emit_cells(line + at, start - at); emits(ICE_COLOR);
                emit_cells(line + start, end - start); emits("\x1b[0m");
                at = end;
            }
            start = a, end = b;
        }
        if (start >= 0) {
            emit_cells(line + at, start - at); emits(ICE_COLOR);
            emit_cells(line + start, end - start); emits("\x1b[0m");
            at = end;
        }
        emit_cells(line + at, COLS - at);
    }
    flush_out();
}

/* Read the screen size and size the screen buffer for it. Runs again whenever the terminal is
 * resized (SIGWINCH, checked once per frame), so the train re-centers and keeps going. */
static void size_screen(void)
{
    struct winsize ws;
    if (tty >= 0 && ioctl(tty, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0)
        ROWS = ws.ws_row, COLS = ws.ws_col;
    else
        ROWS = 24, COLS = 80;
    scr = xrealloc(scr, (size_t)ROWS * COLS * sizeof *scr);
    spans = xrealloc(spans, ROWS * sizeof *spans);
    nspans = xrealloc(nspans, ROWS * sizeof *nspans);
}

static void show_help(void)
{
    printf("Usage: %s [options]\n"
           "A steam locomotive runs across your terminal, with a steam whistle at full volume.\n"
           "Run without options, it usually adds some random ones.\n"
           "\n"
           "  -a, --accident    People cry for help from the windows.\n"
           "  -l, --little      The little train.\n"
           "  -c, --C51         The C51 steam locomotive.\n"
           "  -G, --TGV         A French TGV.\n"
           "  -i, --ICE         A German ICE 1.\n"
           "  -3, --ICE3RD      A German ICE 3.\n"
           "  -F, --fly         The train flies.\n"
           "  -L, --land        The train lands.\n"
           "  -b, --backwards   The train runs from left to right.\n"
           "  -f, --files       The files in this directory ride along as cars (hidden ones too with -a).\n"
           "  -e, --interrupt   Let Ctrl+C stop the train.\n"
           "  -h, --help        Show this help, once the train has gone by.\n"
           "\n"
           "Long names work in any case. If several trains are given, the first of -l, -c, -i, -3, -G runs.\n"
           "Piped text rides along as cars too:  ls | %s\n", prog, prog);
    fflush(stdout);
}

static void cleanup(void)
{
    if (screen_on) {
        const char *s = "\x1b[0m\x1b[?25h\x1b[?1049l";  /* reset colors, show cursor, leave alt screen */
        if (write(1, s, strlen(s)) < 0) {}
    }
    if (have_saved) tcsetattr(tty, TCSAFLUSH, &saved);  /* also discards keys typed during the show */
    if (HELP) {  /* -h shows its help only once the train and the whistle are done */
        if (whistle_pid > 0) while (waitpid(whistle_pid, NULL, 0) < 0 && errno == EINTR) {}
        show_help();
    }
}

static void on_quit(int sig) { quit_code = sig == SIGINT ? 130 : sig == SIGHUP ? 129 : 143; }
static void on_winch(int sig) { (void)sig; resized = 1; }

static void handle(int sig, void (*fn)(int))
{
    struct sigaction sa = {0};
    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);  /* no SA_RESTART, so the frame sleep wakes up for it */
}

/* ---- Piped text, picked up as it arrives ---- */

static char *inbuf;
static size_t inlen, incap;
static int piped, in_eof;

/* Return the next complete line of piped text (without its newline), or NULL if none is waiting.
 * At the end of the input, a last line without a newline still counts. */
static char *next_line(void)
{
    for (;;) {
        char *nl = inlen ? memchr(inbuf, '\n', inlen) : NULL;
        if (nl || (in_eof && inlen)) {
            size_t n = nl ? (size_t)(nl - inbuf) : inlen;
            char *line = xrealloc(NULL, n + 1);
            memcpy(line, inbuf, n);
            line[n] = '\0';
            for (size_t i = 0; i < n; i++) if (!line[i]) line[i] = ' ';
            size_t used = nl ? n + 1 : n;
            memmove(inbuf, inbuf + used, inlen - used);
            inlen -= used;
            return line;
        }
        if (in_eof) { piped = 0; return NULL; }
        struct pollfd pfd = {0, POLLIN, 0};
        if (poll(&pfd, 1, 0) <= 0) return NULL;  /* nothing waiting yet */
        if (incap - inlen < 65536) { incap = incap * 2 + 65536; inbuf = xrealloc(inbuf, incap); }
        ssize_t n = read(0, inbuf + inlen, incap - inlen);
        if (n > 0) inlen += n;
        else if (n == 0 || errno != EINTR) in_eof = 1;
    }
}

/* ---- Options ---- */

static void parse(const char *arg)
{
    static const struct { const char *name; const char *opt; } longs[] = {
        {"--accident", "-a"}, {"--little", "-l"}, {"--fly", "-F"}, {"--c51", "-c"},
        {"--interrupt", "-e"}, {"--files", "-f"}, {"--tgv", "-G"}, {"--ice", "-i"},
        {"--ice3rd", "-3"}, {"--backwards", "-b"}, {"--land", "-L"}, {"--help", "-h"}};
    if (arg[0] == '-' && arg[1] == '-' && arg[2]) {  /* long names, in any case */
        const char *opt = NULL;
        for (size_t i = 0; i < sizeof longs / sizeof *longs; i++)
            if (strcasecmp(arg, longs[i].name) == 0) opt = longs[i].opt;
        if (!opt) return;  /* unknown ones are ignored, like unknown letters */
        arg = opt;
    }
    if (arg[0] != '-') return;
    for (const char *c = arg + 1; *c; c++) {
        switch (*c) {
        case 'a': ACCIDENT = 1; break;
        case 'F': FLY = 1; break;
        case 'l': LOGO = 1; break;
        case 'c': C51 = 1; break;
        case 'e': INTR = 1; break;
        case 'f': FILES = 1; break;
        case 'h': HELP = 1; break;
        case 'G': TGV = 1; break;
        case 'i': ICE = 1; break;
        case '3': ICE3 = 1; break;
        case 'b': BACK = 1; break;
        case 'L': LAND = 1; break;
        }
    }
}

static int in_group(const char *o, const char *group)
{
    char pad[8];
    snprintf(pad, sizeof pad, " %s ", o);
    return strstr(group, pad) != NULL;
}

static int no_dots(const struct dirent *d) { return d->d_name[0] != '.'; }
static int not_dot_dirs(const struct dirent *d)
{
    return strcmp(d->d_name, ".") != 0 && strcmp(d->d_name, "..") != 0;
}

int main(int argc, char **argv)
{
    struct timespec now;
    prog = strrchr(argv[0], '/') ? strrchr(argv[0], '/') + 1 : argv[0];
    clock_gettime(CLOCK_REALTIME, &now);
    srand48(now.tv_nsec ^ now.tv_sec ^ (long)getpid() << 16);
    setlocale(LC_COLLATE, "");

    /* Options: the user's own, or with none given, usually some random ones. Draw repeatedly: a
     * quarter of the time nothing, which stops the draw; otherwise one of the options still in the
     * pool, stacked onto the others. Each option is drawn at most once, and so is a train, and
     * flying or landing: drawing one takes the rest of its group out, so each gets a fair share.
     * -f can only be the first draw, so it stays rare (a big directory makes a very long train you
     * can't stop). */
    const char *pool[10] = {"-a", "-l", "-F", "-f", "-G", "-i", "-3", "-c", "-b", "-L"};
    const char *trains = " -l -c -i -3 -G ", *motions = " -F -L ";
    int npool = 10, own = 0, train_drawn = 0, motion_drawn = 0, i;
    for (i = 1; i < argc; i++) if (argv[i][0] == '-') own = 1;  /* any option of your own turns them off */
    while (!own && npool && rnd(4)) {
        const char *pick = pool[rnd(npool)];
        parse(pick);
        if (in_group(pick, trains)) train_drawn = 1;
        if (in_group(pick, motions)) motion_drawn = 1;
        int keep = 0;
        for (i = 0; i < npool; i++) {
            const char *o = pool[i];
            if (o == pick || strcmp(o, "-f") == 0) continue;
            if (train_drawn && in_group(o, trains)) continue;
            if (motion_drawn && in_group(o, motions)) continue;
            pool[keep++] = o;
        }
        npool = keep;
    }
    for (i = 1; i < argc; i++) parse(argv[i]);

    /* Which train runs (same priority as the kerker00 fork), and its frame time: steam trains 40ms
     * like the original, the TGV 20ms, the ICEs 15ms. */
    TRAIN = LOGO ? T_SL : C51 ? T_C51 : ICE ? T_ICE : ICE3 ? T_ICE3 : TGV ? T_TGV : T_D51;
    FRAME_US = TRAIN == T_TGV ? 20000 : TRAIN == T_ICE || TRAIN == T_ICE3 ? 15000 : 40000;

    /* Like the original, ignore Ctrl+C unless -e; also ignore Ctrl+Z (SIGTSTP) and Ctrl+\ (SIGQUIT).
     * SIGKILL and SIGSTOP can't be caught or ignored by any program, so kill -9 and kill -STOP
     * still work. */
    if (INTR) handle(SIGINT, on_quit);
    else { signal(SIGINT, SIG_IGN); signal(SIGTSTP, SIG_IGN); signal(SIGQUIT, SIG_IGN); }

    whistle();

    piped = !isatty(0);
    if (FILES) {
        struct dirent **names;
        int n = scandir(".", &names, ACCIDENT ? not_dot_dirs : no_dots, alphasort);
        for (i = 0; i < n; i++) { add_label_car(names[i]->d_name); free(names[i]); }
        if (n >= 0) free(names);
    }

    tty = open("/dev/tty", O_RDWR | O_NOCTTY);
    handle(SIGWINCH, on_winch);
    size_screen();
    handle(SIGHUP, on_quit);
    handle(SIGTERM, on_quit);
    atexit(cleanup);
    if (tty >= 0 && tcgetattr(tty, &saved) == 0) {
        struct termios raw = saved;
        have_saved = 1;
        raw.c_lflag &= ~(ECHO | ICANON);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(tty, TCSANOW, &raw);
    }

    /* Colors like the fork, if the terminal has them: the TGV yellow on black across the whole
     * screen, the ICEs red on white, but only where the train is. */
    const char *term = getenv("TERM");
    if (term && *term && strcmp(term, "dumb") != 0) {
        if (TRAIN == T_TGV) COLOR = "\x1b[33;40m";
        if (TRAIN == T_ICE || TRAIN == T_ICE3) ICE_COLOR = "\x1b[31;47m";
    }
    emits("\x1b[?1049h\x1b[?25l");  /* alternate screen, hide the cursor, color, clear */
    emits(COLOR);
    emits("\x1b[2J");
    flush_out();
    screen_on = 1;

    /* Running backwards (-b), the train starts just off the left edge and runs until it has gone off
     * the right, its labeled cars leading the way (first line first). Lines piped in while it runs
     * join between the cars and the train, pushing the train back so the cars already moving don't
     * jump. */
    int x, step;
    if (BACK) {
        int len = TRAIN == T_SL ? LOGOLENGTH : TRAIN == T_C51 ? C51LENGTH : TRAIN == T_TGV ? TGVLENGTH
                : TRAIN == T_ICE ? ICE1_LENGTH : TRAIN == T_ICE3 ? ICE3_LENGTH : D51LENGTH;
        x = -len - LABEL_LEN, step = 1;
    } else {
        x = COLS - 1, step = -1;
    }

    /* One frame per column, FRAME_US apart (40ms for steam, like the original); frame times are
     * measured so slow rendering doesn't stretch the show. */
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    for (;; x += step) {
        if (quit_code) break;

        /* Pick up piped lines as they arrive, a few per frame so a big burst doesn't stall it. */
        for (i = 0; piped && i < 10; i++) {
            char *line = next_line();
            if (!line) break;
            int len = LABEL_LEN;
            add_label_car(line);
            free(line);
            if (BACK) x -= LABEL_LEN - len;  /* make room behind the cars by pushing the train back */
        }

        if (resized) {
            resized = 0;
            size_screen();
            emits(COLOR);
            emits("\x1b[2J");
        }
        if (BACK && x >= COLS) break;  /* gone off the right edge */

        for (size_t c = 0; c < (size_t)ROWS * COLS; c++) scr[c] = ' ';
        memset(nspans, 0, ROWS * sizeof *nspans);
        if (!add_train(x)) break;  /* gone off the left edge */
        write_frame();

        next.tv_nsec += FRAME_US * 1000;
        if (next.tv_nsec >= 1000000000) { next.tv_sec++; next.tv_nsec -= 1000000000; }
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec < next.tv_sec || (now.tv_sec == next.tv_sec && now.tv_nsec < next.tv_nsec))
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
    return quit_code;
}
