#include "output_codec.h"
#include "socket_output.h"
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
static unsigned calls=0;
static bool allowed(void *p){return p==nullptr||++calls==1;}
int main(int argc,char **argv){
 if (argc != 3)
   return 2;
 bool cancel=!strcmp(argv[1],"cancel"),refused=!strcmp(argv[1],"refused");bool tcp=cancel||refused||!strcmp(argv[1],"tcp");uint8_t data[160];
 size_t n=!strcmp(argv[1],"osc")?output_codec::osc("/next",100,data,sizeof(data)):output_codec::raw("next",tcp,data,sizeof(data));
 auto result=socket_output::send(tcp,inet_addr("127.0.0.1"),atoi(argv[2]),data,n,allowed,cancel?&calls:nullptr);
 return result==(cancel||refused?socket_output::Result::Rejected:socket_output::Result::Sent)?0:4;
}
