#include <assert.h>
#include "common.h"
#include "point.h"
#include <math.h>

void
point_translate(struct point *p, double x, double y)
{
	double old_x = point_X(p);
	double old_y = point_Y(p);
	point_set(p, old_x+x, old_y+y);
	return;
}

double
point_distance(const struct point *p1, const struct point *p2)
{
	return sqrt((point_X(p1)-point_X(p2))*(point_X(p1)-point_X(p2)) +  (point_Y(p1)-point_Y(p2))*(point_Y(p1)-point_Y(p2))); 
}

int
point_compare(const struct point *p1, const struct point *p2)
{
	double p1_dist = sqrt((point_X(p1))*(point_X(p1)) +  (point_Y(p1))*(point_Y(p1)));
	double p2_dist = sqrt((point_X(p2))*(point_X(p2)) +  (point_Y(p2))*(point_Y(p2)));
	if (p1_dist > p2_dist) return 1; 
	else if (p1_dist == p2_dist) return 0;
	else return -1; 
}
