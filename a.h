typedef struct Chart Chart;
typedef struct Price Price;

struct Price
{
	Tm date;
	double open;
	double close;
	double high;
	double low;
};

enum
{
	Maxprices = 1024,
};

struct Chart
{
	Price prices[Maxprices];
	usize nprices;
	double ymin;
	double ymax;
};

