/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * ---------------------------------------
 */

/**
 * \file    err_ghost_fs.h
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:03:22 $
 * \version $Revision: 1.15 $
 * \brief   Ghost Filesystem error codes.
 *
 *
 */

#ifndef _ERR_GHOSTFS_H
#define _ERR_GHOSTFS_H

#include "log_macros.h"

static family_error_t __attribute((__unused__)) tab_errstatus_GHOSTFS[] =
{
#define ERR_GHOSTFS_NO_ERROR 0
  {
  ERR_GHOSTFS_NO_ERROR, "ERR_GHOSTFS_NO_ERROR", "No error"},
#define ERR_GHOSTFS_NOENT    2
  {
  ERR_GHOSTFS_NOENT, "ERR_GHOSTFS_NOENT", "No such file or directory"},
#define ERR_GHOSTFS_NOTDIR   3
  {
  ERR_GHOSTFS_NOTDIR, "ERR_GHOSTFS_NOTDIR", "Not a directory"},
#define ERR_GHOSTFS_ACCES   13
  {
  ERR_GHOSTFS_ACCES, "ERR_GHOSTFS_ACCES", "Permission denied"},
#define ERR_GHOSTFS_EXIST   17
  {
  ERR_GHOSTFS_EXIST, "ERR_GHOSTFS_EXIST", "Entry already exist"},
#define ERR_GHOSTFS_ISDIR   21
  {
  ERR_GHOSTFS_ISDIR, "ERR_GHOSTFS_ISDIR", "Directory used in a non-directory operation"},
#define ERR_GHOSTFS_NOTEMPTY   23
  {
  ERR_GHOSTFS_NOTEMPTY, "ERR_GHOSTFS_NOTEMPTY", "Directory is not empty"},
#define ERR_GHOSTFS_INTERNAL 1001
  {
  ERR_GHOSTFS_INTERNAL, "ERR_GHOSTFS_INTERNAL", "GhostFS internal error"},
#define ERR_GHOSTFS_MALLOC   1002
  {
  ERR_GHOSTFS_MALLOC, "ERR_GHOSTFS_MALLOC", "Memory allocation error"},
#define ERR_GHOSTFS_OPEN     1003
  {
  ERR_GHOSTFS_OPEN, "ERR_GHOSTFS_OPEN", "Error opening fislesystem definition file"},
#define ERR_GHOSTFS_READ     1004
  {
  ERR_GHOSTFS_READ, "ERR_GHOSTFS_READ", "Error while reading filesytem definition file"},
#define ERR_GHOSTFS_WRITE    1005
  {
  ERR_GHOSTFS_WRITE, "ERR_GHOSTFS_WRITE",
        "Error while dumping filesytem definition file"},
#define ERR_GHOSTFS_SYNTAX   1006
  {
  ERR_GHOSTFS_SYNTAX, "ERR_GHOSTFS_SYNTAX",
        "Syntax error into filesytem definition file"},
#define ERR_GHOSTFS_ARGS     1007
  {
  ERR_GHOSTFS_ARGS, "ERR_GHOSTFS_ARGS", "Invalid argument"},
#define ERR_GHOSTFS_ALREADYINIT     1008
  {
  ERR_GHOSTFS_ALREADYINIT, "ERR_GHOSTFS_ALREADYINIT",
        "The filesystem has already been loaded"},
#define ERR_GHOSTFS_NOTINIT     1009
  {
  ERR_GHOSTFS_NOTINIT, "ERR_GHOSTFS_NOTINIT", "No filesystem has been loaded"},
#define ERR_GHOSTFS_STALE     1010
  {
  ERR_GHOSTFS_STALE, "ERR_GHOSTFS_STALE", "Invalid file handle"},
#define ERR_GHOSTFS_CORRUPT    1011
  {
  ERR_GHOSTFS_CORRUPT, "ERR_GHOSTFS_CORRUPT", "The filesystem seems to be corrupted"},
#define ERR_GHOSTFS_NOTLNK   1012
  {
  ERR_GHOSTFS_NOTLNK, "ERR_GHOSTFS_NOTLNK", "Not a link"},
#define ERR_GHOSTFS_TOOSMALL   1013
  {
  ERR_GHOSTFS_TOOSMALL, "ERR_GHOSTFS_TOOSMALL", "Buffer too small"},
#define ERR_GHOSTFS_NOTOPENED   1014
  {
  ERR_GHOSTFS_NOTOPENED, "ERR_GHOSTFS_NOTOPENED", "Directory is not opened"},
#define ERR_GHOSTFS_ENDOFDIR   1015
  {
  ERR_GHOSTFS_ENDOFDIR, "ERR_GHOSTFS_ENDOFDIR", "End of directory"},
#define ERR_GHOSTFS_ATTR_NOT_SUPP   1016
  {
  ERR_GHOSTFS_ATTR_NOT_SUPP, "ERR_GHOSTFS_ATTR_NOT_SUPP",
        "Unsupported or read-only attribute"},
  {
  ERR_NULL, "ERR_NULL", ""}
};

#endif /*_ERR_GHOSTFS_H*/
