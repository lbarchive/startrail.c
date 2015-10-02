// startrail.c, star trail in terminal
// Written by Yu-Jie Lin in 2015
//
// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.
//
// In jurisdictions that recognize copyright laws, the author or authors
// of this software dedicate any and all copyright interest in the
// software to the public domain. We make this dedication for the benefit
// of the public at large and to the detriment of our heirs and
// successors. We intend this dedication to be an overt act of
// relinquishment in perpetuity of all present and future rights to this
// software under copyright law.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//
// For more information, please refer to <http://unlicense.org/>


#include <curses.h>
#include <getopt.h>
#include <math.h>
#ifdef matheval
#include <matheval.h>
#endif
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>


#define STR(s)  XSTR(s)
#define XSTR(s) #s

#ifndef M_PI
#define M_PI  3.14159265358979323846
#endif


#ifndef PROGRAM
#define PROGRAM  "startrail"
#endif
#ifndef VERSION
#define VERSION  "unknown"
#endif

#define SCALE_X                    2
#define DEFAULT_star_char        '*'
#define DEFAULT_num_stars        100
#define DEFAULT_angle_min          0
#define DEFAULT_angle_max        360
#define DEFAULT_distance_min       0
#define DEFAULT_distance_max       1
#define DEFAULT_length             1
#define DEFAULT_redraw_length     10
#define DEFAULT_interval_i        50
#define DEFAULT_interval_I      1000
#define DEFAULT_zoom_x             0
#define DEFAULT_zoom_y             0

#define _HELP1  "Usage: " STR(PROGRAM) " [OPTIONS]\n\
star trail in terminal\n\
\n\
Options:\n\
\n\
  -n #      number of stars (Default " STR(DEFAULT_num_stars) ")\n\
  -a #.###  minimum randomly generated angle in degree (Default " STR(DEFAULT_angle_min) ")\n\
  -A #.###  maximum randomly generated angle in degree (Default " STR(DEFAULT_angle_max) ")\n\
  -d #.###  minimum randomly generated distance (Default " STR(DEFAULT_distance_min) ")\n\
  -D #.###  maximum randomly generated distance (Default " STR(DEFAULT_distance_max) ")\n\
  -l #.###  exposing length in terms of Pi\n\
            <=0 exposes forever (Default " STR(DEFAULT_length) ")\n\
  -L #.###  only redraw the last specified length (Default " STR(DEFAULT_redraw_length) ")\n\
  -i #      interval in milliseconds after each iteration\n\
            0 for no interval, <0 for pause (Default " STR(DEFAULT_interval_i) ")\n\
  -I #      interval in milliseconds after exposing the length\n\
            0 for no interval, <0 for pause (Default " STR(DEFAULT_interval_I) ")\n\
  -g        grayscale only\n\
  -c C      character for stars (Default " STR(DEFAULT_star_char) ")\n\
  -s #.###  stepping angle\n\
            (Default tan(1 / (CENTER_X / SCALE_X)))\n\
  -r #.###  radius\n\
            (Default sqrt((SCREEN_H / 2) ** 2 + (SCREEN_W / 2 / SCALE_X) ** 2))\n\
"

#define _HELP2  "\
  -X #.###  simple zooming factor for x (Default " STR(DEFAULT_zoom_x) ")\n\
  -Y #.###  simple zooming factor for y (Default " STR(DEFAULT_zoom_y) ")\n\
  -C        expose clockwise trail as if in Southern Hemisphere\n\
  -R        do not rotate\n\
  -Z        do not zoom\n\
  -h        display this help message\n\
  -v        display version string\n\
\n\
Keys:\n\
\n\
  space     resume from pause (interval<0) / skip (large interval)\n\
  c         clear screen\n\
  q         quit\n\
\n"

#ifdef matheval
#define _HELP_MATHEVAL  "\
  -x func   evaluator for x, special variables:\n\
            x, y - coordinate after rotation or\n\
                   initial coordinate if -R is used\n\
            a, d - star's angle and distance from origin\n\
            i    - ordinal number of current iteration\n\
            s    - stepping angle\n\
            r    - radius\n\
  -y func   evaluator for y, spacial variables same as above\n"
#define HELP  _HELP1 _HELP_MATHEVAL _HELP2
#else
#define HELP  _HELP1 _HELP2
#endif


typedef struct star
{
  double a;  // angle
  double d;  // distance
  int color;
  struct star *next;
} star;
star *stars = NULL;


int num_stars        = DEFAULT_num_stars;
double angle_min     =   0;
double angle_max     = 360;
double distance_min  =   0;
double distance_max  =   1;
double length        = DEFAULT_length * M_PI;
double redraw_length = DEFAULT_redraw_length * M_PI;
double interval_i    = DEFAULT_interval_i;
double interval_I    = DEFAULT_interval_I;
bool grayscale       = FALSE;
int max_colors       = 0;
char star_char       = DEFAULT_star_char;

double r;
bool   r_overridden = FALSE;
double astep;
bool   astep_overridden = FALSE;

double direction = 1.0;  // 1.0 = counter-clockwise, -1.0 = clockwise
double zoom[] = {DEFAULT_zoom_x, DEFAULT_zoom_y};

bool no_rotating = FALSE;
bool no_zooming  = FALSE;

int SCREEN_H, SCREEN_W;
int CENTER_X, CENTER_Y;


void *evalf[] = {NULL, NULL};  // x, y
char *names[] = {"x", "y", "a", "d", "i", "s", "r"};
#define n_values  7


void
cleanup(void)
{
  endwin();

#ifdef matheval
  for (int i = 0; i < 2; i++)
  {
    if (evalf[i])
    {
      evaluator_destroy(evalf[i]);
    }
  }
#endif
}


void
signal_handler(int sig)
{
  cleanup();

  signal(sig, SIG_DFL);
  raise(sig);
}


void
resize(void)
{
  getmaxyx(stdscr, SCREEN_H, SCREEN_W);
  if (!r_overridden)
  {
    r = sqrt(pow(SCREEN_H / 2, 2) + pow(SCREEN_W / 2 / SCALE_X, 2));
  }
  CENTER_X = SCREEN_W / 2;
  CENTER_Y = SCREEN_H / 2;
  if (!astep_overridden)
  {
    astep = tan((double) 1 / (CENTER_X / SCALE_X));
  }

  clear();
}


void
init_colors(void)
{
  if (!has_colors())
  {
    return;
  }

  start_color();

  for (int j = 241; j < 256; j += 2)
  {
    init_pair(++max_colors, j, COLOR_BLACK);
  }

  if (grayscale)
  {
    return;
  }

  for (int j = 0; j < 6; j++)
  {
    for (int k = 0; k < 4; k++)
    {
      init_pair(++max_colors, 33 + j * 36 + 6 * k , COLOR_BLACK);
    }
  }
}


void
new_stars(int n)
{
  star **cur = &stars;

  for (; *cur; )
  {
    stars = (*cur)->next;
    free(*cur);
  }

  for (int i = 0; i < n; i++)
  {
    star *s = malloc(sizeof(star));
    s->a = (((angle_max - angle_min) * rand() / ((long) RAND_MAX + 1) + angle_min) * 2 * M_PI / 360);
    s->d = (distance_max - distance_min) * rand() / RAND_MAX + distance_min;
    s->color = (long) max_colors * rand() / RAND_MAX;
    s->next = NULL;

    if (*cur)
    {
      (*cur)->next = s;
      cur = &(*cur)->next;
    }
    else
    {
      *cur = s;
    }
  }
}


void
expose(long i)
{
  for (star *s = stars; s; s = s->next)
  {
    double x, y;
    double *xy[] = {&x, &y};

    if (no_rotating)
    {
      x = s->d * cos(s->a);
      y = s->d * sin(s->a);
    }
    else
    {
      x = s->d * cos(s->a + direction * i * astep);
      y = s->d * sin(s->a + direction * i * astep);
    }

#ifdef matheval
    double values[] = {x, y, s->a, s->d, i, astep, r};

    for (int j = 0; j < 2; j++)
    {
      if (evalf[j])
      {
        *xy[j] = evaluator_evaluate(evalf[j], n_values, names, values);
      }
    }
#endif

    if (!no_zooming)
    {
      for (int j = 0; j < 2; j++)
      {
        *xy[j] += i * zoom[j] * *xy[j];
      }
    }

    x = CENTER_X + r * x * SCALE_X;
    y = CENTER_Y - r * y;

    attrset(COLOR_PAIR(s->color));
    mvaddch(y, x, star_char);
  }
}


int
main(int argc, char *argv[])
{
  int opt;
#ifdef matheval
#define _MATHEVAL_OPTSTRING "x:y:"
#else
#define _MATHEVAL_OPTSTRING ""
#endif
  while ((opt = getopt(argc, argv, "n:a:A:d:D:i:I:l:L:gc:s:r:X:Y:CRZhv" _MATHEVAL_OPTSTRING)) != -1)
  {
    switch (opt)
    {
      case 'n':
        num_stars = atoi(optarg);
        break;
      case 'a':
        angle_min = atof(optarg);
        break;
      case 'A':
        angle_max = atof(optarg);
        break;
      case 'd':
        distance_min = atof(optarg);
        break;
      case 'D':
        distance_max = atof(optarg);
        break;
      case 'i':
        interval_i = atoi(optarg);
        break;
      case 'I':
        interval_I = atoi(optarg);
        break;
      case 'l':
        length = atof(optarg) * M_PI;
        break;
      case 'L':
        redraw_length = atof(optarg) * M_PI;
        break;
      case 'g':
        grayscale = TRUE;
        break;
      case 'c':
        star_char = optarg[0];
        break;
      case 's':
        astep = atof(optarg);
        astep_overridden = TRUE;
        break;
      case 'r':
        r = atof(optarg);
        r_overridden = TRUE;
        break;
#ifdef matheval
      case 'x':
      case 'y':
        if ((evalf[opt == 'y'] = evaluator_create(optarg)) == NULL)
        {
          fprintf(stderr, "cannot create evaluator for %c: %s\n", opt, optarg);
          return EXIT_FAILURE;
        }
        break;
#endif
      case 'X':
      case 'Y':
        zoom[opt == 'Y'] = atof(optarg);
        break;
      case 'C':
        direction = -1.0;
        break;
      case 'R':
        no_rotating = TRUE;
        break;
      case 'Z':
        no_zooming = TRUE;
        break;
      case 'h':
        printf(HELP);
        cleanup();
        return EXIT_SUCCESS;
      case 'v':
        printf("%s %s\n", STR(PROGRAM), STR(VERSION));
#ifdef matheval
        printf("with libmatheval\n");
#else
        printf("without libmatheval\n");
#endif
        return EXIT_SUCCESS;
    }
  }

  signal(SIGINT, signal_handler);

  srand(time(NULL));

  initscr();
  keypad(stdscr, TRUE);
  nonl();
  cbreak();
  noecho();
  timeout(0);
  curs_set(0);
  init_colors();
  resize();

  struct timespec req = {
    .tv_sec = 0,
    .tv_nsec = 1000000
  };

  int dcnt = 0;
  long i = 0;
  new_stars(num_stars);
  while (TRUE)
  {
    int c = getch();
    switch (c)
    {
      case KEY_RESIZE:
        resize();
        long j = (i - redraw_length / astep) > 0 ? i - redraw_length / astep : 0;
        for (; j < i; j++)
        {
          expose(j);
        }
        break;
      case ' ':
        dcnt = 0;
        break;
      case 'c':
        clear();
        break;
      case 'q':
      case 'Q':
        cleanup();
        return EXIT_SUCCESS;
    }

    if (dcnt != 0)
    {
      nanosleep(&req, NULL);

      if (dcnt > 0)
      {
        dcnt--;
      }
      continue;
    }

    if (i * astep >= length && length > 0)
    {
      i = 0;
      new_stars(num_stars);
      erase();
    }

    expose(i++);
    refresh();
    dcnt = (i * astep >= length && length > 0) ? interval_I : interval_i;
  }
}
