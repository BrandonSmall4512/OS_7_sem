#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <mpi.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline double f(double x) {
    return sin(x);
}

static inline void split_work(long long n, int size, int rank, long long *start, long long *count) {
    long long base = n / size;
    long long r = n % size;
    if (rank < r) {
        *count = base + 1;
        *start = rank * (*count);
    } else {
        *count = base;
        *start = r * (base + 1) + (rank - r) * base;
    }
}

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    double a = 0.0, b = M_PI;   
    long long n = 100000000LL;
    
    if (argc == 4) {
        a = atof(argv[1]);
        b = atof(argv[2]);
        n = atoll(argv[3]);
    }
    double t0 = MPI_Wtime(); 
    double h = (b - a) / (double)n;

    long long start_idx, local_n;
    split_work(n, size, rank, &start_idx, &local_n);

    double local_sum = 0.0;
    long long end_idx = start_idx + local_n;

    for (long long i = start_idx; i < end_idx; ++i) {
        double x_left = a + i * h;
        double x_right = a + (i + 1) * h;
        local_sum += (f(x_left) + f(x_right)) / 2.0;
    }
    double local_integral = local_sum * h;

    double total_integral = 0.0;
    MPI_Reduce(&local_integral, &total_integral, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD); 

    double t1 = MPI_Wtime(); 

    if (rank == 0) {
        double exact = 2.0;
        double err = fabs(total_integral - exact);
        printf("metod trpezii\n");
        printf("integral of sin(x) on [%.7f, %.6f] = %.15f\n", a, b, total_integral);
        printf("exact value: %.15f\n", exact);
        printf("error: %.15e\n", err);
        printf("computed value: %.15f\n", total_integral); 
        printf("number of processes: %d\n", size);
        printf("number of intervals: %lld\n", n);
        printf("time: %.6f seconds\n", t1 - t0);
    }
    MPI_Finalize();
    return 0;
}