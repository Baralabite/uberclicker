#include "output_codec.h"
#include <string.h>
namespace output_codec {
size_t osc(const char *address,int32_t value,uint8_t *out,size_t capacity) {
  size_t n=strlen(address);if(!n||n>64||address[0]!='/')return 0;
  for(size_t i=1;i<n;++i)if(!((address[i]>='a'&&address[i]<='z')||(address[i]>='A'&&address[i]<='Z')||(address[i]>='0'&&address[i]<='9')||address[i]=='/'||address[i]=='_'||address[i]=='-'))return 0;
  size_t padded=(n+4)&~size_t(3), length=padded+8;if(capacity<length)return 0;
  memset(out,0,length);memcpy(out,address,n);out[padded]=',';out[padded+1]='i';
  uint32_t v=uint32_t(value);for(unsigned i=0;i<4;++i)out[padded+4+i]=v>>(24-i*8);return length;
}
size_t raw(const char *payload,bool tcp,uint8_t *out,size_t capacity) {
  size_t n=strlen(payload);if(!n||n>128||capacity<n+size_t(tcp))return 0;
  if(tcp&&(strchr(payload,'\r')||strchr(payload,'\n')))return 0;
  memcpy(out,payload,n);if(tcp)out[n++]='\n';return n;
}
size_t midi(uint8_t type,uint8_t channel,uint8_t number,uint8_t value,bool release,uint8_t *out) {
  if(type>2||!channel||channel>16||number>127||value>127)return 0;
  if(release&&type!=0)return 0;
  out[0]=(type==0?(release?0x80:0x90):type==1?0xb0:0xc0)|(channel-1);out[1]=number;
  if(type==2)return 2;out[2]=release?0:value;return 3;
}
}
