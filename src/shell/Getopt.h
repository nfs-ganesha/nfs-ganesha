/**
 *
 * \file    Getopt.h
 * \author  $Author: leibovic $
 * \date    $Date: 2005/02/02 09:05:30 $
 * \version $Revision: 1.1 $
 * \brief   Prototype for GANESHA's version of getopt,
 *          to avoid portability issues.
 *
 * $Log: Getopt.h,v $
 * Revision 1.1  2005/02/02 09:05:30  leibovic
 * Adding our own version of getopt, to avoid portability issues.
 *
 */

#ifndef _LOCAL_GETOPT_H
#define _LOCAL_GETOPT_H

extern int Opterr;
extern int Optind;
extern int Optopt;
extern char *Optarg;

int Getopt(int argc, char *argv[], char *opts);

#endif
