/* Scoring from throughput and the geometric mean. */
#ifndef CPUBENCH_SCORE_H
#define CPUBENCH_SCORE_H

double score_from_throughput(int workload_index, double throughput);
double score_geomean(const double *scores, int count);

#endif
