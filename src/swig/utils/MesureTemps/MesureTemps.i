// File : MesureTemps.i
%module MesureTemps
%{
#include "MesureTemps.h"
%}

%include "MesureTemps.h"

%inline %{

/* Time functions */
void StartTime(struct Temps * start_time)
{
  MesureTemps(start_time, NULL);
}

void EndTime(struct Temps * start_time, struct Temps * end_time)
{
  MesureTemps(end_time, start_time);
}

%}
