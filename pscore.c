// Gianluca Mazzini @2022- Version 3.02
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mysql/mysql.h>
#include "qsoz_stats.h"
#include "qsoz_util.h"
#include "qsoz_db.h"
#include "pscore.h"
#include "/home/tools/mcp/work/data/radio_data.h"


static const char *conid[]={"CQWWSSB","CQWWCW","CQWPXSSB","CQWPXCW","CQWWDIGI","4080","IARUHF","CQ160SSB","CQ160CW","SPDX","LZDX","OKOMSSB","OKOMCW","HADX","ARIDX","KOSSSB","KOSCW","RDAC","ARRLSSB","ARRLCW","RDXC","JIDXSSB","JIDXCW","YODX","CQM","WAESSB","WAECW","WAERTTY","CQ28","UBASSB","UBACW","IOTA","EUHF","ARISEZ","EURASIA","WAG","CQWPXRTTY","SACSSB","SACCW","PACC","AASSB","AACW","HOLYLANDDX","EUDX","UNDX","URDXC","CQBB","BSC","RRTC","UCC","PADANG","ARRL10","ARRLRU","ARRLRTTY","FTROUNDUP","RCC","ARKTIKA","9ADX","EIUKDXSSB","EIUKDXCW","RAC","ARRLFIELDDAY"};
static int cqz[1000],ituz[1000],contype=-1;
static char cont[1000][2];
static int home_dxcc=-1;

static int uba_eu_dxcc(int dxcc) {
  static const int eu[]={5,21,40,45,52,63,79,84,145,146,149,167,180,206,214,215,221,224,225,227,230,236,239,245,248,254,256,257,263,269,272,275,281,284,453,497,499,503,504};
  int i,n;

  n=(int)(sizeof(eu)/sizeof(eu[0]));
  for(i=0;i<n;i++)if(dxcc==eu[i])return 1;
  return 0;
}

static const char *uba_prefix(const char *callsign) {
  static char out[4];
  int i;

  out[0]='\0';
  if(callsign==NULL || !isalpha((unsigned char)callsign[0]) || !isalpha((unsigned char)callsign[1]))return out;
  out[0]=(char)toupper((unsigned char)callsign[0]);
  out[1]=(char)toupper((unsigned char)callsign[1]);
  for(i=2;callsign[i]!='\0';i++){
    if(isdigit((unsigned char)callsign[i])){
      out[2]=callsign[i];
      out[3]='\0';
      return out;
    }
    if(!isalpha((unsigned char)callsign[i]))break;
  }
  out[0]='\0';
  return out;
}

static const char *uba_section(const char *exchange) {
  static char out[4];
  size_t n;
  int i,j;

  out[0]='\0';
  if(exchange==NULL)return out;
  n=strlen(exchange);
  if(n<3)return out;
  j=0;
  for(i=(int)n-1;i>=0 && j<3;i--){
    if(isalpha((unsigned char)exchange[i]))out[2-j++]=(char)toupper((unsigned char)exchange[i]);
    else if(j>0)break;
  }
  if(j!=3){out[0]='\0'; return out;}
  out[3]='\0';
  if(strcmp(out,"XXX")==0)out[0]='\0';
  return out;
}

int conscore_supported(const char *contest) {
  int i,n;

  if(contest==NULL)return 0;
  n=(int)(sizeof(conid)/sizeof(conid[0]));
  for(i=0;i<n;i++)if(strncmp(contest,conid[i],strlen(conid[i]))==0)return 1;
  return 0;
}

void conscore_setup(MYSQL *con,char tok[][100],char *mycall){
  int vv,c;
  RadioCty cty;
  char buf[1000];
  MYSQL_RES *res;
  MYSQL_ROW row;
  
  vv=sizeof(conid)/sizeof(conid[0]);
  for(contype=0;contype<vv;contype++)if(strncmp(tok[9],conid[contype],strlen(conid[contype]))==0)break;
  if(contype==vv){
    contype=-1;
    return;
  }
  qsoz_stats_reset();
  if(radio_cty_lookup(con,mycall,&cty)!=1){home_dxcc=-1; contype=-1; return;}
  home_dxcc=atoi(cty.dxcc);
  if(home_dxcc<0 || home_dxcc>=1000){home_dxcc=-1; contype=-1; return;}
  memset(cont,0,sizeof(cont));
  memset(cqz,0,sizeof(cqz));
  memset(ituz,0,sizeof(ituz));
  sprintf(buf,"select dxcc,cont,cqzone,ituzone from cty");
  mysql_query(con,buf);
  res=mysql_use_result(con);
  for(;;){
    row=mysql_fetch_row(res);
    if(row==NULL)break;
    c=atoi(row[0]);
    if(c<0 || c>=1000 || row[1]==NULL || row[2]==NULL || row[3]==NULL)continue;
    strncpy(cont[c],row[1],2);
    cqz[c]=atoi(row[2]);
    ituz[c]=atoi(row[3]);
  }
  mysql_free_result(res);
}

void conscore(MYSQL *con,char tok[][100],char *mycall,long long start,long long end){
  int c,vv,d,e,n,z,score,uba_new;
  long uba_total,uba_belgian,uba_belgian_points,uba_bonus;
  char buf[1000],aux1[300],aux2[300],aux3[300],aux4[300],aux5[300],esc_contest[256],esc_mycall[64],*p;
  const char *area,*section,*prefix;
  MYSQL_RES *res;
  MYSQL_ROW row;
  double lat1,lat2,lon1,lon2;
  time_t epoch;
  struct tm *t;

  if(contype==-1)return;
  uba_total=0;
  uba_belgian=0;
  uba_belgian_points=0;
  uba_bonus=0;
  if(!qsoz_db_escape(con,esc_contest,sizeof(esc_contest),tok[9]) || !qsoz_db_escape(con,esc_mycall,sizeof(esc_mycall),mycall))return;
  sprintf(buf,"select callsign,freqtx,dxcc,contesttx,contestrx,mode,open from log where contest='%s' and mycall='%s' and open>=%lld and open<=%lld order by open desc",esc_contest,esc_mycall,start,end);
  mysql_query(con,buf);
  res=mysql_use_result(con);
  for(;;){
    row=mysql_fetch_row(res);
    if(row==NULL)break;
    c=qsoz_band((int)(atol(row[1])/1000000.0))/10;
    vv=atoi(row[2]);
    if(vv<0 || vv>=1000)continue;
    switch(contype){
      case 0: // CQWWSSB
      case 1: { // CQWWCW
        sprintf(aux1,"%03d:%s",c,row[0]);
        incdata3(0,0,aux1,1,0);
        if(strncmp(cont[vv],cont[home_dxcc],2)!=0)incdata3(0,1,aux1,3,0);
        else if(strncmp(cont[vv],"NA",2)==0 && strncmp(cont[home_dxcc],"NA",2)==0 && home_dxcc!=vv)incdata3(0,1,aux1,2,0);
        else if(strncmp(cont[vv],cont[home_dxcc],2)==0 && home_dxcc!=vv)incdata3(0,1,aux1,1,0);
        else incdata3(0,1,aux1,0,0);
        sprintf(aux2,"%03d:%d",c,vv);
        sprintf(aux3,"%03d:Z%d",c,cqz[vv]);
        incdata3(0,2,aux2,1,0); incdata3(0,2,aux3,1,0);
        incdata3(0,3,aux2,1,0); incdata3(0,3,aux3,1,0);
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 2: // CQWPXSSB
      case 3: { // CQWPXCW
        sprintf(aux1,"%03d:%s",c,row[0]);
        incdata3(0,0,aux1,1,0);
        if(home_dxcc==vv)incdata3(0,1,aux1,1,0);
        else if(strncmp(cont[vv],cont[home_dxcc],2)!=0){if(c<=20)incdata3(0,1,aux1,3,0); else incdata3(0,1,aux1,6,0);}
        else if(strncmp(cont[vv],"NA",2)==0 && strncmp(cont[home_dxcc],"NA",2)==0){if(c<=20)incdata3(0,1,aux1,2,0); else incdata3(0,1,aux1,4,0);}
        else {if(c<=20)incdata3(0,1,aux1,1,0); else incdata3(0,1,aux1,2,0);}
        sprintf(aux2,"%03d:%s",c,qsoz_wpx(row[0]));
        sprintf(aux3,"ALL:%s",qsoz_wpx(row[0]));
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux3,1,0);
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 4: { // CQWWDIGI
        sprintf(aux1,"%03d:%s",c,row[0]);
        lat1=((row[3][1]-'A')*10.0+(row[3][3]-'0')+1.0/48.0-90.0);
        lon1=-((row[3][0]-'A')*20.0+(row[3][2]-'0')*2.0+1.0/24.0-180.0);
        lat2=((row[4][1]-'A')*10.0+(row[4][3]-'0')+1.0/48.0-90.0);
        lon2=-((row[4][0]-'A')*20.0+(row[4][2]-'0')*2.0+1.0/24.0-180.0);
        score=1+(int)(radio_distance_km(lat1,lon1,lat2,lon2)/3000.0);
        incdata3(0,0,aux1,1,0);
        incdata3(0,1,aux1,score,0);
        sprintf(aux2,"%03d:%.2s",c,row[4]);
        sprintf(aux3,"%03d:%.2s",c,row[4]);
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux3,1,0);
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 5: { // 4080
        strcpy(aux5,qsoz_mode(row[5]));
        sprintf(aux1,"%02d%2s:%s",c,aux5,row[0]);
        incdata3(0,0,aux1,1,0);
        if(strncmp(aux5,"PH",2)==0)incdata3(0,1,aux1,1,0);
        else if(strncmp(aux5,"DG",2)==0)incdata3(0,1,aux1,2,0);
        else if(strncmp(aux5,"CW",2)==0)incdata3(0,1,aux1,3,0);
        sprintf(aux2,"%02d%2s:%.2s",c,aux5,row[4]);
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        sprintf(aux4,"%02d%2s",c,aux5);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 6: { // IARUHF
        sprintf(aux1,"%03d:%s",c,row[0]);
        incdata3(0,0,aux1,1,0);
        if(!isdigit(row[4][0]))incdata3(0,1,aux1,1,0);
        else if(strncmp(cont[vv],cont[home_dxcc],2)!=0)incdata3(0,1,aux1,5,0);
        else if(ituz[home_dxcc]!=ituz[vv])incdata3(0,1,aux1,3,0);
        else incdata3(0,1,aux1,1,0);
        if(!isdigit(row[4][0]))sprintf(aux2,"%03d:%s",c,row[4]); else sprintf(aux2,"%03d:%d",c,ituz[vv]);
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 7: // CQ160SSB
      case 8: { // CQ160CW
        sprintf(aux1,"%03d:%s",c,row[0]);
        incdata3(0,0,aux1,1,0);
        if(strncmp(cont[vv],cont[home_dxcc],2)!=0)incdata3(0,1,aux1,10,0);
        else if(vv!=home_dxcc)incdata3(0,1,aux1,5,0);
        else incdata3(0,1,aux1,2,0);
        if(!isdigit(row[4][0]))sprintf(aux2,"%03d:%s",c,row[4]); else sprintf(aux2,"%03d:%d",c,vv);
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 9: { // SPDX SP=269
        if((home_dxcc==269 && vv==269) || (home_dxcc!=269 && vv!=269))break;
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(home_dxcc==269){if(strncmp(cont[vv],"EU",2)!=0)incdata3(0,1,aux1,3,0); else if(vv!=269)incdata3(0,1,aux1,1,0);}
        else if(vv==269)incdata3(0,1,aux1,3,0);
        if(home_dxcc==269)sprintf(aux2,"%03d:%d",c,vv); else if(vv==269)sprintf(aux2,"%03d:%s",c,row[4]);
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 10: { // LZDX LZ=212
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(vv==212){if(home_dxcc==212)incdata3(0,1,aux1,1,0); else incdata3(0,1,aux1,10,0);}
        else {if(strncmp(cont[vv],cont[home_dxcc],2)!=0)incdata3(0,1,aux1,3,0); else incdata3(0,1,aux1,1,0);}
        if(home_dxcc==212){
          sprintf(aux2,"%03d:%d",c,vv);
          sprintf(aux3,"%03d:Z%d",c,ituz[vv]);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
          incdata3(0,2,aux3,1,0); incdata3(0,3,aux3,1,0);
        }
        else {
          sprintf(aux2,"%03d:Z%d",c,ituz[vv]);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
          if(vv==212){
            sprintf(aux2,"%03d:%s",c,row[4]);
            incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
          }
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 11: // OKOMSSB OK=503 OM=504
      case 12: { // OKOMCW OK=503 OM=504
        sprintf(aux1,"%03d:%s",c,row[0]);
        incdata3(0,0,aux1,1,0);
        if(home_dxcc==503||home_dxcc==504){
          if(home_dxcc==vv)incdata3(0,1,aux1,2,0);
          else if(strncmp(cont[vv],cont[home_dxcc],2)==0)incdata3(0,1,aux1,3,0);
          else incdata3(0,1,aux1,5,0);
        }
        else {
          if(vv==503||vv==504)incdata3(0,1,aux1,10,0);
          else if(home_dxcc==vv)incdata3(0,1,aux1,1,0);
          else if(strncmp(cont[vv],cont[home_dxcc],2)==0)incdata3(0,1,aux1,3,0);
          else incdata3(0,1,aux1,5,0);
        }
        if(home_dxcc==503||home_dxcc==504)sprintf(aux2,"%03d:%s",c,row[4]);
        else sprintf(aux2,"%03d:%d",c,vv);
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 13: { // HADX HA=239
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(vv==239)incdata3(0,1,aux1,10,0);
        else if(strncmp(cont[vv],cont[home_dxcc],2)!=0)incdata3(0,1,aux1,5,0);
        else incdata3(0,1,aux1,2,0);
        if(vv!=239){
          sprintf(aux2,"%03d:%d",c,vv);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        if(vv==239){
          sprintf(aux2,"%03d:%s",c,row[4]);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 14: { // ARIDX Italy=248 Sardinia=225
        if((home_dxcc==248 || home_dxcc==225) && (vv==248 || vv==225))break;
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(home_dxcc==248 || home_dxcc==225){
          if(strncmp(cont[vv],cont[home_dxcc],2)!=0)incdata3(0,1,aux1,3,0);
          else incdata3(0,1,aux1,1,0);
        }
        else {
          if(home_dxcc==vv)incdata3(0,1,aux1,0,0);
          else if(vv==248 || vv==225)incdata3(0,1,aux1,10,0);
          else if(strncmp(cont[vv],cont[home_dxcc],2)!=0)incdata3(0,1,aux1,3,0);
          else incdata3(0,1,aux1,1,0);
        }
        if(vv==248 || vv==225){
          sprintf(aux2,"%03d:%s",c,row[4]);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        else {
          sprintf(aux2,"%03d:%d",c,vv);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 15: // KOSSSB EA=281 EA6=21 EA9=32 EA8=29
      case 16: { // KOSCW EA=281 EA6=21 EA9=32 EA8=29
        sprintf(aux1,"%03d:%s",c,row[0]);
        incdata3(0,0,aux1,1,0);
        if(home_dxcc==281 || home_dxcc==21 || home_dxcc==32 || home_dxcc==29){
          if(vv==281 || vv==21 || vv==32 || vv==29)incdata3(0,1,aux1,2,0);
          else incdata3(0,1,aux1,1,0);
        }
        else {
          if(vv==281 || vv==21 || vv==32 || vv==29)incdata3(0,1,aux1,3,0);
          else incdata3(0,1,aux1,1,0);
        }
        sprintf(aux2,"%03d:%d",c,vv);
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        if(vv==281 || vv==21 || vv==32 || vv==29){
          sprintf(aux2,"%03d:%s",c,row[4]);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 17: { // RDAC UAinEU=54 UAinAS=15
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(home_dxcc==54 || home_dxcc==15){
          if(vv==home_dxcc)incdata3(0,1,aux1,1,0);
          else if((vv==54 || vv==15) && vv!=home_dxcc)incdata3(0,1,aux1,2,0);
          else if(strncmp(cont[vv],cont[home_dxcc],2)==0)incdata3(0,1,aux1,3,0);
          else incdata3(0,1,aux1,5,0);
        }
        else {
          if(vv==54 || vv==15)incdata3(0,1,aux1,10,0);
        }
        if(home_dxcc==54 || home_dxcc==15){
          sprintf(aux2,"%03d:%d",c,vv);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        if(vv==54 || vv==15){
          sprintf(aux2,"%03d:%s",c,row[4]);
          sprintf(aux3,"ALL:%s",row[4]);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux3,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 18: // ARRLSSB W=291 VE=1
      case 19: { // ARRLCW W=291 VE=1
        sprintf(aux1,"%03d:%s",c,row[0]);
        incdata3(0,0,aux1,1,0);
        incdata3(0,1,aux1,3,0);
        if(home_dxcc==291 || home_dxcc==1){
          sprintf(aux2,"%03d:%d",c,vv);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        else {
          sprintf(aux2,"%03d:%s",c,row[4]);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 20: { // RDXC UAinEU=54 UAinAS=15
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(home_dxcc==54 || home_dxcc==15){
          if(vv==home_dxcc)incdata3(0,1,aux1,2,0);
          else if((vv==54 || vv==15) && strncmp(cont[vv],cont[home_dxcc],2)!=0)incdata3(0,1,aux1,5,0);
          else if(strncmp(cont[vv],cont[home_dxcc],2)==0)incdata3(0,1,aux1,3,0);
          else incdata3(0,1,aux1,5,0);
        }
        else {
          if(vv==54 || vv==15)incdata3(0,1,aux1,10,0);
          else if(home_dxcc==vv)incdata3(0,1,aux1,2,0);
          else if(strncmp(cont[vv],cont[home_dxcc],2)==0)incdata3(0,1,aux1,3,0);
          else incdata3(0,1,aux1,5,0);
        }
        sprintf(aux2,"%03d:%d",c,vv);
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        if(vv==54 || vv==15){
          sprintf(aux2,"%03d:%s",c,row[4]);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 21: // JIDXSSB JA=339
      case 22: { // JIDXCW JA=339
        if((home_dxcc==339 && vv==339) || (home_dxcc!=339 && vv!=339))break;
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(c==160)incdata3(0,1,aux1,4,0);
        else if(c==80 || c==10)incdata3(0,1,aux1,2,0);
        else if(c==40 || c==20 || c==15)incdata3(0,1,aux1,1,0);
        if(home_dxcc==339){
          sprintf(aux2,"%03d:%d",c,vv);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
          sprintf(aux2,"%03d:%d",c,cqz[vv]);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        else {
          sprintf(aux2,"%03d:%s",c,row[4]);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 23: { // YODX YO=275 
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(home_dxcc==275){
          if(vv==275)incdata3(0,1,aux1,0,0);
          else if(strncmp(cont[vv],"EU",2)==0)incdata3(0,1,aux1,4,0);
          else incdata3(0,1,aux1,8,0);
        }
        else {
          if(vv==275)incdata3(0,1,aux1,8,0);
          else if(strncmp(cont[vv],cont[home_dxcc],2)!=0)incdata3(0,1,aux1,4,0);
          else if(vv==home_dxcc)incdata3(0,1,aux1,1,0);
          else incdata3(0,1,aux1,2,0);
        }
        sprintf(aux2,"%03d:%d",c,vv);
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        if(home_dxcc!=275 && vv==275){
          sprintf(aux2,"%03d:%s",c,row[4]);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 24: { // CQM
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(strncmp(cont[vv],cont[home_dxcc],2)==0)incdata3(0,1,aux1,2,0);
        else if(strncmp(cont[home_dxcc],"EU",2)==0 && strncmp(cont[vv],"AS",2)==0)incdata3(0,1,aux1,2,0);
        else if(strncmp(cont[vv],"EU",2)==0 && strncmp(cont[home_dxcc],"AS",2)==0)incdata3(0,1,aux1,2,0);
        else incdata3(0,1,aux1,3,0);
        sprintf(aux2,"%03d:%d",c,vv);
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 25: // WAESSB (no QTC)
      case 26: // WAECW (no QTC)
      case 27: { // WAERTTY (no QTC)
        sprintf(aux1,"%03d:%s",c,row[0]);
        incdata3(0,0,aux1,1,0);
        incdata3(0,1,aux1,1,0);
        d=0; if(c==80)d=4; else if(c==40)d=3; else if(c==20||c==15||c==10)d=2;
        if(strncmp(cont[home_dxcc],"EU",2)!=0){
          if(strncmp(cont[vv],"EU",2)==0){
            sprintf(aux2,"%03d:%d",c,vv);
            incdata3(0,2,aux2,d,0); incdata3(0,3,aux2,d,0);
          }
        }
        else {
          if(strncmp(cont[vv],"EU",2)!=0){
            if(vv==291||vv==1||vv==150||vv==170||vv==462||vv==339||vv==318||vv==108){
              for(p=row[0]+1;*p!='\0';p++)if(isdigit(*p))break;
              sprintf(aux2,"%03d:%d:%c",c,vv,*p);
            }
            else sprintf(aux2,"%03d:%d",c,vv);
            incdata3(0,2,aux2,d,0); incdata3(0,3,aux2,d,0);
          }
        }                                
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 28: { // CQ28
        if(home_dxcc!=248||vv!=248)break;
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(strlen(row[4])==2){
          sprintf(aux2,"%03d:%s",c,row[4]);
          if(numdata3(0,2,aux2)==0)incdata3(0,1,aux1,5,0);
          else incdata3(0,1,aux1,1,0);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        else {
          incdata3(0,1,aux1,10,0);
          sprintf(aux2,"%03d:%.2s",c,row[4]);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
          sprintf(aux2,"%03d:%d",c,atoi(row[4]+2));
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 29: // UBASSB ON=209
      case 30: { // UBACW ON=209
        sprintf(aux1,"%03d:%s",c,row[0]);
        uba_new=numdata3(0,0,aux1)==0;
        if(uba_new){
          uba_total++;
          if(vv==209){
            uba_belgian++;
            uba_belgian_points+=10;
          }
        }
        incdata3(0,0,aux1,1,0);
        if(home_dxcc==209){
          if(vv==209)score=1;
          else if(uba_eu_dxcc(vv))score=2;
          else if(vv==54 || vv==15 || vv==27)score=strncmp(cont[vv],"EU",2)==0?2:3;
          else score=3;
        }
        else {
          if(vv==209)score=10;
          else if(uba_eu_dxcc(vv))score=3;
          else if(vv==54 || vv==15 || vv==27)score=strncmp(cont[vv],"EU",2)==0?3:1;
          else score=1;
        }
        incdata3(0,1,aux1,score,0);
        if(home_dxcc==209){
          sprintf(aux2,"%03d:%d",c,vv);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        else if(vv==209){
          prefix=uba_prefix(row[0]);
          if(prefix[0]!='\0'){
            sprintf(aux2,"%03d:P%s",c,prefix);
            incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
          }
          section=uba_section(row[4]);
          if(section[0]!='\0'){
            sprintf(aux2,"%03d:S%s",c,section);
            incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
          }
        }
        else if(uba_eu_dxcc(vv)){
          sprintf(aux2,"%03d:%d",c,vv);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 31: { // IOTA (no iscland)
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        for(p=row[4];*p!='\0';p++)if(!isdigit(*p))break;
        if(*p!='\0')incdata3(0,1,aux1,15,0);
        else incdata3(0,1,aux1,2,0);
        if(*p!='\0'){
          sprintf(aux2,"%03d:%s:%s",c,p,qsoz_mode(row[5]));
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 32: { // EUHF
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        incdata3(0,1,aux1,1,0);
        sprintf(aux2,"%03d:%s",c,row[4]);
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 33: { // ARISEZ
        if(home_dxcc!=248||vv!=248)break;
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(c==40)incdata3(0,1,aux1,1,0);
        else if(c==80||c==20)incdata3(0,1,aux1,2,0);
        else if(c==160||c==15)incdata3(0,1,aux1,3,0);
        else if(c==10)incdata3(0,1,aux1,4,0);
        sprintf(aux2,"%03d:%s:%s",c,row[4],qsoz_mode(row[5]));
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 34: { // EURASIA
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        lat1=((row[3][1]-'A')*10.0+(row[3][3]-'0')+1.0/48.0-90.0);
        lon1=-((row[3][0]-'A')*20.0+(row[3][2]-'0')*2.0+1.0/24.0-180.0);
        lat2=((row[4][1]-'A')*10.0+(row[4][3]-'0')+1.0/48.0-90.0);
        lon2=-((row[4][0]-'A')*20.0+(row[4][2]-'0')*2.0+1.0/24.0-180.0);
        score=(int)radio_distance_km(lat1,lon1,lat2,lon2);
        if(c==160)score*=(int)(((int)score/500)/10.0+1);
        else if(c==80)score*=(int)(((int)score/1000)/10.0+1);
        else if(c==10){if(score>=100&&score<=800)score*=10;}
        else if(c==15){if(score>=100&&score<=800)score*=5;}
        sprintf(aux5,"%.4s",row[4]);
        if(numdata3(0,5,aux5)==0)score+=1000;
        incdata3(0,5,aux5,1,0);
        incdata3(0,1,aux1,score,0);
        sprintf(aux2,"%03d:%.2s:%s",c,row[4],qsoz_mode(row[5]));
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 35: { // WAG DL=230
        if(home_dxcc!=230&&vv!=230)break;
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(home_dxcc!=230)incdata3(0,1,aux1,3,0);
        else {
          if(vv==230)incdata3(0,1,aux1,1,0);
          else if(strncmp(cont[vv],"EU",2)==0)incdata3(0,1,aux1,3,0);
          else incdata3(0,1,aux1,5,0);
        }
        if(home_dxcc==230){
          sprintf(aux2,"%03d:%d:%s",c,vv,qsoz_mode(row[5]));
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        else {
          sprintf(aux2,"%03d:%.2s:%s",c,row[4],qsoz_mode(row[5]));
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 36: { // CQWPXRTTY
        sprintf(aux1,"%03d:%s",c,row[0]);
        incdata3(0,0,aux1,1,0);
        if(strncmp(cont[vv],cont[home_dxcc],2)!=0){if(c<=20)incdata3(0,1,aux1,3,0); else incdata3(0,1,aux1,6,0);}
        else if(home_dxcc!=vv){if(c<=20)incdata3(0,1,aux1,2,0); else incdata3(0,1,aux1,4,0);}
        else {if(c<=20)incdata3(0,1,aux1,1,0); else incdata3(0,1,aux1,2,0);}
        sprintf(aux2,"%03d:%s",c,qsoz_wpx(row[0]));
        sprintf(aux3,"ALL:%s",qsoz_wpx(row[0]));
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux3,1,0);
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 37: // SACSSB
      case 38: { // SACCW  
        static const int lll[] = {5, 118, 167, 221, 222, 224, 237, 242, 259, 266, 284};
        sprintf(aux1,"%03d:%s",c,row[0]);
        incdata3(0,0,aux1,1,0);
        n=sizeof(lll)/sizeof(lll[0]);
        for(d=0;d<n;d++)if(home_dxcc==lll[d])break;
        if(d<n){
          for(e=0;e<n;e++)if(vv==lll[e])break;
          if(e<n)continue;
          if(strncmp(cont[vv],"EU",2)==0)incdata3(0,1,aux1,2,0);
          else incdata3(0,1,aux1,3,0);
          sprintf(aux2,"%03d:%d",c,vv);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        else {
          for(d=0;d<n;d++)if(vv==lll[d])break;
          if(d<n){
            if(strncmp(cont[vv],"EU",2)==0)incdata3(0,1,aux1,1,0);
            else if(c<=20)incdata3(0,1,aux1,1,0); else incdata3(0,1,aux1,3,0);
            for(p=row[0]+1;*p!='\0';p++)if(isdigit(*p))break;
            sprintf(aux2,"%03d:%d:%c",c,vv,*p);
            incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
          }
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 39: { // PACC PA=263 (no collasos stesse aree)
        static const int lll[] = {15, 112, 339, 100, 108, 1, 291, 150, 462, 170};
        if(home_dxcc!=263&&vv!=263)break;
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        incdata3(0,1,aux1,1,0);
        if(home_dxcc!=263){
          sprintf(aux2,"%03d:%s:%s",c,row[4],qsoz_mode(row[5]));
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        else {
          n=sizeof(lll)/sizeof(lll[0]);
          for(d=0;d<n;d++)if(vv==lll[d])break;
          if(d<n){
            area=qsoz_pacc_area(row[0],vv);
            if(area[0]=='\0')break;
            sprintf(aux2,"%03d:%s:%s",c,area,qsoz_mode(row[5]));
            incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
          }
          else {
            sprintf(aux2,"%03d:%d:%s",c,vv,qsoz_mode(row[5]));
            incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
          }
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 40: // AASSB
      case 41: { // AACW
        static const int lll[] = {247, 293, 406, 286, 325, 336, 215, 271, 279, 376, 299, 327, 381, 370, 369, 387, 376, 372, 372, 407, 386, 406, 318, 510, 221, 330, 292, 334, 341, 318, 387, 387, 339, 226, 363, 340, 378, 344, 305, 390, 54, 315, 130, 321, 324, 325, 326, 312, 321, 318, 309, 3, 333, 380, 105};
        if(home_dxcc==vv)break;
        sprintf(aux1,"%03d:%s",c,row[0]);
        incdata3(0,0,aux1,1,0);
        n=sizeof(lll)/sizeof(lll[0]);
        for(d=0;d<n;d++)if(home_dxcc==lll[d])break;
        for(e=0;e<n;e++)if(vv==lll[e])break;
        if(d<n){
          if(e<n){
            if(c==160)incdata3(0,1,aux1,3,0);
            else if(c==80||c==10)incdata3(0,1,aux1,2,0);
            else incdata3(0,1,aux1,1,0);
          }
          else {
            if(c==160)incdata3(0,1,aux1,9,0);
            else if(c==80||c==10)incdata3(0,1,aux1,6,0);
            else incdata3(0,1,aux1,3,0);
          }
          sprintf(aux2,"%03d:%d",c,vv);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        else {
          if(e<n){
            if(c==160)incdata3(0,1,aux1,3,0);
            else if(c==80||c==10)incdata3(0,1,aux1,2,0);
            else incdata3(0,1,aux1,1,0);
            sprintf(aux2,"%03d:%s",c,qsoz_wpx(row[0]));
            incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
          }
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 42: { // HOLYLANDDX 4x=336
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(home_dxcc==336){
          if(vv==336)incdata3(0,1,aux1,1,0);
          else if(strncmp(cont[vv],cont[home_dxcc],2)==0)incdata3(0,1,aux1,2,0);
          else incdata3(0,1,aux1,8,0);
          if(vv==336){
            sprintf(aux2,"%03d:%s",c,row[4]);
            incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
          }
          else {
            sprintf(aux2,"%03d:%d:%s",c,vv,qsoz_mode(row[5]));
            incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
          }
          
        }
        else {
          if(vv==336)incdata3(0,1,aux1,8,0);
          else if(strncmp(cont[vv],cont[home_dxcc],2)!=0)incdata3(0,1,aux1,4,0);
          else if(home_dxcc==vv)incdata3(0,1,aux1,1,0);
          else incdata3(0,1,aux1,2,0);
          if(vv==336){
            sprintf(aux2,"%03d:%s",c,row[4]);
            incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
          }
          else {
            sprintf(aux2,"%03d:%d",c,vv);
            incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
          }
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 43: { // EUDX
        static const int lll[] =  {206, 209, 212, 497, 215, 221, 227, 244, 227, 230, 236, 245, 248, 249, 277, 254, 257, 263, 269, 272, 503, 275, 504, 499, 281, 284, 510, 511, 512, 513, 516, 517, 516, 510, 509, 517, 519, 520, 521, 522, 523, 524};
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        n=sizeof(lll)/sizeof(lll[0]);
        for(d=0;d<n;d++)if(home_dxcc==lll[d])break;
        for(e=0;e<n;e++)if(vv==lll[e])break;
        if(d<n){
          if(home_dxcc==vv)incdata3(0,1,aux1,2,0);
          else if(e<n)incdata3(0,1,aux1,10,0);
          else if(strncmp(cont[vv],cont[home_dxcc],2)==0)incdata3(0,1,aux1,3,0);
          else incdata3(0,1,aux1,5,0);
        }
        else {
          if(e<n)incdata3(0,1,aux1,10,0);
          else if(home_dxcc==vv)incdata3(0,1,aux1,2,0);
          else if(strncmp(cont[vv],cont[home_dxcc],2)==0)incdata3(0,1,aux1,3,0);
          else incdata3(0,1,aux1,5,0);
        }
        sprintf(aux2,"%03d:%d",c,vv);
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        if(e<n){
          sprintf(aux2,"%03d:%s",c,row[4]);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 44: { // UNDX UN=130
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(home_dxcc==130){
          if(vv==130)incdata3(0,1,aux1,10,0);
          else if(strncmp(cont[vv],cont[home_dxcc],2)==0)incdata3(0,1,aux1,3,0);
          else incdata3(0,1,aux1,5,0);
        }
        else {
          if(vv==130)incdata3(0,1,aux1,10,0);
          else if(vv==home_dxcc)incdata3(0,1,aux1,2,0);
          else if(strncmp(cont[vv],cont[home_dxcc],2)==0)incdata3(0,1,aux1,3,0);
          else incdata3(0,1,aux1,5,0);
        }
        sprintf(aux2,"%03d:%d",c,vv);
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        if(vv==130){
          sprintf(aux2,"%03d:%s",c,row[4]);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 45: { // URDXC UR=288
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(home_dxcc==288){
          if(vv==288)incdata3(0,1,aux1,1,0);
          else if(strncmp(cont[vv],cont[home_dxcc],2)==0)incdata3(0,1,aux1,2,0);
          else incdata3(0,1,aux1,3,0);
        }
        else {
          if(vv==288)incdata3(0,1,aux1,10,0);
          else if(vv==home_dxcc)incdata3(0,1,aux1,1,0);
          else if(strncmp(cont[vv],cont[home_dxcc],2)==0)incdata3(0,1,aux1,2,0);
          else incdata3(0,1,aux1,3,0);
        }
        sprintf(aux2,"%03d:%d",c,vv);
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        if(home_dxcc!=288 && vv==288){
          sprintf(aux2,"%03d:%s",c,row[4]);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 46: { // CQBB I=248
        if(home_dxcc!=248||vv!=248)break;
        if(c!=40&&c!=80&&c!=160)break;
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(strncmp(row[0],"IQ",2)==0 || strncmp(row[0],"IY",2)==0)incdata3(0,1,aux1,10,0);
        else if(strncmp(qsoz_mode(row[5]),"CW",2)==0)incdata3(0,1,aux1,2,0);
        else incdata3(0,1,aux1,1,0);
        sprintf(aux2,"%03d:%.2s:%s",c,row[4],qsoz_mode(row[5]));
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        if(strlen(row[4])>2){
          sprintf(aux2,"%03d:%d:%s",c,atoi(row[4]+2),qsoz_mode(row[5]));
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 47: { // BSC Black Sea Cup
        static const int lll[] = {206, 251, 27, 212, 501, 239, 230, 286, 248, 502, 179, 269, 54, 275, 504, 499, 390, 288, 497, 514, 503, 287, 296};
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        n=sizeof(lll)/sizeof(lll[0]);
        for(d=0;d<n;d++)if(vv==lll[d])break;
        if(d<n||strncmp(row[4],"BS",2)==0)incdata3(0,1,aux1,10,0);
        else if(ituz[home_dxcc]==ituz[vv])incdata3(0,1,aux1,1,0);
        else if(ituz[home_dxcc]!=ituz[vv]&&strncmp(cont[vv],cont[home_dxcc],2)==0)incdata3(0,1,aux1,3,0);
        else incdata3(0,1,aux1,5,0);
        if(d<n){
          sprintf(aux2,"%03d:@@%d",c,vv);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux2,"%03d:%s",c,row[4]);
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 48: { // RRTC
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(!isdigit(row[4][0]))incdata3(0,1,aux1,1,0);
        else if(ituz[home_dxcc]==ituz[vv])incdata3(0,1,aux1,2,0);
        else incdata3(0,1,aux1,3,0);
        if(!isdigit(row[4][0])){
          sprintf(aux2,"%03d:%.3s",c,row[4]);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        else {
          sprintf(aux2,"%03d:%d",c,ituz[vv]);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 49: { // UCC
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        incdata3(0,1,aux1,1,0);
        if(!isdigit(row[4][0])){
          sprintf(aux2,"%03d:%s",c,row[4]);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 50: { // PADANG YB=327
        sprintf(aux1,"%03d:%s",c,row[0]);
        incdata3(0,0,aux1,1,0);
        if(strcmp(row[0],"7B5C")==0)incdata3(0,1,aux1,20,0);
        else if(home_dxcc==vv)incdata3(0,1,aux1,2,0);
        else if(strncmp(cont[vv],cont[home_dxcc],2)==0)incdata3(0,1,aux1,4,0);
        else incdata3(0,1,aux1,6,0);
        sprintf(aux2,"%03d:%d",c,vv);
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        if(vv==327){
          sprintf(aux2,"%03d:%s",c,qsoz_wpx(row[0]));
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 51: { // ARRL10
        if(c!=10)break;
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(strncmp(qsoz_mode(row[5]),"PH",2)==0)incdata3(0,1,aux1,2,0);
        else incdata3(0,1,aux1,4,0);
        sprintf(aux2,"%03d:%d:%s",c,vv,qsoz_mode(row[5]));
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        sprintf(aux2,"%03d:Z%d:%s",c,ituz[vv],qsoz_mode(row[5]));
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        if(!isdigit(row[4][0])){
          sprintf(aux2,"%03d:%s:%s",c,row[4],qsoz_mode(row[5]));
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 52: // ARRLRU
      case 53: // ARRLRTTY
      case 54: { // FTROUNDUP
        sprintf(aux1,"%03d:%s",c,row[0]);
        incdata3(0,0,aux1,1,0);
        incdata3(0,1,aux1,1,0);
        if(isdigit(row[4][0])){
          sprintf(aux2,"%03d:%d",c,vv); incdata3(0,2,aux2,1,0); 
          sprintf(aux3,"ALL:%d",vv); incdata3(0,3,aux3,1,0);
        }
        else {
          sprintf(aux2,"%03d:%s",c,row[4]); incdata3(0,2,aux2,1,0);
          sprintf(aux3,"ALL:%s",row[4]); incdata3(0,3,aux3,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 55: { // RCC
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(strncmp(row[4],"RCC",3)==0){
          incdata3(0,1,aux1,10,0);
          sprintf(aux2,"%03d:%s:%s",c,row[4],qsoz_mode(row[5]));
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        else {
          if(strncmp(cont[vv],cont[home_dxcc],2)==0)incdata3(0,1,aux1,3,0);
          else incdata3(0,1,aux1,5,0);
          sprintf(aux2,"%03d:%d:%s",c,ituz[vv],qsoz_mode(row[5]));
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 56: { // ARKTIKA
        sprintf(aux1,"%03d:%s",c,row[0]);
        incdata3(0,0,aux1,1,0);
        if(isdigit(row[4][0]))incdata3(0,1,aux1,1,0);
        else {
          incdata3(0,1,aux1,3,0);
          sprintf(aux2,"%03d:%s",c,row[4]);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 57: { // 9ADX 9A=497
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(home_dxcc==497){
          if(vv==497)incdata3(0,1,aux1,1,0);
          else if(strncmp(cont[vv],"EU",2)==0){
            if(c>=80)incdata3(0,1,aux1,4,0);
            else incdata3(0,1,aux1,2,0);
          }
          else {
            if(c>=80)incdata3(0,1,aux1,10,0);
            else if(c==40)incdata3(0,1,aux1,8,0);
            else incdata3(0,1,aux1,6,0);
          }
        }
        else {
          epoch=atoll(row[6]); t=gmtime(&epoch); 
          e=(((t->tm_hour*60+t->tm_min)>=23*60) || ((t->tm_hour*60+t->tm_min)<=4*60+59)) ?2:0;
          if(vv==497){
            if(c>=40)incdata3(0,1,aux1,10+e,0);
            else incdata3(0,1,aux1,6+e,0);
          }
          else if(strncmp(cont[home_dxcc],cont[vv],2)!=0){
            if(c>=40)incdata3(0,1,aux1,6+e,0);
            else incdata3(0,1,aux1,3+e,0);
          }
          else {
            if(c>=40)incdata3(0,1,aux1,2+e,0);
            else incdata3(0,1,aux1,1+e,0);
          }
        }
        sprintf(aux2,"%03d:%d:%s",c,ituz[vv],qsoz_mode(row[5]));
        incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        if(home_dxcc==497){
          sprintf(aux2,"%03d:@%d:%s",c,vv,qsoz_mode(row[5]));
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        else if(vv==497){
          sprintf(aux2,"%03d:s:%s",c,row[4]);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }      
      case 58: // EIUKDXSSB
      case 59: { // EIUKDXCW
        static const int lll[] =  {245, 223, 114, 265, 122, 279, 106, 294};
        sprintf(aux1,"%03d:%s",c,row[0]);
        incdata3(0,0,aux1,1,0);
        n=sizeof(lll)/sizeof(lll[0]);
        for(d=0;d<n;d++)if(home_dxcc==lll[d])break;
        for(e=0;e<n;e++)if(vv==lll[e])break;
        if(d<n){
          epoch=atoll(row[6]); t=gmtime(&epoch); 
          z=(((t->tm_hour*60+t->tm_min)>=1*60) && ((t->tm_hour*60+t->tm_min)<=4*60+59)) ?2:1;
          if(e<n || strncmp(cont[vv],"EU",2)==0){
            if(c>=40)incdata3(0,1,aux1,4*z,0);
            else incdata3(0,1,aux1,2*z,0);
          }
          else {
            if(c>=40)incdata3(0,1,aux1,8*z,0);
            else incdata3(0,1,aux1,4*z,0);
          }
        }
        else if(strncmp(cont[home_dxcc],"EU",2)==0){
          if(e<n || strncmp(cont[vv],"EU",2)!=0){
            if(c>=40)incdata3(0,1,aux1,4,0);
            else incdata3(0,1,aux1,2,0);
          }
          else {
            if(c>=40)incdata3(0,1,aux1,2,0);
            else incdata3(0,1,aux1,1,0);
          }
        }
        else {
          if(e<n){
            if(c>=40)incdata3(0,1,aux1,8,0);
            else incdata3(0,1,aux1,4,0);
          }
          else if(strncmp(cont[vv],"EU",2)==0){
            if(c>=40)incdata3(0,1,aux1,4,0);
            else incdata3(0,1,aux1,2,0);
          }
          else {
            if(c>=40)incdata3(0,1,aux1,2,0);
            else incdata3(0,1,aux1,1,0);
          }
        }
        if(e<n){
          for(p=row[4];*p!='\0';p++)if(!isdigit(*p))break;
          if(*p!='\0'){
            sprintf(aux2,"%03d:%s",c,p);
            incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
          }
        }
        else {
          sprintf(aux2,"%03d:%d",c,vv);
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 60: { // RAC VA=1
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(vv==1){
          if(strcmp(row[0],"VE3RHQ")==0 || (strlen(row[0])>=6 && strncmp(row[0]+3,"RAC",3)==0))incdata3(0,1,aux1,20,0);
          else incdata3(0,1,aux1,10,0);
        }
        else incdata3(0,1,aux1,2,0);
        if(!isdigit(row[4][0])){
          sprintf(aux2,"%03d:%s:%s",c,row[4],qsoz_mode(row[5]));
          incdata3(0,2,aux2,1,0); incdata3(0,3,aux2,1,0);
        }
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
      case 61: { // ARRLFIELDDAY no power declaration so score=0 only qso points
        sprintf(aux1,"%03d:%s:%s",c,row[0],qsoz_mode(row[5]));
        incdata3(0,0,aux1,1,0);
        if(strncmp(qsoz_mode(row[5]),"PH",2)==0)incdata3(0,1,aux1,1,0);
        else incdata3(0,1,aux1,2,0);
        sprintf(aux4,"%03d",c);
        incdata3(0,4,aux4,1,0);
        break;
      }
    }
  }
  if((contype==29 || contype==30) && home_dxcc!=209 && uba_total>0 && uba_belgian>0){
    uba_bonus=(uba_belgian_points*uba_belgian)/uba_total;
    if(uba_bonus>0)incdata3(0,1,"UBA:BONUS",uba_bonus,0);
  }
  mysql_free_result(res);
}
