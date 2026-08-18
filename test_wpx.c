#include <stdio.h>
#include <string.h>
#include "qsoz_util.h"

typedef struct { const char *call; const char *prefix; } Test;

int main(void) {
  static const Test t[]={
    {"N8BJQ","N8"},{"N8BJQ/3","N3"},{"PA/N8BJQ","PA0"},
    {"XEFTJW","XE0"},{"OL25LP","OL25"},{"DL60CHILD","DL60"},
    {"9A800VZ","9A800"},{"DR2006Q","DR2006"},{"LY1000CW","LY1000"},
    {"KL7RA/WK9","WK9"},{"OE/K5ZD","OE0"},{"KH6XXX/4","KH4"},
    {"IK4LZH/P","IK4"},{"IK4LZH/MM","IK4"},{"EA8/IK4LZH/P","EA8"},
    {"4U1UN","4U1"},{"3D2CR","3D2"}
  };
  int i,n;
  const char *p;

  n=(int)(sizeof(t)/sizeof(t[0]));
  for(i=0;i<n;i++){
    p=qsoz_wpx(t[i].call);
    printf("%-14s -> %-8s expected %-8s %s\n",t[i].call,p,t[i].prefix,strcmp(p,t[i].prefix)==0?"OK":"FAIL");
    if(strcmp(p,t[i].prefix)!=0)return 1;
  }
  return 0;
}
