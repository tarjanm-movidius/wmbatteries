/*
 *    wmbatteries - A dockapp to monitor ACPI status of two batteries
 *    Copyright (C) 2003  Florian Krohs <krohs@uni.de>

 *    Based on work by Thomas Nemeth <tnemeth@free.fr>
 *    Copyright (C) 2002  Thomas Nemeth <tnemeth@free.fr>
 *    and on work by Seiichi SATO <ssato@sh.rim.or.jp>
 *    Copyright (C) 2001,2002  Seiichi SATO <ssato@sh.rim.or.jp>
 *    and on work by Mark Staggs <me@markstaggs.net>
 *    Copyright (C) 2002  Mark Staggs <me@markstaggs.net>

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
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define THERMAL_FILE "/proc/acpi/thermal_zone/THM0/temperature"
#define BAT0_STATE_FILE "/proc/acpi/battery/BAT0/state"
#define BAT1_STATE_FILE "/proc/acpi/battery/BAT1/state"
#define BAT0_INFO_FILE "/proc/acpi/battery/BAT0/info"
#define BAT1_INFO_FILE "/proc/acpi/battery/BAT1/info"
#define AC_STATE_FILE "/proc/acpi/ac_adapter/AC/state"
