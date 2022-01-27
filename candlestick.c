#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include "a.h"

enum
{
	BACK,
	TEXT,
	AXIS,
	DIVS,
	LINE,
	BULL,
	BEAR,
	NCOLS,
};

enum
{
	Padding = 4,
};

Chart chart;
char *title = "^FCHI";
Mousectl *mc;
Keyboardctl *kc;
Image *cols[NCOLS];
Font *afont;
Rectangle chartr;
Rectangle arear;
Point pmin;
Point pmax;
Point pt;
Point pl;
int offset;
int maxprices;

#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define MAX(x,y) ((x) > (y) ? (x) : (y))

double
parsedouble(char *s)
{
	char *e;
	double d;

	d = strtod(s, &e);
	if(e == nil || e == s)
		return -1;
	return d;
}

void
loadprices(Chart *chart, int fd)
{
	Biobuf *bp;
	char *s, *f[5];
	int n, l;
	Price *price;

	bp = Bfdopen(fd, OREAD);
	if(bp == nil)
		sysfatal("Bfdopen: %r");
	chart->ymin = 1000000000;
	chart->ymax = 0.0;
	for(l = 0; ;l++){
		s = Brdstr(bp, '\n', 1);
		if(s == nil)
			break;
		n = getfields(s, f, nelem(f), 0, ",");
		if(n != 5){
			fprint(2, "invalid input at line %d\n", l+1);
			continue;
		}
		price = &chart->prices[chart->nprices];
		if(tmparse(&price->date, "YYYY-MM-DD", f[0], nil, nil) == nil){
			fprint(2, "invalid date '%s' at line %d\n", f[0], l+1);
			continue;
		}
		price->open  = parsedouble(f[1]);
		price->high  = parsedouble(f[2]);
		price->low   = parsedouble(f[3]);
		price->close = parsedouble(f[4]);
		chart->ymin  = MIN(chart->ymin, price->low);
		chart->ymax  = MAX(chart->ymax, price->high);
		chart->nprices += 1;
	}
	if(chart->nprices == 0)
		sysfatal("could not parse input");
}

Point
stringdbl(Image *b, Point p, Image *c, Point sp, Font *f, double d)
{
	char buf[32] = {0};

	snprint(buf, sizeof buf, "%f", d);
	return string(b, p, c, sp, f, buf);
}

void
drawlegend(Price *price)
{
	char buf[255] = {0};
	Point p;
	Image *c;

	p = Pt(chartr.max.x, chartr.min.y - 1);
	draw(screen, Rpt(pl, p), cols[BACK], nil, ZP);
	if(price == nil){
		flushimage(display, 1);
		return;
	}
	c = price->open < price->close ? cols[BULL] : cols[BEAR];
	p = pl;
	if(title != nil)
		p = string(screen, p, cols[TEXT], ZP, font, " - ");
	snprint(buf, sizeof buf, "%τ", tmfmt(&price->date, "DD/MM/YYYY"));
	p = string(screen, p, cols[TEXT], ZP, font, buf);
	p = string(screen, p, cols[TEXT], ZP, font, " O:");
	p = stringdbl(screen, p, c, ZP, font, price->open);
	p = string(screen, p, cols[TEXT], ZP, font, " H:");
	p = stringdbl(screen, p, c, ZP, font, price->high);
	p = string(screen, p, cols[TEXT], ZP, font, " L:");
	p = stringdbl(screen, p, c, ZP, font, price->low);
	p = string(screen, p, cols[TEXT], ZP, font, " C:");
	p = stringdbl(screen, p, c, ZP, font, price->close);
	flushimage(display, 1);
}

void
drawyaxis(void)
{
	char buf[32] = {0};
	Point p, q;
	int i, n;
	double v, dv;

	border(screen, arear, 1, cols[AXIS], ZP);
	n = (Dy(arear)-4*Padding) / 10;
	dv = (chart.ymax - chart.ymin) / 10;
	p = addpt(arear.min, Pt(1, 2*Padding));
	q = addpt(p, Pt(Dx(arear) - 2, 0));
	pmax = q;
	for(i = 0; i <= 10; i++){
		line(screen, p, q, 0, 0, 0, cols[DIVS], ZP);
		line(screen, addpt(q, Pt(1,0)), addpt(q, Pt(5,0)), 0, 0, 0, cols[AXIS], ZP);
		if(i == 10) v = chart.ymin;
		else        v = chart.ymax - i*dv;
		snprint(buf, sizeof buf, "%f", v);
		string(screen, addpt(q, Pt(5+Padding, -font->height/3)), cols[AXIS], ZP, afont, buf);
		p.y += n;
		q.y += n;
	}
	pmin = subpt(q, Pt(0, n));
}

void
drawxtick(Price *price, int x)
{
	Point p, q;
	char buf[12] = {0};
	int w;

	p = Pt(x, arear.min.y + 1);
	q = Pt(x, arear.max.y - 1);
	line(screen, p, q, 0, 0, 0, cols[DIVS], ZP);
	line(screen, addpt(q, Pt(0,1)), addpt(q, Pt(0,5)), 0, 0, 0, cols[AXIS], ZP);
	snprint(buf, sizeof buf, "%τ", tmfmt(&price->date, "DD/MM/YYYY"));
	w = stringwidth(afont, buf);
	string(screen, addpt(q, Pt(-w/2, 5+Padding)), cols[AXIS], ZP, afont, buf);
}

int
pointy(double v)
{
	int y;
	double Δv, Δp, Δy;

	Δv = v - chart.ymin;
	Δp = pmax.y - pmin.y;
	Δy = chart.ymax - chart.ymin;
	y = pmin.y + Δp * (Δv / Δy);
	return y;
}

void
redraw(void)
{
	Point open, high, low, close;
	Image *c;
	Price *price;
	int i, x;

	draw(screen, screen->r, cols[BACK], nil, ZP);
	if(title != nil)
		pl = string(screen, pt, cols[TEXT], ZP, font, title);
	else
		pl = pt;
	drawyaxis();
	for(i = offset; i-offset < maxprices-1 && i < chart.nprices; i++){
		price = &chart.prices[i];
		c = price->open < price->close ? cols[BULL] : cols[BEAR];
		x = arear.min.x + 6*(i-offset+1) + 2;
		if(x+4 >= arear.max.x){
			fprint(2, "should not happen\n");
			break;
		}
		open  = Pt(x, pointy(price->open));
		high  = Pt(x, pointy(price->high));
		low   = Pt(x, pointy(price->low));
		close = Pt(x, pointy(price->close));
		if(i > 0 && i%15 == 0)
			drawxtick(price, x);
		line(screen, high, low, 0, 0, 0, cols[LINE], ZP);
		line(screen, open, close, 0, 0, 2, c, ZP);
	}
	flushimage(display, 1);
}

void
resize(void)
{
	Point p, q;
	int wmin, wmax, w;
	char buf[32] = {0};

	snprint(buf, sizeof buf, "%f", chart.ymin);
	wmin = stringwidth(afont, buf);
	snprint(buf, sizeof buf, "%f", chart.ymax);
	wmax = stringwidth(afont, buf);
	w = MAX(wmin, wmax) + Padding + Padding;
	pt = addpt(screen->r.min, Pt(Padding, Padding));
	p  = addpt(pt, Pt(0, font->height + Padding));
	q  = subpt(screen->r.max, Pt(Padding, Padding + font->height + Padding));
	chartr = Rpt(p, q);
	arear  = chartr;
	arear.max.x -= w;
	maxprices = 1+(Dx(arear) - 6)/6;
	if(maxprices >= chart.nprices)
		offset = 0;
	redraw();
}

Image*
initcol(ulong c)
{
	Image *i;

	i = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, c);
	if(i == nil)
		sysfatal("allocimage: %r");
	return i;
}

void
initcols(void)
{
	cols[BACK] = initcol(0x282828ff);
	cols[TEXT] = initcol(0xebdbb2ff);
	cols[AXIS] = initcol(0x858891ff);
	cols[DIVS] = initcol(0x333333ff);
	cols[LINE] = initcol(0xa89984ff);
	cols[BULL] = initcol(0x98971aff);
	cols[BEAR] = initcol(0xcc241dff);
}
	

void
threadmain(int argc, char *argv[])
{
	Mouse m;
	Rune k;
	Alt a[] = {
		{ nil, &m,  CHANRCV },
		{ nil, nil, CHANRCV },
		{ nil, &k,  CHANRCV },
		{ nil, nil, CHANEND },
	};
	int n, l;

	title = nil;
	ARGBEGIN{
	case 't':
		title = ARGF();
		break;
	}ARGEND;
	tmfmtinstall();
	loadprices(&chart, 0);
	if(initdraw(nil, nil, argv0) < 0)
		sysfatal("initdraw: %r");
	display->locking = 0;
	if((mc = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	if((kc = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");
	a[0].c = mc->c;
	a[1].c = mc->resizec;
	a[2].c = kc->c;
	initcols();
	afont = openfont(display, "/lib/font/bit/misc/unicode.6x13.font");
	if(afont == nil)
		sysfatal("openfont: %r");
	offset = 0;
	resize();
	l = 0;
	for(;;){
		switch(alt(a)){
		case 0:
			if(ptinrect(m.xy, arear)){
				n = offset + ((m.xy.x - arear.min.x) / 6) - 1;
				if(n > 0 && n < chart.nprices){
					drawlegend(&chart.prices[n]);
					l = 1;
				}else if(l){
					drawlegend(nil);
					l = 0;
				}
			}else if(l){
				drawlegend(nil);
				l = 0;
			}
			break;
		case 1:
			if(getwindow(display, Refnone) < 0)
				sysfatal("getwindow: %r");
			resize();
			break;
		case 2:
			switch(k){
			case Kleft:
				if(offset > 0 && maxprices < chart.nprices){
					offset -= 10;
					redraw();
				}
				break;
			case Kright:
				if(offset+maxprices < chart.nprices){
					offset += 10;
					redraw();
				}
				break;
			case 'q':
			case Kdel:
				threadexitsall(nil);
			}
			break;
		}
	}
}

