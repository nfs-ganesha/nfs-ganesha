/**
 *
 * \file    shell_utils.h
 * \author  $Author: leibovic $
 * \date    $Date: 2005/05/11 07:25:45 $
 * \version $Revision: 1.3 $
 * \brief   Miscellaneous build-in commands for shell.
 *
 *
 * $Log: shell_utils.h,v $
 * Revision 1.3  2005/05/11 07:25:45  leibovic
 * adding chomp util.
 *
 * Revision 1.2  2005/05/09 14:54:43  leibovic
 * Adding eq and ne.
 *
 * Revision 1.1  2005/05/09 12:23:55  leibovic
 * Version 2 of ganeshell.
 *
 *
 */

#ifndef _SHELL_UTILS_H
#define _SHELL_UTILS_H

#include "shell_types.h"
#include <stdio.h>

/*----------------------------------*
 *        Utilities commands.
 *----------------------------------*/

/** Timer management. */

int util_timer(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output    /* IN : output stream          */
    );

/** System utils. */

int util_sleep(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output    /* IN : output stream          */
    );

int util_shell(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output    /* IN : output stream          */
    );

int util_meminfo(int argc,      /* IN : number of args in argv */
                 char **argv,   /* IN : arg list               */
                 FILE * output  /* IN : output stream          */
    );

/** String utils */

int util_cmp(int argc,          /* IN : number of args in argv */
             char **argv,       /* IN : arg list               */
             FILE * output      /* IN : output stream          */
    );

int util_diff(int argc,         /* IN : number of args in argv */
              char **argv,      /* IN : arg list               */
              FILE * output     /* IN : output stream          */
    );

int util_wc(int argc,           /* IN : number of args in argv */
            char **argv,        /* IN : arg list               */
            FILE * output       /* IN : output stream          */
    );

int util_chomp(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output    /* IN : output stream          */
    );

/*----------------------------------*
 *        Utilities list.
 *----------------------------------*/

/* util list */

extern command_def_t shell_utils[];

#endif
