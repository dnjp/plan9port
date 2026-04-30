
#line	2	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
#include "common.h"
#include <ctype.h>
#include "smtpd.h"

#define YYSTYPE zzstype
typedef struct quux zzstype;
struct quux {
	String	*s;
	int	c;
};
Biobuf *zzfp;
YYSTYPE *bang;
extern Biobuf bin;
extern int debug;

YYSTYPE cat(YYSTYPE*, YYSTYPE*, YYSTYPE*, YYSTYPE*, YYSTYPE*, YYSTYPE*, YYSTYPE*);
int zzparse(void);
int zzlex(void);
YYSTYPE anonymous(void);
extern	int	zzerrflag;
#ifndef	YYMAXDEPTH
#define	YYMAXDEPTH	150
#endif
#ifndef	YYSTYPE
#define	YYSTYPE	int
#endif
YYSTYPE	zzlval;
YYSTYPE	zzval;
#define	SPACE	57346
#define	CNTRL	57347
#define	CRLF	57348
#define YYEOFCODE 1
#define YYERRCODE 2

#line	181	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"


void
parseinit(void)
{
	bang = (YYSTYPE*)malloc(sizeof(YYSTYPE));
	bang->c = '!';
	bang->s = 0;
	zzfp = &bin;
}

int
zzlex(void)
{
	int c;

	for(;;){
		c = Bgetc(zzfp);
		if(c == -1)
			return 0;
		if(debug)
			fprint(2, "%c", c);
		zzlval.c = c = c & 0x7F;
		if(c == '\n'){
			return CRLF;
		}
		if(c == '\r'){
			c = Bgetc(zzfp);
			if(c != '\n'){
				Bungetc(zzfp);
				c = '\r';
			} else {
				if(debug)
					fprint(2, "%c", c);
				return CRLF;
			}
		}
		if(isalpha(c))
			return tolower(c);
		if(isspace(c))
			return SPACE;
		if(iscntrl(c))
			return CNTRL;
		return c;
	}
}

YYSTYPE
cat(YYSTYPE *y1, YYSTYPE *y2, YYSTYPE *y3, YYSTYPE *y4, YYSTYPE *y5, YYSTYPE *y6, YYSTYPE *y7)
{
	YYSTYPE rv;

	memset(&rv, 0, sizeof rv);
	if(y1->s)
		rv.s = y1->s;
	else {
		rv.s = s_new();
		s_putc(rv.s, y1->c);
		s_terminate(rv.s);
	}
	if(y2){
		if(y2->s){
			s_append(rv.s, s_to_c(y2->s));
			s_free(y2->s);
		} else {
			s_putc(rv.s, y2->c);
			s_terminate(rv.s);
		}
	} else
		return rv;
	if(y3){
		if(y3->s){
			s_append(rv.s, s_to_c(y3->s));
			s_free(y3->s);
		} else {
			s_putc(rv.s, y3->c);
			s_terminate(rv.s);
		}
	} else
		return rv;
	if(y4){
		if(y4->s){
			s_append(rv.s, s_to_c(y4->s));
			s_free(y4->s);
		} else {
			s_putc(rv.s, y4->c);
			s_terminate(rv.s);
		}
	} else
		return rv;
	if(y5){
		if(y5->s){
			s_append(rv.s, s_to_c(y5->s));
			s_free(y5->s);
		} else {
			s_putc(rv.s, y5->c);
			s_terminate(rv.s);
		}
	} else
		return rv;
	if(y6){
		if(y6->s){
			s_append(rv.s, s_to_c(y6->s));
			s_free(y6->s);
		} else {
			s_putc(rv.s, y6->c);
			s_terminate(rv.s);
		}
	} else
		return rv;
	if(y7){
		if(y7->s){
			s_append(rv.s, s_to_c(y7->s));
			s_free(y7->s);
		} else {
			s_putc(rv.s, y7->c);
			s_terminate(rv.s);
		}
	} else
		return rv;
	return rv;
}

void
zzerror(char *x)
{
	USED(x);
}

/*
 *  an anonymous user
 */
YYSTYPE
anonymous(void)
{
	YYSTYPE rv;

	memset(&rv, 0, sizeof rv);
	rv.s = s_copy("/dev/null");
	return rv;
}
static	const	short	zzexca[] =
{-1, 1,
	1, -1,
	-2, 0,
};
#define	YYNPROD	162
#define	YYPRIVATE 57344
#define	YYLAST	1427
static	const	short	zzact[] =
{
 235, 234, 301, 136, 186, 176, 135, 177, 137, 181,
 192, 269,  91, 251, 178,  88, 248, 218,  85, 278,
 254, 182, 183, 242, 219, 171, 304, 253, 132, 263,
 264, 294,  86, 120, 121, 122, 123, 124, 125, 126,
 127, 128, 129, 261, 236,  18,  67,  61,  64,  52,
  66,  69,  70,  71,  72,  73,  40, 228,  76,  77,
  78,  21,  80, 233,  22,  19,  84,  57, 130,  49,
  93, 236,  62,  92,  93,  37, 174,  92,  35, 299,
  50, 297, 166, 162,  79,  63,  55, 170,  93, 159,
 131,  92, 160, 173, 173,  93,  54, 172,  92,  48,
 168, 167,  39, 296, 180, 120, 121, 122, 123, 124,
 125, 126, 127, 128, 129,  24,  32,  25,  31,  26,
  30, 262, 258, 211,   3,  27, 257, 256,  15,   4,
   5, 243, 224,   6,  14, 210, 209,   7, 188, 207,
  13, 187,  60, 190,   8,   9,  11,  10,  47,  28,
  12, 165, 164, 163, 161,  44,  46,  36, 293,  56,
 185,  43,  23,  20, 239, 238, 237, 232,  42, 214,
  41, 227, 226, 225, 223,  93, 217, 208,  92,  51,
  45, 217,  29,  93,  93, 212,  92,  92, 220, 185,
  59,  58, 180, 180, 221, 222, 185,  53,  34,  33,
  38,  17, 298,  67, 216, 279,  67,  67, 215,  68,
 189,  65, 305, 291, 290, 289, 244, 240, 205, 169,
  83,  82,  81, 173,  75, 230,  74,  67,   2, 133,
  16, 231, 267, 179, 252, 229, 249, 245, 175,  87,
 250, 300,   1,   0,   0,   0, 173,   0, 259,   0,
   0,   0,   0,   0,   0,   0,   0,  93, 185, 271,
  92, 280, 270, 247, 273,   0, 272,  93,   0,   0,
  92, 271, 188, 288, 270, 187, 273, 190, 272, 287,
 284, 283, 173, 185, 292,   0, 265,   0,   0, 281,
 188, 295,   0, 187,   0, 190, 282,   0,   0,   0,
 302, 303,   0,   0, 303, 306,   0, 260,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
 275, 276, 277, 191, 195,   0, 101,  98, 105, 108,
 106,  94, 102,  99, 111, 200, 114, 113, 148,  96,
 109,  97, 112, 107, 115, 118, 117, 110, 202, 203,
 198, 204, 199, 139, 150, 151, 194, 193, 146, 153,
 196, 197, 201, 138, 140, 141, 142, 143, 144, 145,
 147, 149, 152, 154, 155, 156, 157, 158,  95, 100,
 103, 104, 116, 119, 120, 121, 122, 123, 124, 125,
 126, 127, 128, 129, 274, 195,   0, 101,  98, 105,
 108, 106,  94, 102,  99, 111, 200, 114, 113, 148,
  96, 109,  97, 112, 107, 115, 118, 117, 110, 202,
 203, 198, 204, 199, 139, 150, 151, 285, 286, 146,
 153, 196, 197, 201, 138, 140, 141, 142, 143, 144,
 145, 147, 149, 152, 154, 155, 156, 157, 158,  95,
 100, 103, 104, 116, 119, 120, 121, 122, 123, 124,
 125, 126, 127, 128, 129, 274, 195,   0, 101,  98,
 105, 108, 106,  94, 102,  99, 111, 200, 114, 113,
 148,  96, 109,  97, 112, 107, 115, 118, 117, 110,
 202, 203, 198, 204, 199, 139, 150, 151,   0, 268,
 146, 153, 196, 197, 201, 138, 140, 141, 142, 143,
 144, 145, 147, 149, 152, 154, 155, 156, 157, 158,
  95, 100, 103, 104, 116, 119, 120, 121, 122, 123,
 124, 125, 126, 127, 128, 129,  67,   0,   0, 101,
  98, 105, 108, 106,  94, 102,  99, 111,   0, 114,
 113, 148,  96, 109,  97, 112, 107, 115, 118, 117,
 110, 236,   0,   0,   0,   0, 139, 150, 151, 255,
 134, 146, 153,   0,   0,   0, 138, 140, 141, 142,
 143, 144, 145, 147, 149, 152, 154, 155, 156, 157,
 158,  95, 100, 103, 104, 116, 119, 120, 121, 122,
 123, 124, 125, 126, 127, 128, 129, 101,  98, 105,
 108, 106,  94, 102,  99, 111,   0, 114, 113, 148,
  96, 109,  97, 112, 107, 115, 118, 117, 110,   0,
 246,   0, 253,   0, 139, 150, 151, 255, 134, 146,
 153,   0,   0,   0, 138, 140, 141, 142, 143, 144,
 145, 147, 149, 152, 154, 155, 156, 157, 158,  95,
 100, 103, 104, 116, 119, 120, 121, 122, 123, 124,
 125, 126, 127, 128, 129, 101,  98, 105, 108, 106,
  94, 102,  99, 111,   0, 114, 113, 148,  96, 109,
  97, 112, 107, 115, 118, 117, 110, 236,   0,   0,
   0,   0, 139, 150, 151, 255, 134, 146, 153,   0,
   0,   0, 138, 140, 141, 142, 143, 144, 145, 147,
 149, 152, 154, 155, 156, 157, 158,  95, 100, 103,
 104, 116, 119, 120, 121, 122, 123, 124, 125, 126,
 127, 128, 129, 101,  98, 105, 108, 106,  94, 102,
  99, 111,   0, 114, 113, 148,  96, 109,  97, 112,
 107, 115, 118, 117, 110,   0,   0,   0,   0,   0,
 139, 150, 151, 255, 134, 146, 153,   0,   0,   0,
 138, 140, 141, 142, 143, 144, 145, 147, 149, 152,
 154, 155, 156, 157, 158,  95, 100, 103, 104, 116,
 119, 120, 121, 122, 123, 124, 125, 126, 127, 128,
 129, 101,  98, 105, 108, 106,  94, 102,  99, 111,
   0, 114, 113, 148,  96, 109,  97, 112, 107, 115,
 118, 117, 110,   0,   0,   0,   0, 266, 139, 150,
 151,   0, 134, 146, 153,   0,   0,   0, 138, 140,
 141, 142, 143, 144, 145, 147, 149, 152, 154, 155,
 156, 157, 158,  95, 100, 103, 104, 116, 119, 120,
 121, 122, 123, 124, 125, 126, 127, 128, 129, 241,
 101,  98, 105, 108, 106,  94, 102,  99, 111,   0,
 114, 113, 148,  96, 109,  97, 112, 107, 115, 118,
 117, 110,   0,   0,   0,   0,   0, 139, 150, 151,
   0, 134, 146, 153,   0,   0,   0, 138, 140, 141,
 142, 143, 144, 145, 147, 149, 152, 154, 155, 156,
 157, 158,  95, 100, 103, 104, 116, 119, 120, 121,
 122, 123, 124, 125, 126, 127, 128, 129, 213, 101,
  98, 105, 108, 106,  94, 102,  99, 111,   0, 114,
 113, 148,  96, 109,  97, 112, 107, 115, 118, 117,
 110,   0,   0,   0,   0,   0, 139, 150, 151,   0,
 134, 146, 153,   0,   0,   0, 138, 140, 141, 142,
 143, 144, 145, 147, 149, 152, 154, 155, 156, 157,
 158,  95, 100, 103, 104, 116, 119, 120, 121, 122,
 123, 124, 125, 126, 127, 128, 129, 206, 101,  98,
 105, 108, 106,  94, 102,  99, 111,   0, 114, 113,
 148,  96, 109,  97, 112, 107, 115, 118, 117, 110,
   0,   0,   0,   0,   0, 139, 150, 151,   0, 134,
 146, 153,   0,   0,   0, 138, 140, 141, 142, 143,
 144, 145, 147, 149, 152, 154, 155, 156, 157, 158,
  95, 100, 103, 104, 116, 119, 120, 121, 122, 123,
 124, 125, 126, 127, 128, 129, 184, 101,  98, 105,
 108, 106,  94, 102,  99, 111,   0, 114, 113, 148,
  96, 109,  97, 112, 107, 115, 118, 117, 110,   0,
   0,   0,   0,   0, 139, 150, 151,   0, 134, 146,
 153,   0,   0,   0, 138, 140, 141, 142, 143, 144,
 145, 147, 149, 152, 154, 155, 156, 157, 158,  95,
 100, 103, 104, 116, 119, 120, 121, 122, 123, 124,
 125, 126, 127, 128, 129, 101,  98, 105, 108, 106,
  94, 102,  99, 111,   0, 114, 113, 148,  96, 109,
  97, 112, 107, 115, 118, 117, 110,   0,   0,   0,
   0,   0, 139, 150, 151,   0, 134, 146, 153,   0,
   0,   0, 138, 140, 141, 142, 143, 144, 145, 147,
 149, 152, 154, 155, 156, 157, 158,  95, 100, 103,
 104, 116, 119, 120, 121, 122, 123, 124, 125, 126,
 127, 128, 129, 101,  98, 105, 108, 106,  94, 102,
  99, 111,   0, 114, 113,   0,  96, 109,  97, 112,
 107, 115, 118, 117, 110,   0,   0,   0,   0,   0,
  89,  90,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,  95, 100, 103, 104, 116,
 119, 120, 121, 122, 123, 124, 125, 126, 127, 128,
 129, 101,  98, 105, 108, 106,  94, 102,  99, 111,
   0, 114, 113,   0,  96, 109,  97, 112, 107, 115,
 118, 117, 110,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0, 182, 183,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,  95, 100, 103, 104, 116, 119, 120,
 121, 122, 123, 124, 125, 126, 127, 128, 129, 101,
  98, 105, 108, 106,  94, 102,  99, 111,   0, 114,
 113,   0,  96, 109,  97, 112, 107, 115, 118, 117,
 110,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,  95, 100, 103, 104, 116, 119, 120, 121, 122,
 123, 124, 125, 126, 127, 128, 129
};
static	const	short	zzpact[] =
{
 122, 122,-1000,-1000, 193,  38, 151,  41, 150, 107,
 134, 172, 103, 101,  99,-1000,-1000, 190, 189,  57,
 144,  54, 192,  84,  32, 159, 157, 149, 141, 170,
 143, 133,  81,  59, 169,  25, 188,  78,  68, 147,
  45, 182, 181, 127,  21,  51,  67,  24, 204, 223,
 203, 223, 223, 223, 223, 220, 218, 223, 223, 223,
  66, 223, 216, 215, 214, 223,1216, 223,-1000,1148,
1216,1148, 140,  65,-1000,-1000, 139, 138, 137,  64,
1148,-1000,-1000,-1000,1352, 213, 223,  -8,-1000, -32,
  40,1284,-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,
-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,
-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,
-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,
-1000,1080,-1000,-1000, 319,-1000,-1000,-1000,-1000,-1000,
-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,
-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000, 212,
1011, 124, 167, 121, 120, 108, 176, 942, 202,-1000,
-1000,1216, -32,-1000,-1000, -19,  -9, -32, -18,1352,
1352,-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,
-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,
-1000,-1000,-1000,-1000,-1000,-1000,-1000, 164, 116, 163,
 162, 161,  34,-1000,1148,-1000,-1000,-1000,-1000, -32,
-1000, -18,-1000, 156,  42, 155, 154, 153, 211, 873,
 -10,-1000, 115, 210,-1000,  15, 600, 111, 110, 106,
-1000,-1000, -32,  42,-1000,-1000,-1000,  13, 105,  -3,
  -1,-1000,-1000,1216, 804, 461,  42,  42,  42, -14,
 199,-1000, 736,1216,  -5,-1000,1148, 390, 319,-1000,
-1000,-1000,-1000,-1000,-1000, 209, 208, 207, -32,-1000,
 146,   1,-1000,-1000,-1000,-1000, 319,-1000,-1000,-1000,
-1000,-1000,-1000,  86,-1000,-1000,  63, 195,  60, 532,
 206,-1000, 668,-1000,-1000,-1000,-1000
};
static	const	short	zzpgo[] =
{
   0, 242, 228,   0,  18,  63, 241,  20,  15,   1,
  26,  16,   2, 240,  32, 239,   7, 238, 236,  13,
 234,  12,  14, 233,   9,   6,   3,  28, 232,   4,
  11, 229,   5,  10, 210,   8
};
static	const	short	zzr1[] =
{
   0,   1,   1,   2,   2,   2,   2,   2,   2,   2,
   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
   2,   2,   2,   2,   2,   9,   9,   9,   5,   5,
  12,  12,   6,   6,  11,  11,  13,   4,   4,  14,
  14,  14,  15,  15,  15,  15,  10,  10,  18,  18,
   8,   8,   8,  22,  22,  23,  23,  23,  21,  21,
  19,  19,   7,   7,  20,  28,  28,  28,  28,  27,
  27,  17,  16,  16,  32,   3,   3,  24,  24,  33,
  33,  33,  33,  33,  33,  33,  33,  33,  33,  34,
  34,  34,  35,  35,  35,  35,  35,  35,  35,  35,
  35,  35,  35,  35,  35,  35,  35,  35,  35,  35,
  35,  35,  35,  25,  25,  25,  25,  25,  25,  25,
  25,  25,  25,  25,  25,  25,  25,  25,  25,  25,
  25,  25,  25,  25,  25,  25,  25,  25,  25,  26,
  26,  26,  26,  26,  26,  26,  26,  26,  26,  31,
  31,  31,  30,  30,  30,  30,  30,  29,  29,  29,
  29,  29
};
static	const	short	zzr2[] =
{
   0,   1,   2,   1,   7,   7,  12,  19,  10,   5,
   5,  12,  12,  12,   7,   7,   5,   7,   5,   5,
   5,   9,   9,   7,   1,   2,   3,   5,   1,   2,
   1,   1,   1,   2,   1,   3,   2,   1,   2,   1,
   2,   3,   1,   2,   2,   3,   1,   3,   1,   1,
   1,   2,   3,   1,   2,   1,   2,   3,   1,   1,
   1,   3,   1,   2,   3,   2,   3,   1,   2,   1,
   2,   7,   1,   2,   1,   1,   2,   1,   1,   1,
   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
   1,   1
};
static	const	short	zzchk[] =
{
-1000,  -1,  -2,   2,   7,   8,  11,  15,  22,  23,
  25,  24,  28,  18,  12,   6,  -2,   8,   7,  27,
  12,  20,  23,  12,   8,  10,  12,  18,  15,  10,
  17,  17,  17,   9,   9,  21,  13,  21,   8,  18,
  24,  11,  11,  12,  14,  10,  13,  15,  18,  10,
  21,  10,  24,   9,  18,  18,  12,  22,   9,   9,
  15,  26,  21,  18,  24,   7,  -3,   4,   6,  -3,
  -3,  -3,  -3,  -3,   6,   6,  -3,  -3,  -3,  18,
  -3,   6,   6,   6,  -3,  -4, -14, -15,  -8,  34,
  35, -21, -25, -26,  12,  59,  20,  22,   8,  14,
  60,   7,  13,  61,  62,   9,  11,  24,  10,  21,
  28,  15,  23,  18,  17,  25,  63,  27,  26,  64,
  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,
  -3,  -7, -27, -31,  38, -25, -26, -35,  44,  34,
  45,  46,  47,  48,  49,  50,  39,  51,  19,  52,
  35,  36,  53,  40,  54,  55,  56,  57,  58,  -4,
  -7,  14,  18,  14,  14,  14,  18,  -7,  -8,   6,
  -3,  33, -16, -26,  36, -17, -32, -16, -22, -23,
 -21, -24,  39,  40,   6, -27, -29, -25, -26, -34,
 -35,   4, -33,  38,  37,   5,  41,  42,  31,  33,
  16,  43,  29,  30,  32,   6,   6,  15,  10,  15,
  15,  15,   9,   6,  -3,   6, -14, -26,  36,  33,
 -24, -22, -22,  10,  16,  10,  10,  10,  23,  -7,
 -32, -24,  11,  -5,  -9,  -3,  29,  11,  11,  11,
   6,   6,  33,  16,   6,  -9,  30, -10, -11, -18,
 -13, -19, -20,  32,  -7,  37,  16,  16,  16, -32,
  -5,  30,  16,  32,  31, -14,  33, -28,  38, -30,
 -25, -26, -33, -35,   4,  -5,  -5,  -5,  33,   6,
  -3, -10, -14, -11, -19,  37,  38, -30, -29,   6,
   6,   6, -32,  12,  30, -29,  17,  18,   7,  19,
  -6, -12,  -3,  -9, -10,   6, -12
};
static	const	short	zzdef[] =
{
   0,  -2,   1,   3,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,  24,   2,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,  75,  16,   0,
   0,   0,   0,   0,  10,   9,   0,   0,   0,   0,
   0,  18,  19,  20,   0,   0,  37,  39,  42,   0,
   0,  50,  58,  59, 113, 114, 115, 116, 117, 118,
 119, 120, 121, 122, 123, 124, 125, 126, 127, 128,
 129, 130, 131, 132, 133, 134, 135, 136, 137, 138,
 139, 140, 141, 142, 143, 144, 145, 146, 147, 148,
  76,   0,  62,  69,   0, 149, 150, 151,  92,  93,
  94,  95,  96,  97,  98,  99, 100, 101, 102, 103,
 104, 105, 106, 107, 108, 109, 110, 111, 112,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   4,
  38,  40,  43,  72,  44,   0,   0,  74,  51,   0,
  53,  55,  77,  78,  17,  63,  70, 157, 158, 159,
 160, 161,  89,  90,  91,  79,  80,  81,  82,  83,
  84,  85,  86,  87,  88,   5,  15,   0,   0,   0,
   0,   0,   0,  14,   0,  23,  41,  73,  45,   0,
  56,  52,  54,   0,   0,   0,   0,   0,   0,   0,
   0,  57,   0,   0,  28,   0,   0,   0,   0,   0,
  21,  22,   0,   0,   8,  29,  25,   0,   0,  46,
  34,  48,  49,   0,  60,   0,   0,   0,   0,   0,
   0,  26,   0,   0,   0,  36,   0,   0,   0,  67,
 152, 153, 154, 155, 156,   0,   0,   0,   0,   6,
   0,   0,  47,  35,  61,  64,   0,  68,  65,  11,
  12,  13,  71,   0,  27,  66,   0,   0,   0,   0,
   0,  32,   0,  30,  31,   7,  33
};
static	const	short	zztok1[] =
{
   1,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,  44,  37,  34,  45,  46,  47,  48,
  41,  42,  49,  50,  31,  39,  33,  51,  65,  66,
  67,  68,  69,  70,  71,  72,  73,  74,  16,  43,
  29,  19,  30,  52,  32,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,  35,  38,  36,  53,  40,  54,  12,  59,  20,
  22,   8,  14,  60,   7,  13,  61,  62,   9,  11,
  24,  10,  21,  28,  15,  23,  18,  17,  25,  63,
  27,  26,  64,  55,  56,  57,  58
};
static	const	short	zztok2[] =
{
   2,   3,   4,   5,   6
};
static	const	long	zztok3[] =
{
   0
};
#define YYFLAG 		-1000
#define YYERROR		goto zzerrlab
#define YYACCEPT	return(0)
#define YYABORT		return(1)
#define	zzclearin	zzchar = -1
#define	zzerrok		zzerrflag = 0

#ifdef	zzdebug
#include	"y.debug"
#else
#define	zzdebug		0
static	const	char*	zztoknames[1];		/* for debugging */
static	const	char*	zzstates[1];		/* for debugging */
#endif

/*	parser for yacc output	*/
#ifdef YYARG
#define	zznerrs		zzarg->zznerrs
#define	zzerrflag	zzarg->zzerrflag
#define zzval		zzarg->zzval
#define zzlval		zzarg->zzlval
#else
int	zznerrs = 0;		/* number of errors */
int	zzerrflag = 0;		/* error recovery flag */
#endif

extern	int	fprint(int, char*, ...);
extern	int	sprint(char*, char*, ...);

static const char*
zztokname(int zzc)
{
	static char x[10];

	if(zzc > 0 && zzc <= sizeof(zztoknames)/sizeof(zztoknames[0]))
	if(zztoknames[zzc-1])
		return zztoknames[zzc-1];
	sprint(x, "<%d>", zzc);
	return x;
}

static const char*
zzstatname(int zzs)
{
	static char x[10];

	if(zzs >= 0 && zzs < sizeof(zzstates)/sizeof(zzstates[0]))
	if(zzstates[zzs])
		return zzstates[zzs];
	sprint(x, "<%d>\n", zzs);
	return x;
}

static long
#ifdef YYARG
zzlex1(struct Yyarg *zzarg)
#else
zzlex1(void)
#endif
{
	long zzchar;
	const long *t3p;
	int c;

#ifdef YYARG	
	zzchar = zzlex(zzarg);
#else
	zzchar = zzlex();
#endif
	if(zzchar <= 0) {
		c = zztok1[0];
		goto out;
	}
	if(zzchar < sizeof(zztok1)/sizeof(zztok1[0])) {
		c = zztok1[zzchar];
		goto out;
	}
	if(zzchar >= YYPRIVATE)
		if(zzchar < YYPRIVATE+sizeof(zztok2)/sizeof(zztok2[0])) {
			c = zztok2[zzchar-YYPRIVATE];
			goto out;
		}
	for(t3p=zztok3;; t3p+=2) {
		c = t3p[0];
		if(c == zzchar) {
			c = t3p[1];
			goto out;
		}
		if(c == 0)
			break;
	}
	c = 0;

out:
	if(c == 0)
		c = zztok2[1];	/* unknown char */
	if(zzdebug >= 3)
		fprint(2, "lex %.4lux %s\n", zzchar, zztokname(c));
	return c;
}

int
#ifdef YYARG
zzparse(struct Yyarg *zzarg)
#else
zzparse(void)
#endif
{
	struct
	{
		YYSTYPE	zzv;
		int	zzs;
	} zzs[YYMAXDEPTH], *zzp, *zzpt;
	const short *zzxi;
	int zzj, zzm, zzstate, zzn, zzg;
	long zzchar;
#ifndef YYARG
	YYSTYPE save1, save2;
	int save3, save4;

	save1 = zzlval;
	save2 = zzval;
	save3 = zznerrs;
	save4 = zzerrflag;
#endif

	zzstate = 0;
	zzchar = -1;
	zznerrs = 0;
	zzerrflag = 0;
	zzp = &zzs[0];
	zzp--;
	goto zzstack;

ret0:
	zzn = 0;
	goto ret;

ret1:
	zzn = 1;
	goto ret;

ret:
#ifndef YYARG
	zzlval = save1;
	zzval = save2;
	zznerrs = save3;
	zzerrflag = save4;
#endif
	return zzn;

zzstack:
	/* put a state and value onto the stack */
	if(zzdebug >= 4)
		fprint(2, "char %s in %s", zztokname(zzchar), zzstatname(zzstate));

	zzp++;
	if(zzp >= &zzs[YYMAXDEPTH]) {
		zzerror("yacc stack overflow");
		goto ret1;
	}
	zzp->zzs = zzstate;
	zzp->zzv = zzval;

zznewstate:
	zzn = zzpact[zzstate];
	if(zzn <= YYFLAG)
		goto zzdefault; /* simple state */
	if(zzchar < 0)
#ifdef YYARG
		zzchar = zzlex1(zzarg);
#else
		zzchar = zzlex1();
#endif
	zzn += zzchar;
	if(zzn < 0 || zzn >= YYLAST)
		goto zzdefault;
	zzn = zzact[zzn];
	if(zzchk[zzn] == zzchar) { /* valid shift */
		zzchar = -1;
		zzval = zzlval;
		zzstate = zzn;
		if(zzerrflag > 0)
			zzerrflag--;
		goto zzstack;
	}

zzdefault:
	/* default state action */
	zzn = zzdef[zzstate];
	if(zzn == -2) {
		if(zzchar < 0)
#ifdef YYARG
		zzchar = zzlex1(zzarg);
#else
		zzchar = zzlex1();
#endif

		/* look through exception table */
		for(zzxi=zzexca;; zzxi+=2)
			if(zzxi[0] == -1 && zzxi[1] == zzstate)
				break;
		for(zzxi += 2;; zzxi += 2) {
			zzn = zzxi[0];
			if(zzn < 0 || zzn == zzchar)
				break;
		}
		zzn = zzxi[1];
		if(zzn < 0)
			goto ret0;
	}
	if(zzn == 0) {
		/* error ... attempt to resume parsing */
		switch(zzerrflag) {
		case 0:   /* brand new error */
			zzerror("syntax error");
			if(zzdebug >= 1) {
				fprint(2, "%s", zzstatname(zzstate));
				fprint(2, "saw %s\n", zztokname(zzchar));
			}
			goto zzerrlab;
		zzerrlab:
			zznerrs++;

		case 1:
		case 2: /* incompletely recovered error ... try again */
			zzerrflag = 3;

			/* find a state where "error" is a legal shift action */
			while(zzp >= zzs) {
				zzn = zzpact[zzp->zzs] + YYERRCODE;
				if(zzn >= 0 && zzn < YYLAST) {
					zzstate = zzact[zzn];  /* simulate a shift of "error" */
					if(zzchk[zzstate] == YYERRCODE)
						goto zzstack;
				}

				/* the current zzp has no shift onn "error", pop stack */
				if(zzdebug >= 2)
					fprint(2, "error recovery pops state %d, uncovers %d\n",
						zzp->zzs, (zzp-1)->zzs );
				zzp--;
			}
			/* there is no state on the stack with an error shift ... abort */
			goto ret1;

		case 3:  /* no shift yet; clobber input char */
			if(zzdebug >= 2)
				fprint(2, "error recovery discards %s\n", zztokname(zzchar));
			if(zzchar == YYEOFCODE)
				goto ret1;
			zzchar = -1;
			goto zznewstate;   /* try again in the same state */
		}
	}

	/* reduction by production zzn */
	if(zzdebug >= 2)
		fprint(2, "reduce %d in:\n\t%s", zzn, zzstatname(zzstate));

	zzpt = zzp;
	zzp -= zzr2[zzn];
	zzval = (zzp+1)->zzv;
	zzm = zzn;

	/* consult goto table to find next state */
	zzn = zzr1[zzn];
	zzg = zzpgo[zzn];
	zzj = zzg + zzp->zzs + 1;

	if(zzj >= YYLAST || zzchk[zzstate=zzact[zzj]] != -zzn)
		zzstate = zzact[zzg];
	switch(zzm) {
		
case 4:
#line	34	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ hello(zzpt[-1].zzv.s, 0); } break;
case 5:
#line	36	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ hello(zzpt[-1].zzv.s, 1); } break;
case 6:
#line	38	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ sender(zzpt[-1].zzv.s); } break;
case 7:
#line	40	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ sender(zzpt[-8].zzv.s); } break;
case 8:
#line	42	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ receiver(zzpt[-1].zzv.s); } break;
case 9:
#line	44	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ data(); } break;
case 10:
#line	46	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ reset(); } break;
case 11:
#line	48	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ sender(zzpt[-1].zzv.s); } break;
case 12:
#line	50	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ sender(zzpt[-1].zzv.s); } break;
case 13:
#line	52	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ sender(zzpt[-1].zzv.s); } break;
case 14:
#line	54	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ verify(zzpt[-1].zzv.s); } break;
case 15:
#line	56	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ verify(zzpt[-1].zzv.s); } break;
case 16:
#line	58	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ help(0); } break;
case 17:
#line	60	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ help(zzpt[-1].zzv.s); } break;
case 18:
#line	62	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ noop(); } break;
case 19:
#line	64	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ quit(); } break;
case 20:
#line	66	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ turn(); } break;
case 21:
#line	68	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ starttls(); } break;
case 22:
#line	70	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ auth(zzpt[-3].zzv.s, zzpt[-1].zzv.s); } break;
case 23:
#line	72	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ auth(zzpt[-1].zzv.s, nil); } break;
case 24:
#line	74	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ reply("501 illegal command or bad syntax\r\n"); } break;
case 25:
#line	76	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = anonymous(); } break;
case 26:
#line	77	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = zzpt[-1].zzv; } break;
case 27:
#line	78	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-3].zzv, bang, &zzpt[-1].zzv, 0, 0 ,0, 0); } break;
case 28:
#line	80	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = zzpt[-0].zzv; } break;
case 29:
#line	81	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = zzpt[-0].zzv; } break;
case 30:
#line	83	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = zzpt[-0].zzv; } break;
case 31:
#line	84	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = zzpt[-0].zzv; } break;
case 32:
#line	86	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = zzpt[-0].zzv; } break;
case 33:
#line	87	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = zzpt[-0].zzv; } break;
case 34:
#line	90	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-0].zzv, 0, 0, 0, 0 ,0, 0); } break;
case 35:
#line	91	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-2].zzv, bang, &zzpt[-0].zzv, 0, 0, 0, 0); } break;
case 36:
#line	93	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-0].zzv, 0, 0, 0, 0 ,0, 0); } break;
case 37:
#line	95	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = zzpt[-0].zzv; } break;
case 38:
#line	96	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = zzpt[-1].zzv; } break;
case 39:
#line	98	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-0].zzv, 0, 0, 0, 0 ,0, 0); } break;
case 40:
#line	99	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-1].zzv, 0, 0, 0, 0 ,0, 0); } break;
case 41:
#line	100	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-2].zzv, &zzpt[-1].zzv, &zzpt[-0].zzv, 0, 0 ,0, 0); } break;
case 42:
#line	102	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-0].zzv, 0, 0, 0, 0 ,0, 0); } break;
case 43:
#line	103	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-1].zzv, &zzpt[-0].zzv, 0, 0, 0 ,0, 0); } break;
case 44:
#line	104	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-1].zzv, &zzpt[-0].zzv, 0, 0, 0 ,0, 0); } break;
case 45:
#line	105	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-2].zzv, &zzpt[-1].zzv, &zzpt[-0].zzv, 0, 0 ,0, 0); } break;
case 46:
#line	107	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-0].zzv, 0, 0, 0, 0 ,0, 0); } break;
case 47:
#line	108	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-0].zzv, bang, &zzpt[-2].zzv, 0, 0 ,0, 0); } break;
case 48:
#line	110	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-0].zzv, 0, 0, 0, 0 ,0, 0); } break;
case 49:
#line	111	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-0].zzv, 0, 0, 0, 0 ,0, 0); } break;
case 50:
#line	113	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-0].zzv, 0, 0, 0, 0 ,0, 0); } break;
case 51:
#line	114	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-1].zzv, &zzpt[-0].zzv, 0, 0, 0 ,0, 0); } break;
case 52:
#line	115	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-2].zzv, &zzpt[-1].zzv, &zzpt[-0].zzv, 0, 0 ,0, 0); } break;
case 54:
#line	118	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-1].zzv, &zzpt[-0].zzv, 0, 0, 0 ,0, 0); } break;
case 56:
#line	121	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-1].zzv, &zzpt[-0].zzv, 0, 0, 0 ,0, 0); } break;
case 57:
#line	122	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-2].zzv, &zzpt[-1].zzv, &zzpt[-0].zzv, 0, 0 ,0, 0); } break;
case 60:
#line	127	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-0].zzv, 0, 0, 0, 0 ,0, 0); } break;
case 61:
#line	128	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-2].zzv, &zzpt[-1].zzv, &zzpt[-0].zzv, 0, 0 ,0, 0); } break;
case 62:
#line	131	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-0].zzv, 0, 0, 0, 0 ,0, 0); } break;
case 63:
#line	132	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-1].zzv, &zzpt[-0].zzv, 0, 0, 0 ,0, 0); } break;
case 64:
#line	135	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-2].zzv, &zzpt[-1].zzv, &zzpt[-0].zzv, 0, 0 ,0, 0); } break;
case 65:
#line	137	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-0].zzv, 0, 0, 0, 0 ,0, 0); } break;
case 66:
#line	138	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-2].zzv, &zzpt[-0].zzv, 0, 0, 0 ,0, 0); } break;
case 68:
#line	140	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-1].zzv, &zzpt[-0].zzv, 0, 0, 0 ,0, 0); } break;
case 70:
#line	143	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = zzpt[-0].zzv; } break;
case 71:
#line	145	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-6].zzv, &zzpt[-5].zzv, &zzpt[-4].zzv, &zzpt[-3].zzv, &zzpt[-2].zzv, &zzpt[-1].zzv, &zzpt[-0].zzv); } break;
case 72:
#line	147	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-0].zzv, 0, 0, 0, 0 ,0, 0); } break;
case 73:
#line	148	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = cat(&zzpt[-1].zzv, &zzpt[-0].zzv, 0, 0, 0 ,0, 0); } break;
case 74:
#line	150	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ if(atoi(s_to_c(zzpt[-0].zzv.s)) > 255) print("bad snum\n"); } break;
case 75:
#line	152	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = zzpt[-0].zzv; } break;
case 76:
#line	153	"/usr/local/plan9/src/cmd/upas/smtp/smtpd.y"
{ zzval = zzpt[-1].zzv; } break;
	}
	goto zzstack;  /* stack new state and value */
}
