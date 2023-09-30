# Software Compilation Instructions
## omp-sph.c
- Compilation:
```bash
gcc -std=c99 -Wall -Wpedantic -fopenmp omp-sph.c -o omp-sph.o -lm
```
- Execution:
```bash
OMP_NUM_THREADS=[N_Threads] ./omp-sph.o [N_Particles [N_Steps]]
```

## mpi-sph.c
- Compilation:
```bash
mpicc -std=c99 -Wall -Wpedantic mpi-sph.c -o mpi-sph.o -lm
```

- Execution:
```bash
mpirun -n [N_Procs] ./mpi-sph.o [N_Particles [N_Steps]]
```
