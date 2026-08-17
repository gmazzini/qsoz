// da cancellare
// prx.c UDP port 2333 ADIF entry receiver by GM @2025 V 2.0
#include "pfunc.c"
#define PORT 2333
#define LOG "/home/www/log/prx.log"

char adif[20][200],adif1[20][20];
int adifextract(char *,int);

int main(void) {
  int sockfd,vv,gg,opt;
  struct sockaddr_in server_addr,client_addr;
  socklen_t addr_len=sizeof(client_addr);
  ssize_t len;
  char buffer[1000],buf[1000],aux1[300];
  MYSQL *con;
  FILE *fp;

  strcpy(adif1[0],"call"); strcpy(adif1[1],"freq"); strcpy(adif1[2],"freq_rx"); strcpy(adif1[3],"rst_sent"); strcpy(adif1[4],"rst_rcvd"); strcpy(adif1[5],"mode");
  strcpy(adif1[6],"time_on"); strcpy(adif1[7],"time_off"); strcpy(adif1[8],"stx_string"); strcpy(adif1[9],"stx"); strcpy(adif1[10],"srx_string"); strcpy(adif1[11],"srx");
  strcpy(adif1[12],"contest_id"); strcpy(adif1[13],"qso_date"); strcpy(adif1[14],"qso_date_off"); strcpy(adif1[15],"comment"); strcpy(adif1[16],"station_callsign");
  sockfd=socket(AF_INET,SOCK_DGRAM,0);
  if(sockfd<0)exit(-1);
  opt=1;
  if(setsockopt(sockfd,SOL_SOCKET,SO_REUSEPORT,&opt,sizeof(opt))<0)exit(-1);
  memset(&server_addr,0,sizeof(server_addr));
  server_addr.sin_family=AF_INET;
  server_addr.sin_addr.s_addr=INADDR_ANY;
  server_addr.sin_port=htons(PORT);
  if(bind(sockfd,(struct sockaddr *)&server_addr,sizeof(server_addr))<0)exit(-1);
  fp=fopen(LOG,"a");
  if(fp!=NULL){fprintf(fp,"Start %ld\n",time(NULL)); fclose(fp);}
  for(;;){
    len=recvfrom(sockfd,buffer,sizeof(buffer)-1,0,(struct sockaddr *)&client_addr,&addr_len);
    if(len<0)continue;
    buffer[len]='\0';
    vv=17; gg=adifextract(buffer,vv);
    if(strcmp(adif[15],secret_rx)!=0)continue;
    if(adif[6][4]=='\0'){adif[6][4]='0'; adif[6][5]='0'; adif[6][6]='\0';}
    if(adif[14][0]=='\0')strcpy(adif[14],adif[13]);
    if(adif[7][0]=='\0')strcpy(adif[7],adif[6]);
    if(adif[7][4]=='\0'){adif[7][4]='0'; adif[7][5]='0'; adif[7][6]='\0';}
    con=mysql_init(NULL);
    if(con==NULL)continue;
    if(mysql_real_connect(con,dbhost,dbuser,dbpassword,dbname,0,NULL,0)==NULL)continue;
    searchcty(con,adif[0]);
    sprintf(aux1,"('%s','%s','%s',%ld,%ld,'%s','%s','%s','%s','%s',%d,%lld,%lld)",adif[16],adif[0],adif[5],(long)(atof(adif[1])*1000000.0),(long)(atof(adif[2])*1000000.0),adif[3],adif[4],(adif[8][0]=='\0')?adif[9]:adif[8],(adif[10][0]=='\0')?adif[11]:adif[10],adif[12],atoi(mycty[2]),dt2e(adif[13],adif[6]),dt2e(adif[14],adif[7]));
    fp=fopen(LOG,"a");
    if(fp!=NULL){fprintf(fp,"%s\n",aux1); fclose(fp);}
    sprintf(buf,"insert ignore into log (mycall,callsign,mode,freqtx,freqrx,signaltx,signalrx,contesttx,contestrx,contest,dxcc,open,close) value %s",aux1);
    mysql_query(con,buf);
    mysql_close(con);
  }
}
