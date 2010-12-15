/**
 * mod_aaencode -- apache filter module encoding to japanese style emoticons.
 *
 * AUTHOR:
 *   Yasuhiro Matsumoto <mattn.jp@gmail.com> a.k.a mattn
 *
 * ORIGINAL IDEA:
 *   Yosuke HASEGAWA, http://utf-8.jp/
 *
 * COMPILING:
 *
 *  for Windows:
 *    apxs2 -i -a -c -Wc,-O2 mod_aaencode.c \
 *      libapr-1.lib libaprutil-1.lib libhttpd.lib
 *
 *  for UNIX:
 *    apxs2 -i -a -c -Wc,-O2 mod_aaencode.c -lapr-1 -laprutil-1
 *
 * INSTALLATION:
 *   add following line into your '.htaccess'.
 *
 *   AddOutputFilter AAENCODE .js
 */
#include "httpd.h"
#include "http_config.h"
#include "http_protocol.h"
#include "http_log.h"
#include "ap_config.h"

#define PUT(f, bb, s) \
  APR_BRIGADE_INSERT_TAIL(bb, \
		  apr_bucket_immortal_create(s, strlen(s), f->c->bucket_alloc))

#define CTOI(c) ( \
  (c >= '0' && c <= '9') ? (c - '0') : \
  (c >= 'a' && c <= 'f') ? (c - 'a' + 10) : \
  (c >= 'A' && c <= 'F') ? (c - 'A' + 10) : -1)

static char
utf8len_tab[256] = {
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,6,6,1,1,
};

static char
utf8len_tab_zero[256] = {
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,6,6,0,0,
};

static int
utf_ptr2len(unsigned char* p) {
  int    len;
  int    i;

  if (*p == 0)
    return 0;
  len = utf8len_tab[*p];
  for (i = 1; i < len; ++i)
    if ((p[i] & 0xc0) != 0x80)
      return 1;
  return len;
}

static int
utf_ptr2char(unsigned char* p) {
  int    len;

  if (p[0] < 0x80)  /* be quick for ASCII */
    return p[0];

  len = utf8len_tab_zero[p[0]];
  if (len > 1 && (p[1] & 0xc0) == 0x80) {
    if (len == 2)
      return ((p[0] & 0x1f) << 6) + (p[1] & 0x3f);
    if ((p[2] & 0xc0) == 0x80) {
      if (len == 3)
        return ((p[0] & 0x0f) << 12) + ((p[1] & 0x3f) << 6)
          + (p[2] & 0x3f);
      if ((p[3] & 0xc0) == 0x80) {
        if (len == 4)
          return ((p[0] & 0x07) << 18) + ((p[1] & 0x3f) << 12)
            + ((p[2] & 0x3f) << 6) + (p[3] & 0x3f);
        if ((p[4] & 0xc0) == 0x80) {
          if (len == 5)
            return ((p[0] & 0x03) << 24) + ((p[1] & 0x3f) << 18)
              + ((p[2] & 0x3f) << 12) + ((p[3] & 0x3f) << 6)
              + (p[4] & 0x3f);
          if ((p[5] & 0xc0) == 0x80 && len == 6)
            return ((p[0] & 0x01) << 30) + ((p[1] & 0x3f) << 24)
              + ((p[2] & 0x3f) << 18) + ((p[3] & 0x3f) << 12)
              + ((p[4] & 0x3f) << 6) + (p[5] & 0x3f);
        }
      }
    }
  }
  /* Illegal value, just return the first byte */
  return p[0];
}

static apr_status_t
aaencode_output_filter(ap_filter_t* f, apr_bucket_brigade* bb) {
  const char* aa[] = {
    "(c^_^o)", // (c^_^o)
    "(\xef\xbe\x9f\xce\x98\xef\xbe\x9f)", // (ﾟΘﾟ)
    "((o^_^o) - (\xef\xbe\x9f\xce\x98\xef\xbe\x9f))", // ((o^_^o) - (ﾟΘﾟ))
    "(o^_^o)", // (o^_^o)
    "(\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f)", // (ﾟｰﾟ)
    "((\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f) + (\xef\xbe\x9f\xce\x98\xef\xbe\x9f))", // ((ﾟｰﾟ) + (ﾟΘﾟ))
    "((o^_^o) +(o^_^o))", // ((o^_^o) +(o^_^o))
    "((\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f) + (o^_^o))", // ((ﾟｰﾟ) + (o^_^o))
    "((\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f) + (\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f))", // ((ﾟｰﾟ) + (ﾟｰﾟ))
    "((\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f) + (\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f) + (\xef\xbe\x9f\xce\x98\xef\xbe\x9f))", // ((ﾟｰﾟ) + (ﾟｰﾟ) + (ﾟΘﾟ))
    "(\xef\xbe\x9f\xd0\x94\xef\xbe\x9f) .\xef\xbe\x9f\xcf\x89\xef\xbe\x9f\xef\xbe\x89", // (ﾟДﾟ) .ﾟωﾟﾉ
    "(\xef\xbe\x9f\xd0\x94\xef\xbe\x9f) .\xef\xbe\x9f\xce\x98\xef\xbe\x9f\xef\xbe\x89", // (ﾟДﾟ) .ﾟΘﾟﾉ
    "(\xef\xbe\x9f\xd0\x94\xef\xbe\x9f) ['c']", // (ﾟДﾟ) ['c']
    "(\xef\xbe\x9f\xd0\x94\xef\xbe\x9f) .\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f\xef\xbe\x89", // (ﾟДﾟ) .ﾟｰﾟﾉ
    "(\xef\xbe\x9f\xd0\x94\xef\xbe\x9f) .\xef\xbe\x9f\xd0\x94\xef\xbe\x9f\xef\xbe\x89", // (ﾟДﾟ) .ﾟДﾟﾉ
    "(\xef\xbe\x9f\xd0\x94\xef\xbe\x9f) [\xef\xbe\x9f\xce\x98\xef\xbe\x9f]", // (ﾟДﾟ) [ﾟΘﾟ]
  };
  const char* head =
    "\xef\xbe\x9f\xcf\x89\xef\xbe\x9f\xef\xbe\x89= /\xef\xbd\x80\xef\xbd\x8d\xc2\xb4\xef\xbc\x89\xef\xbe\x89 ~\xe2\x94\xbb\xe2\x94\x81\xe2\x94\xbb   //*\xc2\xb4\xe2\x88\x87\xef\xbd\x80*/ ['_']; o=(\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f)  =_=3; " // ﾟωﾟﾉ= /｀ｍ´）ﾉ ~┻━┻   //*´∇｀*/ ['_']; o=(ﾟｰﾟ)  =_=3; 
    "c=(\xef\xbe\x9f\xce\x98\xef\xbe\x9f) =(\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f)-(\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f); (\xef\xbe\x9f\xd0\x94\xef\xbe\x9f) =(\xef\xbe\x9f\xce\x98\xef\xbe\x9f)= (o^_^o)/ (o^_^o);" // c=(ﾟΘﾟ) =(ﾟｰﾟ)-(ﾟｰﾟ); (ﾟДﾟ) =(ﾟΘﾟ)= (o^_^o)/ (o^_^o);
    "(\xef\xbe\x9f\xd0\x94\xef\xbe\x9f)={\xef\xbe\x9f\xce\x98\xef\xbe\x9f: '_' ,\xef\xbe\x9f\xcf\x89\xef\xbe\x9f\xef\xbe\x89 : ((\xef\xbe\x9f\xcf\x89\xef\xbe\x9f\xef\xbe\x89==3) +'_') [\xef\xbe\x9f\xce\x98\xef\xbe\x9f] " // (ﾟДﾟ)={ﾟΘﾟ: '_' ,ﾟωﾟﾉ : ((ﾟωﾟﾉ==3) +'_') [ﾟΘﾟ] 
    ",\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f\xef\xbe\x89 :(\xef\xbe\x9f\xcf\x89\xef\xbe\x9f\xef\xbe\x89+ '_')[o^_^o -(\xef\xbe\x9f\xce\x98\xef\xbe\x9f)] " // ,ﾟｰﾟﾉ :(ﾟωﾟﾉ+ '_')[o^_^o -(ﾟΘﾟ)] 
    ",\xef\xbe\x9f\xd0\x94\xef\xbe\x9f\xef\xbe\x89:((\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f==3) +'_')[\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f] }; (\xef\xbe\x9f\xd0\x94\xef\xbe\x9f) [\xef\xbe\x9f\xce\x98\xef\xbe\x9f] =((\xef\xbe\x9f\xcf\x89\xef\xbe\x9f\xef\xbe\x89==3) +'_') [c^_^o];" // ,ﾟДﾟﾉ:((ﾟｰﾟ==3) +'_')[ﾟｰﾟ] }; (ﾟДﾟ) [ﾟΘﾟ] =((ﾟωﾟﾉ==3) +'_') [c^_^o];
    "(\xef\xbe\x9f\xd0\x94\xef\xbe\x9f) ['c'] = ((\xef\xbe\x9f\xd0\x94\xef\xbe\x9f)+'_') [ (\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f)+(\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f)-(\xef\xbe\x9f\xce\x98\xef\xbe\x9f) ];" // (ﾟДﾟ) ['c'] = ((ﾟДﾟ)+'_') [ (ﾟｰﾟ)+(ﾟｰﾟ)-(ﾟΘﾟ) ];
    "(\xef\xbe\x9f\xd0\x94\xef\xbe\x9f) ['o'] = ((\xef\xbe\x9f\xd0\x94\xef\xbe\x9f)+'_') [\xef\xbe\x9f\xce\x98\xef\xbe\x9f];" // (ﾟДﾟ) ['o'] = ((ﾟДﾟ)+'_') [ﾟΘﾟ];
    "(\xef\xbe\x9fo\xef\xbe\x9f)=(\xef\xbe\x9f\xd0\x94\xef\xbe\x9f) ['c']+(\xef\xbe\x9f\xd0\x94\xef\xbe\x9f) ['o']+(\xef\xbe\x9f\xcf\x89\xef\xbe\x9f\xef\xbe\x89 +'_')[\xef\xbe\x9f\xce\x98\xef\xbe\x9f]+ ((\xef\xbe\x9f\xcf\x89\xef\xbe\x9f\xef\xbe\x89==3) +'_') [\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f] + " // (ﾟoﾟ)=(ﾟДﾟ) ['c']+(ﾟДﾟ) ['o']+(ﾟωﾟﾉ +'_')[ﾟΘﾟ]+ ((ﾟωﾟﾉ==3) +'_') [ﾟｰﾟ] + 
    "((\xef\xbe\x9f\xd0\x94\xef\xbe\x9f) +'_') [(\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f)+(\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f)]+ ((\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f==3) +'_') [\xef\xbe\x9f\xce\x98\xef\xbe\x9f]+" // ((ﾟДﾟ) +'_') [(ﾟｰﾟ)+(ﾟｰﾟ)]+ ((ﾟｰﾟ==3) +'_') [ﾟΘﾟ]+
    "((\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f==3) +'_') [(\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f) - (\xef\xbe\x9f\xce\x98\xef\xbe\x9f)]+(\xef\xbe\x9f\xd0\x94\xef\xbe\x9f) ['c']+" // ((ﾟｰﾟ==3) +'_') [(ﾟｰﾟ) - (ﾟΘﾟ)]+(ﾟДﾟ) ['c']+
    "((\xef\xbe\x9f\xd0\x94\xef\xbe\x9f)+'_') [(\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f)+(\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f)]+ (\xef\xbe\x9f\xd0\x94\xef\xbe\x9f) ['o']+" // ((ﾟДﾟ)+'_') [(ﾟｰﾟ)+(ﾟｰﾟ)]+ (ﾟДﾟ) ['o']+
    "((\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f==3) +'_') [\xef\xbe\x9f\xce\x98\xef\xbe\x9f];(\xef\xbe\x9f\xd0\x94\xef\xbe\x9f) ['_'] =(o^_^o) [\xef\xbe\x9fo\xef\xbe\x9f] [\xef\xbe\x9fo\xef\xbe\x9f];" // ((ﾟｰﾟ==3) +'_') [ﾟΘﾟ];(ﾟДﾟ) ['_'] =(o^_^o) [ﾟoﾟ] [ﾟoﾟ];
    "(\xef\xbe\x9f\xce\xb5\xef\xbe\x9f)=((\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f==3) +'_') [\xef\xbe\x9f\xce\x98\xef\xbe\x9f]+ (\xef\xbe\x9f\xd0\x94\xef\xbe\x9f) .\xef\xbe\x9f\xd0\x94\xef\xbe\x9f\xef\xbe\x89+" // (ﾟεﾟ)=((ﾟｰﾟ==3) +'_') [ﾟΘﾟ]+ (ﾟДﾟ) .ﾟДﾟﾉ+
    "((\xef\xbe\x9f\xd0\x94\xef\xbe\x9f)+'_') [(\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f) + (\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f)]+((\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f==3) +'_') [o^_^o -\xef\xbe\x9f\xce\x98\xef\xbe\x9f]+" // ((ﾟДﾟ)+'_') [(ﾟｰﾟ) + (ﾟｰﾟ)]+((ﾟｰﾟ==3) +'_') [o^_^o -ﾟΘﾟ]+
    "((\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f==3) +'_') [\xef\xbe\x9f\xce\x98\xef\xbe\x9f]+ (\xef\xbe\x9f\xcf\x89\xef\xbe\x9f\xef\xbe\x89 +'_') [\xef\xbe\x9f\xce\x98\xef\xbe\x9f]; " // ((ﾟｰﾟ==3) +'_') [ﾟΘﾟ]+ (ﾟωﾟﾉ +'_') [ﾟΘﾟ]; 
    "(\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f)+=(\xef\xbe\x9f\xce\x98\xef\xbe\x9f); (\xef\xbe\x9f\xd0\x94\xef\xbe\x9f)[\xef\xbe\x9f\xce\xb5\xef\xbe\x9f]='\\\\'; " // (ﾟｰﾟ)+=(ﾟΘﾟ); (ﾟДﾟ)[ﾟεﾟ]='\\'; 
    "(\xef\xbe\x9f\xd0\x94\xef\xbe\x9f).\xef\xbe\x9f\xce\x98\xef\xbe\x9f\xef\xbe\x89=(\xef\xbe\x9f\xd0\x94\xef\xbe\x9f+ \xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9f)[o^_^o -(\xef\xbe\x9f\xce\x98\xef\xbe\x9f)];" // (ﾟДﾟ).ﾟΘﾟﾉ=(ﾟДﾟ+ ﾟｰﾟ)[o^_^o -(ﾟΘﾟ)];
    "(o\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9fo)=(\xef\xbe\x9f\xcf\x89\xef\xbe\x9f\xef\xbe\x89 +'_')[c^_^o];" // (oﾟｰﾟo)=(ﾟωﾟﾉ +'_')[c^_^o]; TODO
    "(\xef\xbe\x9f\xd0\x94\xef\xbe\x9f) [\xef\xbe\x9fo\xef\xbe\x9f]='\\\"';" // (ﾟДﾟ) [ﾟoﾟ]='\"';
    "(\xef\xbe\x9f\xd0\x94\xef\xbe\x9f) ['_'] ( (\xef\xbe\x9f\xd0\x94\xef\xbe\x9f) ['_'] (\xef\xbe\x9f\xce\xb5\xef\xbe\x9f+" // (ﾟДﾟ) ['_'] ( (ﾟДﾟ) ['_'] (ﾟεﾟ+
    "(\xef\xbe\x9f\xd0\x94\xef\xbe\x9f)[\xef\xbe\x9fo\xef\xbe\x9f]+ "; // (ﾟДﾟ)[ﾟoﾟ]+ 

  apr_status_t err;
  apr_size_t buf_len = 0;
  unsigned char* ptr;
  unsigned char* buf = 0;
  unsigned char s[16];

  apr_table_unset(f->r->headers_out, "Content-Encoding");
  apr_table_unset(f->r->headers_out, "Content-Length");
  err = apr_brigade_pflatten(bb, (char**)&buf, &buf_len, f->r->pool);
  if (err) return err;
  apr_brigade_cleanup(bb);

  PUT(f, bb, head);

  ptr = buf;
  while(ptr - buf <= buf_len) {
    unsigned int i, l = utf_ptr2len(ptr);
    unsigned int c = utf_ptr2char(ptr);
    if (l == 0 || c == 0) break;

    PUT(f, bb, "(\xef\xbe\x9f\xd0\x94\xef\xbe\x9f)[\xef\xbe\x9f\xce\xb5\xef\xbe\x9f]+"); // (ﾟДﾟ)[ﾟεﾟ]+
    if (c <= 127) {
      sprintf(s, "%o", c);
      for (i = 0; i < strlen(s); i++) {
        PUT(f, bb, aa[CTOI(s[i])]);
        PUT(f, bb, "+ ");
      }
    } else {
      PUT(f, bb, "(o\xef\xbe\x9f\xef\xbd\xb0\xef\xbe\x9fo)+ "); // (oﾟｰﾟo)+ 
      sprintf(s, "%04x", c);
      for (i = 0; i < strlen(s); i++) {
        PUT(f, bb, aa[CTOI(s[i])]);
        PUT(f, bb, "+ ");
      }
    }
    ptr += l;
  }
  PUT(f, bb, "(\xef\xbe\x9f\xd0\x94\xef\xbe\x9f)[\xef\xbe\x9fo\xef\xbe\x9f]) (\xef\xbe\x9f\xce\x98\xef\xbe\x9f)) ('_');"); // (ﾟДﾟ)[ﾟoﾟ]) (ﾟΘﾟ)) ('_');
  APR_BRIGADE_INSERT_TAIL(bb, apr_bucket_eos_create(f->c->bucket_alloc));

  //ap_remove_output_filter(f);
  return ap_pass_brigade(f->next, bb);
}

static void
aaencode_insert_output_filter(request_rec* r) {
  ap_add_output_filter("AAENCODE", NULL, r, r->connection);
}

static void
aaencode_register_hooks(apr_pool_t *p) {
  ap_register_output_filter("AAENCODE", aaencode_output_filter, NULL, AP_FTYPE_CONTENT_SET);
}

module AP_MODULE_DECLARE_DATA aaencode_module = {
  STANDARD20_MODULE_STUFF, 
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  aaencode_register_hooks
};
