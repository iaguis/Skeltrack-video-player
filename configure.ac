#
# configure.ac
#
# Skeltrack-Video-Player
# Copyright (C) 2013 Iago López Galeiras
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

AC_PREREQ([2.59])
AC_INIT([SKELTRACK_VIDEO_PLAYER], [0.1], [iaguis@gmail.com])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_CONFIG_FILES([Makefile src/Makefile])

# Checks for libraries.
PKG_CHECK_MODULES(SKELTRACK, skeltrack-0.1 >= 0.1.10)

CLUTTER_REQUIRED=1.8.4
CAIRO_REQUIRED=1.10.2
PKG_CHECK_MODULES(VIDEO_PLAYER_DEPS, clutter-1.0 >= CLUTTER_REQUIRED
                                     cairo >= CAIRO_REQUIRED)

# Checks for header files.
AC_CHECK_HEADERS([string.h])

AC_OUTPUT
