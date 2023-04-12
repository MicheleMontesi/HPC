# Istruzioni di compilazione del software
## omp-sph.c
- Compilazione: 
```bash
gcc -std=c99 -Wall -Wpedantic -fopenmp omp-sph.c -o omp-sph.o -lm
```
- Esecuzione:
```bash
 OMP_NUM_THREADS=[N_Threads] ./omp-sph.o [N_Particles [N_Steps]]
```

## mpi-sph.c
- Compilazione:
```bash
 mpicc -std=c99 -Wall -Wpedantic mpi-sph.c -o mpi-sph.o -lm
```

- Esecuzione:
```bash
mpirun -n [N_Procs] ./mpi-sph.o [N_Particles [N_Steps]]
```