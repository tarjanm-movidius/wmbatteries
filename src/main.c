/*
 *    wmbatteries - A dockapp to monitor ACPI status of two batteries
 *    Copyright (C) 2003  Florian Krohs <krohs@uni.de>

 *    Based on work by Thomas Nemeth <tnemeth@free.fr>
 *    Copyright (C) 2002  Thomas Nemeth <tnemeth@free.fr>
 *    and on work by Seiichi SATO <ssato@sh.rim.or.jp>
 *    Copyright (C) 2001,2002  Seiichi SATO <ssato@sh.rim.or.jp>
 *    and on work by Mark Staggs <me@markstaggs.net>
 *    Copyright (C) 2002  Mark Staggs <me@markstaggs.net>

 *    Bugfixes and sysfs operation by Marcell Tarjan <tarjan.marcell@gmail.com>

 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.

 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.

 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "files.h"
#include "defaults.h"
#include "dockapp.h"
#include <signal.h>
#include "backlight_on.xpm"
#include "backlight_off.xpm"
#include "parts.xpm"
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#ifdef linux
# include <sys/stat.h>
# include <X11/XKBlib.h>
#endif

#define CHARGING    3
#define DISCHARGING 1
#define UNKNOWN     0

#define RATE 0
#define TEMP 1

#define NONE     0
#define STATE_OK 1
#define INFO_OK  2
#define BAT_OK   3

#define SIZE      58
#define MAXSTRLEN 512

#ifdef DEBUG
# define DDOT() { printf("."); fflush(stdout); }
# define DPRINTF(...) { printf(__VA_ARGS__); fflush(stdout); }
#else
# define DPRINTF(...)
#endif

typedef struct AcpiInfos {
  const char  driver_version[10];
  int         ac_line_status;
  int         battery_status[2];
  int         battery_percentage[2];
  long        rate[2];
  long        *ratehist[2];
  long        remain[2];
  long        currcap[2];
  int         thermal_temp;
  int         thermal_state;
  int         hours_left;
  int         minutes_left;
  int         low;
} AcpiInfos;

typedef enum { LIGHTOFF, LIGHTON } light;


Pixmap pixmap;
Pixmap backdrop_on;
Pixmap backdrop_off;
Pixmap parts;
Pixmap mask;
static char     *display_name     = "";
static char     light_color[256]  = "";   /* back-light color */
char            tmp_string[256];
static char     *config_file      = NULL; /* name of configfile */
static unsigned update_interval   = UPDATE_INTERVAL;
static light    backlight         = LIGHTOFF;
static unsigned alarm_blink       = ALARM_BLINK;
static unsigned alarm_level       = ALARM_LEVEL;
static unsigned alarm_level_temp  = ALARM_TEMP*10;
static char     *notif_cmd        = NULL;
static char     *suspend_cmd      = NULL;
static char     *standby_cmd      = NULL;
static int      mode              = STATMODE;
static int      togglemode        = TOGGLEMODE;
static int      togglespeed       = TOGGLESPEED;
static int      animationspeed    = ANIMATION_SPEED;
static AcpiInfos cur_acpi_infos;
static int      number_of_batteries = 2;
static char     uevent_files[2][256] = {BAT0_UEVENT_FILE,BAT1_UEVENT_FILE};
static char     thermal[256]      = THERMAL_FILE;
static char     ac_state[256]     = AC_STATE_FILE;
static int      history_size      = RATE_HISTORY;
static int      blink_pos         = 0;

#ifdef linux
# ifndef ACPI_32_BIT_SUPPORT
#  define ACPI_32_BIT_SUPPORT      0x0002
# endif
#endif

/* prototypes */
static void parse_config_file(char *config);
static int update();
static void switch_light();
#ifdef CAPS_NUM_UPD_SPD
static void draw_locks();
#endif
static void draw_remaining_time(AcpiInfos infos);
static void draw_batt(AcpiInfos infos);
static void draw_low();
static void draw_rate(AcpiInfos infos);
static void draw_temp(AcpiInfos infos);
static void draw_statusdigit(AcpiInfos infos);
static void draw_pcgraph(AcpiInfos infos);
static void parse_arguments(int argc, char **argv);
static void print_help(char *prog);
static int  acpi_exists();
static int  my_system (char *cmd);
static void blink_batt();
static void draw_all();

#ifdef linux
int acpi_read(AcpiInfos *i);
void init_stats(AcpiInfos *k);
#endif


int main(int argc, char **argv) {

  XEvent    event;
  XpmColorSymbol  colors[2] = { {"Back0", NULL, 0}, {"Back1", NULL, 0} };
  int       ncolor = 0;
  static unsigned cns_oldstate;
  unsigned  cns_state = 0;
  struct    sigaction sa;
  long      timeout;
  int       charging;

  sa.sa_handler = SIG_IGN;
#ifdef SA_NOCLDWAIT
  sa.sa_flags = SA_NOCLDWAIT;
#else
  sa.sa_flags = 0;
#endif

  printf("wmbatteries %s  (c) Florian Krohs\n"
         "<florian.krohs@informatik.uni-oldenburg.de>\n"
         "This Software comes with absolutely no warranty.\n"
         "Use at your own risk!\n\n", VERSION);

  sigemptyset(&sa.sa_mask);
  sigaction(SIGCHLD, &sa, NULL);

  /* Parse CommandLine */
  parse_arguments(argc, argv);

  /* Check for ACPI support */
  if (!acpi_exists()) {
#ifdef linux
    fprintf(stderr, "No ACPI support in kernel\n");
#else
    fprintf(stderr, "Unable to access ACPI info\n");
#endif
    exit(1);
  }

  /* Initialize Application */
  init_stats(&cur_acpi_infos);
  //acpi_read(&cur_acpi_infos);
  //update();
  dockapp_open_window(display_name, PACKAGE, SIZE, SIZE, argc, argv);
  dockapp_set_eventmask(ButtonPressMask|KeymapStateMask);

  if (strcmp(light_color,"")) {
    colors[0].pixel = dockapp_getcolor(light_color);
    colors[1].pixel = dockapp_blendedcolor(light_color, -24, -24, -24, 1.0);
    ncolor = 2;
  }

  /* change raw xpm data to pixmap */
  if (dockapp_iswindowed)
    backlight_on_xpm[1] = backlight_off_xpm[1] = WINDOWED_BG;

  if (!dockapp_xpm2pixmap(backlight_on_xpm, &backdrop_on, &mask, colors, ncolor)) {
    fprintf(stderr, "Error initializing backlit background image.\n");
    exit(1);
  }
  if (!dockapp_xpm2pixmap(backlight_off_xpm, &backdrop_off, NULL, NULL, 0)) {
    fprintf(stderr, "Error initializing background image.\n");
    exit(1);
  }
  if (!dockapp_xpm2pixmap(parts_xpm, &parts, NULL, colors, ncolor)) {
    fprintf(stderr, "Error initializing parts image.\n");
    exit(1);
  }

  /* shape window */
  if (!dockapp_iswindowed) dockapp_setshape(mask, 0, 0);
  if (mask) XFreePixmap(display, mask);

  /* pixmap : draw area */
  pixmap = dockapp_XCreatePixmap(SIZE, SIZE);

  /* Initialize pixmap */
  if (backlight == LIGHTON)
    dockapp_copyarea(backdrop_on, pixmap, 0, 0, SIZE, SIZE, 0, 0);
  else
    dockapp_copyarea(backdrop_off, pixmap, 0, 0, SIZE, SIZE, 0, 0);

  dockapp_set_background(pixmap);
  update();
  dockapp_show();
  long update_timeout = update_interval;
  long animation_timeout = animationspeed;
  long toggle_timeout = togglespeed;
  int show = 0;
  /* Main loop */
  while (1) {
    if (cur_acpi_infos.battery_status[0]==CHARGING || cur_acpi_infos.battery_status[1]==CHARGING)
      charging = 1;
    else
      charging = 0;
#if CAPS_NUM_UPD_SPD > 0
    timeout = CAPS_NUM_UPD_SPD;
    if (update_timeout<timeout)
#endif
      timeout = update_timeout;
    if (charging && animation_timeout<timeout) timeout = animation_timeout;
    if (togglemode && toggle_timeout<timeout)  timeout = toggle_timeout;

    if (dockapp_nextevent_or_timeout(&event, timeout)) {
      /* Next Event */
      switch (event.type) {
      case ButtonPress:
        switch (event.xbutton.button) {
        case 1: switch_light(); break;
        case 3: mode=!mode; toggle_timeout = togglespeed; show=1; break;
        case 4: /* scroll up */ break;
        case 5: /* scroll dn */ break;
        default: break;
        }
        break;
      case KeymapNotify:
        draw_all();
        dockapp_copy2window(pixmap);
        break;
      default: break;
      }
    } else {
      /* Time Out */
      update_timeout -= timeout;
      if(togglemode) {
        toggle_timeout -= timeout;
        if(toggle_timeout<5) {
          toggle_timeout += togglespeed;
          mode=!mode;
          show = 1;
        }
      }
      if(charging) {
        animation_timeout -= timeout;
        if(animation_timeout<5) {
          animation_timeout += animationspeed;
          blink_batt();
          dockapp_copy2window(pixmap);
        }
      }
      if(update_timeout<5) {
        update_timeout += update_interval;
        if (update()) show = 1;
      }
      XkbGetIndicatorState(display, XkbUseCoreKbd, &cns_state);
      if(cns_state != cns_oldstate) {
        cns_oldstate = cns_state;
        show = 1;
      }
      if(show) {
        /* show */
        draw_all();
        dockapp_copy2window(pixmap);
        show = 0;
      }
    }
  }
  return 0;
}


void init_stats(AcpiInfos *k) {
  int bat_status[2]={NONE,NONE};
  FILE *fd;
  char *buf;
  char *ptr;
  long tmp;
  int i;

  buf=(char *)malloc(sizeof(char)*MAXSTRLEN);
  if(buf == NULL)
    exit(-1);
  /* get info about existing batteries */
  number_of_batteries=0;
  for(i=0; i<2; i++) {
    if((fd = fopen(uevent_files[i], "r"))) {
      bzero(buf, MAXSTRLEN);
      if (fread(buf, 1, MAXSTRLEN, fd)) {
        if ((ptr = strstr(buf,"POWER_SUPPLY_PRESENT="))) {
          if(ptr[21] == '1') {
            bat_status[i] = BAT_OK;
          }
        } else {
          DPRINTF("POWER_SUPPLY_PRESENT not found in '%s'\n", uevent_files[i])
        }
        if ((ptr = strstr(buf,"POWER_SUPPLY_ENERGY_FULL="))) {
          sscanf(ptr+25, "%ld", &k->currcap[i]);
          if((ptr = strstr(buf,"POWER_SUPPLY_ENERGY_NOW="))) {
            sscanf(ptr+24, "%ld", &tmp);
            if (tmp > k->currcap[i]) k->currcap[i] = tmp;
          } else {
            DPRINTF("POWER_SUPPLY_ENERGY_NOW not found in '%s'\n", uevent_files[i])
          }
          if(bat_status[i]==BAT_OK) {
            if((ptr = strstr(buf,"POWER_SUPPLY_ENERGY_FULL_DESIGN="))) {
              sscanf(ptr+32, "%ld", &tmp);
              printf("BAT%d OK, %0.1f%% performance\n", i, (float)k->currcap[i] * 100.0f / (float)tmp);
            } else {
              DPRINTF("POWER_SUPPLY_ENERGY_FULL_DESIGN not found in '%s'\n", uevent_files[i])
            }
            number_of_batteries++;
          }
        } else {
          DPRINTF("POWER_SUPPLY_ENERGY_FULL not found in '%s'\n", uevent_files[i])
        }
        if ((ptr = strstr(buf,"POWER_SUPPLY_POWER_NOW="))) {
          sscanf(ptr+23, "%ld", &k->rate[i]);
        } else {
          DPRINTF("POWER_SUPPLY_POWER_NOW not found in '%s'\n", uevent_files[i])
        }
      }
      fclose(fd);
    } else {
      DPRINTF("D: File not found: '%s'\n", uevent_files[i])
    }
  }
  free(buf);

  if(bat_status[0]!=BAT_OK && bat_status[1]==BAT_OK) {
    strcpy(uevent_files[0], uevent_files[1]);
    k->currcap[0] = k->currcap[1];
    k->rate[0] = k->rate[1];
  }

  DPRINTF("D: %i batter%s found in system\n", number_of_batteries, number_of_batteries==1 ? "y" : "ies");

  // initialising history buffer
  if ((k->ratehist[0] = (long*)malloc(history_size * sizeof(long))) == NULL) exit(-1);
  for (i=0; i<history_size; i++) k->ratehist[0][i] = k->rate[0];
  if (number_of_batteries == 2) {
    if ((k->ratehist[1] = (long*)malloc(history_size * sizeof(long))) == NULL) exit(-1);
    for (i=0; i<history_size; i++) k->ratehist[1][i] = k->rate[1];
  } else k->ratehist[1] = NULL;
  k->ac_line_status = 0;
  k->battery_status[0] = 0;
  k->battery_percentage[0] = 0;
  k->remain[0] = 0;
  k->battery_status[1] = 0;
  k->battery_percentage[1] = 0;
  k->remain[1] = 0;
  k->thermal_temp = 0;
  k->thermal_state = 0;
}


/* called by timer */
static int update() {
  static light pre_backlight;
  static Bool in_alarm_mode = False;
  int ret;

  /* get current battery usage in percent */
  ret = acpi_read(&cur_acpi_infos);

  /* alarm mode */
  if (cur_acpi_infos.low || (cur_acpi_infos.thermal_temp > alarm_level_temp)) {
    if (!in_alarm_mode) {
      pre_backlight = backlight;
      my_system(notif_cmd);
    }
    if (alarm_blink || !in_alarm_mode) {
      switch_light();
      in_alarm_mode = True;
      return 0;
    }
  } else {
    if (in_alarm_mode) {
      in_alarm_mode = False;
      if (backlight != pre_backlight) {
        switch_light();
        return 0;
      }
    }
  }
  return ret;
}


static void parse_config_file(char *config) {

  FILE *fd=NULL;
  char *buf;
  char *item;
  char *value;
  extern int errno;
  int linenr=0;
  int tmp;
  buf=(char *)malloc(sizeof(char)*MAXSTRLEN);
  if(buf == NULL)
    exit(-1);
  if(config != NULL) {
    if((fd = fopen(config, "r"))) {
      DPRINTF("D: using command line given config file '%s'\n", config)
    } else {
      printf("Config file '%s' NOT found\n", config);
    }
  }
  if(fd==NULL) {
    strcpy(buf, getenv("HOME"));
    strcat(buf, "/.wmbatteriesrc");
    if((fd = fopen(buf, "r"))) {
      DPRINTF("D: using config file '%s'\n", buf)
    } else {
      if((fd = fopen("/etc/wmbatteries", "r"))) {
        DPRINTF("D: using config file '/etc/wmbatteries'\n")
      } else {
        DPRINTF("D: no config file found. Using defaults\n")
      }
    }
  }

  if(fd!=NULL) { // some config file was found, try parsing
    while( fgets( buf, 255, fd ) != NULL ) {

      item = strtok( buf, "\t =\n\r" ) ;
      if( item != NULL && item[0] != '#' ) {
        value = strtok( NULL, "\t =\n\r" );
        if(!strcmp(item,"backlight")) {
          if(strcasecmp(value,"yes") && strcasecmp(value,"true") && strcasecmp(value,"false") && strcasecmp(value,"no")) {
            printf("backlight option wrong in line %i,use yes/no or true/false\n",linenr);
          } else {
            if(!strcasecmp(value,"true") || !strcasecmp(value,"yes")) {
              backlight = LIGHTON;
            } else {
              backlight = LIGHTOFF;
            }
          }
        }

        if(!strcmp(item,"alarm_blink")) {
          if(strcasecmp(value,"yes") && strcasecmp(value,"true") && strcasecmp(value,"false") && strcasecmp(value,"no")) {
            printf("alarm_blink option wrong in line %i,use yes/no or true/false\n",linenr);
          } else {
            if(!strcasecmp(value,"true") || !strcasecmp(value,"yes")) {
              alarm_blink = True;
            } else {
              alarm_blink = False;
            }
          }
        }

        if(!strcmp(item,"lightcolor")) {
          strcpy(light_color,value);
        }

        if(!strcmp(item,"temperature")) {
          strcpy(thermal,value);
        }

        if(!strcmp(item,"bat0_uevent")) {
          strcpy(uevent_files[0],value);
        }

        if(!strcmp(item,"bat1_uevent")) {
          strcpy(uevent_files[1],value);
        }

        if(!strcmp(item,"ac_state")) {
          strcpy(ac_state,value);
        }

        if(!strcmp(item,"updateinterval")) {
          tmp=atoi(value);
          if(tmp<100) {
            printf("update interval is out of range in line %i,must be >= 100\n",linenr);
          } else {
            update_interval=tmp;
          }
        }

        if(!strcmp(item,"alarm")) {
          tmp=atoi(value);
          if(tmp<1 || tmp>125) {
            printf("alarm is out of range in line %i,must be > 0 and <= 125\n",linenr);
          } else {
            alarm_level=tmp;
          }
        }

        if(!strcmp(item,"togglespeed")) {
          tmp=atoi(value);
          if(tmp<100) {
            printf("togglespeed variable is out of range in line %i,must be >= 100\n",linenr);
          } else {
            togglespeed=tmp;
          }
        }

        if(!strcmp(item,"animationspeed")) {
          tmp=atoi(value);
          if(tmp<100) {
            printf("animationspeed variable is out of range in line %i,must be >= 100\n",linenr);
          } else {
            animationspeed=tmp;
          }
        }

        if(!strcmp(item,"historysize")) {
          tmp=atoi(value);
          if(tmp<1 || tmp>1000) {
            printf("historysize variable is out of range in line %i,must be >=1 and <=1000\n",linenr);
          } else {
            history_size=tmp;
          }
        }

        if(!strcmp(item,"mode")) {
          if(strcmp(value,"rate") && strcmp(value,"toggle") && strcmp(value,"temp")) {
            printf("mode must be one of rate,temp,toggle in line %i\n",linenr);
          } else {
            togglemode=0;
            if(!strcmp(value,"rate")) mode=RATE;
            if(!strcmp(value,"temp")) mode=TEMP;
            if(!strcmp(value,"toggle")) togglemode=1;
          }
        }
      }
      linenr++;
    }
    fclose(fd);
  }
  free(buf);
}


#ifdef CAPS_NUM_UPD_SPD
static void draw_locks() {
/*
  Returns the turned on leds:
   Bit 0 is Capslock
   Bit 1 is Numlock
   Bit 2 is Scrollock
*/
  unsigned int states;

  XkbGetIndicatorState(display, XkbUseCoreKbd, &states);
  if(states & 0x1) dockapp_copyarea(parts, pixmap, 0, 58, 7, 5, 49, 46);  // CAPS
  if(states & 0x2) dockapp_copyarea(parts, pixmap, 0, 58, 7, 5, 49, 51);  // NUM
}
#endif


static void draw_all() {

  /* all clear */
  if (backlight == LIGHTON)
    dockapp_copyarea(backdrop_on, pixmap, 0, 0, 58, 58, 0, 0);
  else
    dockapp_copyarea(backdrop_off, pixmap, 0, 0, 58, 58, 0, 0);

  /* draw digit */
  draw_remaining_time(cur_acpi_infos);
#ifdef CAPS_NUM_UPD_SPD
  draw_locks();
#endif
  if(mode==RATE) draw_rate(cur_acpi_infos);
  else if(mode==TEMP) draw_temp(cur_acpi_infos);
  draw_statusdigit(cur_acpi_infos);
  draw_pcgraph(cur_acpi_infos);

  if(cur_acpi_infos.low) draw_low();

  draw_batt(cur_acpi_infos);
}


/* called when mouse button pressed */
static void switch_light() {
  switch (backlight) {
  case LIGHTOFF:
    backlight = LIGHTON;
    dockapp_copyarea(backdrop_on, pixmap, 0, 0, 58, 58, 0, 0);
    break;
  case LIGHTON:
    backlight = LIGHTOFF;
    dockapp_copyarea(backdrop_off, pixmap, 0, 0, 58, 58, 0, 0);
    break;
  }

  draw_remaining_time(cur_acpi_infos);
#ifdef CAPS_NUM_UPD_SPD
  draw_locks();
#endif
  if(mode==RATE) draw_rate(cur_acpi_infos);
  else if(mode==TEMP) draw_temp(cur_acpi_infos);
  draw_statusdigit(cur_acpi_infos);
  draw_pcgraph(cur_acpi_infos);
  if(cur_acpi_infos.battery_status[0]==CHARGING || cur_acpi_infos.battery_status[1]==CHARGING) {
    blink_batt();
  } else draw_batt(cur_acpi_infos);
  if(cur_acpi_infos.low) {
    draw_low();
  }
  /* show */
  dockapp_copy2window(pixmap);
}


static void blink_batt() {
  int light_offset=0;
  int bat=0;
  if (backlight == LIGHTON) {
    light_offset=50;
  }
  if (++blink_pos>=5) blink_pos=0;
  for(bat=0;bat<number_of_batteries;bat++) {
    if(cur_acpi_infos.battery_status[bat]==CHARGING) {
      dockapp_copyarea(parts, pixmap, blink_pos*9+light_offset , 117, 9, 5,  16+bat*11, 39);
    }
  }
}


static void draw_batt(AcpiInfos infos) {
  int light_offset=0;
  int bat=0;
  if (backlight == LIGHTON) {
    light_offset=50;
  }
  for(bat=0;bat<number_of_batteries;bat++) {
    if(cur_acpi_infos.battery_status[bat]==CHARGING) {
      dockapp_copyarea(parts, pixmap, blink_pos*9+light_offset, 117, 9, 5,  16+bat*11, 39);
    } else {
      dockapp_copyarea(parts, pixmap, light_offset, 117, 9, 5,  16+bat*11, 39);
    }
  }
}


static void draw_remaining_time(AcpiInfos infos) {
  int y = 0;
  if (backlight == LIGHTON) y = 20;
  if (infos.ac_line_status == 1 && !(cur_acpi_infos.battery_status[0]==CHARGING || cur_acpi_infos.battery_status[1]==CHARGING)) {
    dockapp_copyarea(parts, pixmap, 0, 68+68+y, 10, 20,  17, 5);
    dockapp_copyarea(parts, pixmap, 10, 68+68+y, 10, 20,  32, 5);
  } else {

    dockapp_copyarea(parts, pixmap, (infos.hours_left / 10) * 10, 68+y, 10, 20,  5, 5);
    dockapp_copyarea(parts, pixmap, (infos.hours_left % 10) * 10, 68+y, 10, 20, 17, 5);
    dockapp_copyarea(parts, pixmap, (infos.minutes_left / 10)  * 10, 68+y, 10, 20, 32, 5);
    dockapp_copyarea(parts, pixmap, (infos.minutes_left % 10)  * 10, 68+y, 10, 20, 44, 5);
  }

}


static void draw_low() {
  int y = 0;
  if (backlight == LIGHTON) y = 28;
  dockapp_copyarea(parts, pixmap,42+y , 58, 17, 7, 38, 38);

}


static void draw_temp(AcpiInfos infos) {
  int temp = infos.thermal_temp;
  int light_offset=0;
  if (backlight == LIGHTON) {
    light_offset=50;
  }

  if (temp < 0)  temp = 0;
  if (temp > 999) dockapp_copyarea(parts, pixmap, (temp/1000)*5 + light_offset, 40, 5, 9, 10, 46);
  dockapp_copyarea(parts, pixmap, ((temp/100) % 10)*5 + light_offset, 40, 5, 9, 16, 46);
  dockapp_copyarea(parts, pixmap, ((temp/10) % 10)*5 + light_offset, 40, 5, 9, 22, 46);
  dockapp_copyarea(parts, pixmap, 0, 58, 2, 3, 28, 53);  //.
  dockapp_copyarea(parts, pixmap, (temp%10)*5 + light_offset, 40, 5, 9, 31, 46);
  dockapp_copyarea(parts, pixmap, 10 + light_offset, 49, 5, 9, 37, 46);  //o
  dockapp_copyarea(parts, pixmap, 15 + light_offset, 49, 5, 9, 43, 46);  //C

}


static void draw_statusdigit(AcpiInfos infos) {
  int light_offset=0;
  if (backlight == LIGHTON) {
    light_offset=28;
  }
  if (infos.ac_line_status == 1) {
    dockapp_copyarea(parts, pixmap,33+light_offset , 58, 9, 5, 5, 39);
  }
}


static void draw_rate(AcpiInfos infos) {
  int light_offset=0;
  long rate = (infos.rate[0]+infos.rate[1])/1000;
  if (backlight == LIGHTON) {
    light_offset=50;
  }

  dockapp_copyarea(parts, pixmap, (rate/10000)*5 + light_offset, 40, 5, 9, 5, 46);
  dockapp_copyarea(parts, pixmap, ((rate/1000)%10)*5 + light_offset, 40, 5, 9, 11, 46);
  dockapp_copyarea(parts, pixmap, ((rate/100)%10)*5 + light_offset, 40, 5, 9, 17, 46);
  dockapp_copyarea(parts, pixmap, ((rate/10)%10)*5 + light_offset, 40, 5, 9, 23, 46);
  dockapp_copyarea(parts, pixmap, (rate%10)*5 + light_offset, 40, 5, 9, 29, 46);

  dockapp_copyarea(parts, pixmap, 0 + light_offset, 49, 5, 9, 36, 46);  //m
  dockapp_copyarea(parts, pixmap, 5 + light_offset, 49, 5, 9, 42, 46);  //W

}


static void draw_pcgraph(AcpiInfos infos) {
  int bat;
  int width;
  int light_offset=0;
  if (backlight == LIGHTON) {
    light_offset=5;
  }
  for(bat=0;bat<number_of_batteries;bat++) {
    width = (infos.battery_percentage[bat]*32)/100;
    dockapp_copyarea(parts, pixmap, 0, 58+light_offset, width, 5, 5, 26+6*bat);
    if(infos.battery_percentage[bat] >= 100) { // don't display leading 0
      dockapp_copyarea(parts, pixmap, 4*(infos.battery_percentage[bat]/100), 126+light_offset, 3, 5, 38, 26+6*bat);
    }
    if(infos.battery_percentage[bat] > 9) { //don't display leading 0
      dockapp_copyarea(parts, pixmap, 4*((infos.battery_percentage[bat]%100)/10), 126+light_offset, 3, 5, 42, 26+6*bat);
    }
    dockapp_copyarea(parts, pixmap, 4*(infos.battery_percentage[bat]%10), 126+light_offset, 3, 5, 46, 26+6*bat);
  }

}


static void parse_arguments(int argc, char **argv) {
  int i;
  int integer;
  char character;

  for (i = 1; i < argc; i++) { // first search for config file option
    if (!strcmp(argv[i], "--config") || !strcmp(argv[i], "-c")) {
      if (argc == i + 1) { fprintf(stderr, "%s: error parsing argument for option %s\n", argv[0], argv[i]); exit(1); }
      config_file = argv[i + 1];
      i++;
    }
  }
  // parse config file before other command line options, to allow overriding
  parse_config_file(config_file);
  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
      print_help(argv[0]), exit(0);
    } else if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
      printf("%s version %s\n", PACKAGE, VERSION), exit(0);
    } else if (!strcmp(argv[i], "--display") || !strcmp(argv[i], "-d")) {
      if (argc == i + 1) { fprintf(stderr, "%s: error parsing argument for option %s\n", argv[0], argv[i]); exit(1); }
      display_name = argv[i + 1];
      i++;
    } else if (!strcmp(argv[i], "--backlight") || !strcmp(argv[i], "-bl")) {
      backlight = LIGHTON;
    } else if (!strcmp(argv[i], "--light-color") || !strcmp(argv[i], "-lc")) {
      if (argc == i + 1) { fprintf(stderr, "%s: error parsing argument for option %s\n", argv[0], argv[i]); exit(1); }
      strcpy(light_color,argv[i + 1]);
      i++;
    } else if (!strcmp(argv[i], "--config") || !strcmp(argv[i], "-c")) {
      if (argc == i + 1) { fprintf(stderr, "%s: error parsing argument for option %s\n", argv[0], argv[i]); exit(1); }
      config_file = argv[i + 1];
      i++;
    } else if (!strcmp(argv[i], "--interval") || !strcmp(argv[i], "-i")) {
      if (argc == i + 1) { fprintf(stderr, "%s: error parsing argument for option %s\n", argv[0], argv[i]); exit(1); }
      if (sscanf(argv[i + 1], "%i", &integer) != 1) { fprintf(stderr, "%s: error parsing argument for option %s\n", argv[0], argv[i]); exit(1); }
      if (integer < 100) { fprintf(stderr, "%s: argument %s must be >=100\n", argv[0], argv[i]); exit(1); }
      update_interval = integer;
      i++;
    } else if (!strcmp(argv[i], "--alarm") || !strcmp(argv[i], "-a")) {
      if (argc == i + 1) { fprintf(stderr, "%s: error parsing argument for option %s\n", argv[0], argv[i]); exit(1); }
      if (sscanf(argv[i + 1], "%i", &integer) != 1) { fprintf(stderr, "%s: error parsing argument for option %s\n", argv[0], argv[i]); exit(1); }
      if ( (integer < 0) || (integer > 125) ) { fprintf(stderr, "%s: argument %s must be >=0 and <=125\n", argv[0], argv[i]); exit(1); }
      alarm_level = integer;
      i++;
    } else if (!strcmp(argv[i], "--windowed") || !strcmp(argv[i], "-w")) {
      dockapp_iswindowed = True;
    } else if (!strcmp(argv[i], "--broken-wm") || !strcmp(argv[i], "-bw")) {
      dockapp_isbrokenwm = True;
    } else if (!strcmp(argv[i], "--notify") || !strcmp(argv[i], "-n")) {
      notif_cmd = argv[i + 1];
      i++;
    } else if (!strcmp(argv[i], "--suspend") || !strcmp(argv[i], "-s")) {
      suspend_cmd = argv[i + 1];
      i++;
    } else if (!strcmp(argv[i], "--togglespeed") || !strcmp(argv[i], "-ts")) {
      if (argc == i + 1) { fprintf(stderr, "%s: error parsing argument for option %s\n", argv[0], argv[i]); exit(1); }
      if (sscanf(argv[i + 1], "%i", &integer) != 1) { fprintf(stderr, "%s: error parsing argument for option %s\n", argv[0], argv[i]); exit(1); }
      if (integer < 100) { fprintf(stderr, "%s: argument %s must be >=100\n", argv[0], argv[i]); exit(1); }
      togglespeed=integer;
      i++;
    } else if (!strcmp(argv[i], "--animationspeed") || !strcmp(argv[i], "-as")) {
      if (argc == i + 1) { fprintf(stderr, "%s: error parsing argument for option %s\n", argv[0], argv[i]); exit(1); }
      if (sscanf(argv[i + 1], "%i", &integer) != 1) { fprintf(stderr, "%s: error parsing argument for option %s\n", argv[0], argv[i]); exit(1); }
      if (integer < 100) { fprintf(stderr, "%s: argument %s must be >=100\n", argv[0], argv[i]); exit(1); }
      animationspeed=integer;
      i++;
    } else if (!strcmp(argv[i], "--historysize") || !strcmp(argv[i], "-hs")) {
      if (argc == i + 1) { fprintf(stderr, "%s: error parsing argument for option %s\n", argv[0], argv[i]); exit(1); }
      if (sscanf(argv[i + 1], "%i", &integer) != 1) { fprintf(stderr, "%s: error parsing argument for option %s\n", argv[0], argv[i]); exit(1); }
      if (integer < 1 || integer > 1000) { fprintf(stderr, "%s: argument %s must be >=1 && <=1000\n", argv[0], argv[i]); exit(1); }
      history_size=integer;
      i++;
    } else if (!strcmp(argv[i], "--mode") || !strcmp(argv[i], "-m")) {
      if (argc == i + 1) { fprintf(stderr, "%s: error parsing argument for option %s\n", argv[0], argv[i]); exit(1); }
      if (sscanf(argv[i + 1], "%c", &character) != 1) { fprintf(stderr, "%s: error parsing argument for option %s\n", argv[0], argv[i]); exit(1); }
      if (!(character=='t' || character=='r' || character=='s')) { fprintf(stderr, "%s: argument %s must be t, r or s\n", argv[0], argv[i]); exit(1); }
      if(character=='s') togglemode=1;
      else if(character=='t') mode=TEMP;
      else if(character=='r') mode=RATE;
      i++;
    } else if (!strcmp(argv[i], "--standby") || !strcmp(argv[i], "-S")) {
      if (argc == i + 1) { fprintf(stderr, "%s: error parsing argument for option %s\n", argv[0], argv[i]); exit(1); }
      standby_cmd = argv[i + 1];
      i++;
    } else {
      fprintf(stderr, "%s: unrecognized option '%s'\n", argv[0], argv[i]);
      print_help(argv[0]);
      exit(1);
    }
  }
}


static void print_help(char *prog) {
  printf("Usage: %s [OPTIONS]\n"
   "%s - Window Maker mails monitor dockapp\n"
   "  -d,  --display <string>        display to use\n"
   "  -bl, --backlight               turn on backlight\n"
   "  -lc, --light-color <string>    backlight colour (rgb:6E/C6/3B is default)\n"
   "  -c,  --config <string>         set filename of config file\n"
   "  -i,  --interval <number>       update interval in msec (=%u)\n"
   "  -a,  --alarm <number>          low battery level when to raise alarm (=%u)\n"
   "  -h,  --help                    show this help text and exit\n"
   "  -v,  --version                 show program version and exit\n"
   "  -w,  --windowed                run the application in windowed mode\n"
   "  -bw, --broken-wm               activate broken window manager fix\n"
   "  -n,  --notify <string>         command to launch when alarm is on\n"
   "  -s,  --suspend <string>        set command for acpi suspend\n"
   "  -S,  --standby <string>        set command for acpi standby\n"
   "  -m,  --mode [t|r|s]            set mode for the lower row (=%c), \n"
   "                                 t=temperature, r=current rate, s=toggle\n"
   "  -ts  --togglespeed <int>       set toggle speed in msec (=%u)\n"
   "  -as  --animationspeed <int>    set speed for charging animation in msec (=%u)\n"
   "  -hs  --historysize <int>       set size of history for calculating\n"
   "                                 average power consumption rate (=%u)\n",
   prog, prog, UPDATE_INTERVAL, ALARM_LEVEL, TOGGLEMODE?'s':(STATMODE?'t':'r'), TOGGLESPEED, ANIMATION_SPEED, RATE_HISTORY);
}


int acpi_exists() {
  if (access("/sys/module/acpi", R_OK))
    return 0;
  else
    return 1;
}


static int my_system (char *cmd) {
  int           pid;
  extern char **environ;

  if (cmd == 0) return 1;
  pid = fork ();
  if (pid == -1) return -1;
  if (pid == 0) {
    pid = fork ();
    if (pid == 0) {
      char *argv[4];
      argv[0] = "sh";
      argv[1] = "-c";
      argv[2] = cmd;
      argv[3] = 0;
      execve ("/bin/sh", argv, environ);
      exit (0);
    }
    exit (0);
  }
  return 0;
}


#ifdef linux

int acpi_read(AcpiInfos *i) {
  FILE      *fd;
  static int rhptr = 0;
  int       ret = 0;
  int       bat;
  char      *buf;
  char      *ptr;
  int       hist;
  long      tmp;
  float     time;
  long      allcapacity=0;
  long      allremain=0;

  buf=(char *)malloc(sizeof(char)*MAXSTRLEN);

  /* get acpi thermal cpu info */
  if ((fd = fopen(thermal, "r"))) {
    if (fscanf(fd, "%ld", &tmp)) {
      tmp /= 100;
      if (i->thermal_temp != tmp) {
        i->thermal_temp = tmp;
        ret = 1;
      }
    } else {
      DPRINTF("fscanf(%s) error\n", thermal)
    }
    fclose(fd);
  } else {
    DPRINTF("fopen(%s) error\n", thermal)
  }

  /* get ac power state */
  if ((fd = fopen(ac_state, "r"))) {
    if (fread(buf,1,1,fd)) {
      buf[0] -= '0';
      if(buf[0] != i->ac_line_status) {
        i->ac_line_status = buf[0];
        ret = 1;
      }
    } else {
      DPRINTF("fread(%s) error\n", ac_state)
    }
    fclose(fd);
  } else {
    DPRINTF("fopen(%s) error\n", ac_state)
  }

  /* get battery statuses */
  for(bat=0;bat<number_of_batteries;bat++) {
    i->ratehist[bat][rhptr] = 0;
    if ((fd = fopen(uevent_files[bat], "r"))) {
      bzero(buf, MAXSTRLEN);
      if (fread(buf, 1, MAXSTRLEN, fd)) {
        if ((ptr = strstr(buf,"POWER_SUPPLY_STATUS"))) {
          switch (ptr[20]) {
            case 'D': tmp=DISCHARGING; break;
            case 'C': tmp=CHARGING; break;
            default: tmp=UNKNOWN; break;
          }
          if (i->battery_status[bat] != tmp) {
            i->battery_status[bat] = tmp;
            ret = 1;
          }
        } else {
          DPRINTF("POWER_SUPPLY_STATUS not found\n")
        }
        if ((ptr = strstr(ptr,"POWER_SUPPLY_POWER_NOW"))) {
          sscanf(ptr+23, "%ld", &i->ratehist[bat][rhptr]);
        } else {
          DPRINTF("POWER_SUPPLY_POWER_NOW not found (order?)\n")
        }
        if ((ptr = strstr(ptr,"POWER_SUPPLY_ENERGY_NOW"))) {
          sscanf(ptr+24, "%ld", &tmp);
          if (i->remain[bat] != tmp) {
            i->remain[bat] = tmp;
            i->battery_percentage[bat] = (((float)(i->remain[bat])*100.0f)/(float)cur_acpi_infos.currcap[bat]);
            if (i->battery_percentage[bat] > 100) i->battery_percentage[bat] = 100;
            ret = 1;
          }
        } else {
          DPRINTF("POWER_SUPPLY_ENERGY_NOW not found (order?)\n")
        }
      } else {
        DPRINTF("fread(%s) error\n", uevent_files[bat])
      }
      fclose(fd);
    } else {
      DPRINTF("fopen(%s) error\n", uevent_files[bat])
    }

    /* calc average */
    tmp = 0;
    if (i->ratehist[bat][rhptr] != 0) {
      for(hist=0; hist<history_size; hist++) {
        if (i->ratehist[bat][hist] > 0)
          tmp += i->ratehist[bat][hist];
        else
          tmp += i->ratehist[bat][rhptr];
      }
      tmp /= history_size;
    }
    if (i->rate[bat] != tmp) {
      i->rate[bat] = tmp;
      ret = 1;
    }

  }
  if (++rhptr >= history_size) rhptr = 0;

  if (ret) {
    /* calc remaining time (only if something has changed) */
    if((i->battery_status[0] == DISCHARGING || i->battery_status[1] == DISCHARGING) && (i->rate[0] + i->rate[1]) > 0) {
      time = (float)(i->remain[0]+i->remain[1])/(float)(i->rate[0]+i->rate[1]);
      i->hours_left=(int)time;
      i->minutes_left=(int)((time-(int)time)*60);
    }
    if(i->battery_status[0]==0 && i->battery_status[1]==0) {
      i->hours_left=0;
      i->minutes_left=0;
    }
    if((i->battery_status[0] == CHARGING || i->battery_status[1] == CHARGING) && (i->rate[0]>0 || i->rate[1]>0)) {
      time = (float)(cur_acpi_infos.currcap[0] - i->remain[0] + cur_acpi_infos.currcap[1] - i->remain[1])/(float)(i->rate[0]+i->rate[1]);
      i->hours_left=(int)time;
      i->minutes_left=(int)(60*(time-(int)time));
    }
    for(bat=0;bat<number_of_batteries;bat++) {
      allremain += i->remain[bat];
      allcapacity += cur_acpi_infos.currcap[bat];
    }

    cur_acpi_infos.low=0;
    if(allcapacity>0) {
      if(((float)allremain/(float)allcapacity)*100<alarm_level) {
        cur_acpi_infos.low=1;
      }
    }
  }

  free(buf);
  return ret;
}
#endif
