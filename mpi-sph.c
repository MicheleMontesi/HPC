/****************************************************************************
 *
 * sph.c -- Smoothed Particle Hydrodynamics
 *
 * https://github.com/cerrno/mueller-sph
 *
 * Copyright (C) 2016 Lucas V. Schuermann
 * Copyright (C) 2022 Moreno Marzolla
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ****************************************************************************/
#ifdef GUI
#if __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <mpi.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* "Particle-Based Fluid Simulation for Interactive Applications" by
   Müller et al. solver parameters */

const float Gx = 0.0, Gy = -10.0;   // external (gravitational) forces
const float REST_DENS = 300;    // rest density
const float GAS_CONST = 2000;   // const for equation of state
const float H = 16;             // kernel radius
const float EPS = 16;           // equal to H
const float MASS = 2.5;         // assume all particles have the same mass
const float VISC = 200;         // viscosity constant
const float DT = 0.0007;        // integration timestep
const float BOUND_DAMPING = -0.5;

// rendering projection parameters
// (the following ought to be "const float", but then the compiler
// would give an error because VIEW_WIDTH and VIEW_HEIGHT are
// initialized with non-literal expressions)
#ifdef GUI

const int MAX_PARTICLES = 5000;
#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 768

#else

const int MAX_PARTICLES = 20000;
// Larger window size to accommodate more particles
#define WINDOW_WIDTH 3000
#define WINDOW_HEIGHT 2000

#endif

const int DAM_PARTICLES = 500;

const float VIEW_WIDTH = 1.5 * WINDOW_WIDTH;
const float VIEW_HEIGHT = 1.5 * WINDOW_HEIGHT;

/* Particle data structure; stores position, velocity, and force for
   integration stores density (rho) and pressure values for SPH.

   You may choose a different layout of the particles[] data structure
   to suit your needs. */
typedef struct {
    float x, y;         // position
    float vx, vy;       // velocity
    float fx, fy;       // force
    float rho, p;       // density, pressure
} particle_t;

particle_t *particles;
int n_particles = 0;    // number of currently active particles

/**
 * Return a random value in [a, b]
 */
float randab(float a, float b)
{
    return a + (b-a)*rand() / (float)(RAND_MAX);
}

/**
 * Set initial position of particle `*p` to (x, y); initialize all
 * other attributes to default values (zeros).
 */
void init_particle( particle_t *p, float x, float y )
{
    p->x = x;
    p->y = y;
    p->vx = p->vy = 0.0;
    p->fx = p->fy = 0.0;
    p->rho = 0.0;
    p->p = 0.0;
}

/**
 * Return nonzero iff (x, y) is within the frame
 */
int is_in_domain( float x, float y )
{
    return ((x < VIEW_WIDTH - EPS) &&
            (x > EPS) &&
            (y < VIEW_HEIGHT - EPS) &&
            (y > EPS));
}

/**
 * Initialize the SPH model with `n` particles. The caller is
 * responsible for allocating the `particles[]` array of size
 * `MAX_PARTICLES`.
 *
 * DO NOT parallelize this function, since it calls rand() which is
 * not thread-safe.
 *
 * For MPI and OpenMP: only the master must initialize the domain;
 *
 * For CUDA: the CPU must initialize the domain.
 */
void init_sph( int n )
{
    n_particles = 0;
    printf("Initializing with %d particles\n", n);

    for (float y = EPS; y < VIEW_HEIGHT - EPS; y += H) {
        for (float x = EPS; x <= VIEW_WIDTH * 0.8f; x += H) {
            if (n_particles < n) {
                float jitter = rand() / (float)RAND_MAX;
                init_particle(particles + n_particles, x+jitter, y);
                n_particles++;
            } else {
                return;
            }
        }
    }
    assert(n_particles == n);
}

/**
 ** You may parallelize the following four functions
 **/

void compute_density_pressure( particle_t *local_data, int start, int end )
{
    const float HSQ = H * H;    // radius^2 for optimization

    /* Smoothing kernels defined in Muller and their gradients adapted
       to 2D per "SPH Based Shallow Water Simulation" by Solenthaler
       et al. */
    const float POLY6 = 4.0 / (M_PI * pow(H, 8));

    for (int i=start; i<end; i++) {
        particle_t *pi = &local_data[i];
        pi->rho = 0.0;
        for (int j=start; j<end; j++) {
            const particle_t *pj = &local_data[j];

            const float dx = pj->x - pi->x;
            const float dy = pj->y - pi->y;
            const float d2 = dx*dx + dy*dy;

            if (d2 < HSQ) {
                pi->rho += MASS * POLY6 * pow(HSQ - d2, 3.0);
            }
        }
        pi->p = GAS_CONST * (pi->rho - REST_DENS);
    }
}

void compute_forces( particle_t *local_data, int start, int end )
{
    /* Smoothing kernels defined in Muller and their gradients adapted
       to 2D per "SPH Based Shallow Water Simulation" by Solenthaler
       et al. */
    const float SPIKY_GRAD = -10.0 / (M_PI * pow(H, 5));
    const float VISC_LAP = 40.0 / (M_PI * pow(H, 5));
    const float EPS = 1e-6;

    for (int i=start; i<end; i++) {
        particle_t *pi = &local_data[i];
        float fpress_x = 0.0, fpress_y = 0.0;
        float fvisc_x = 0.0, fvisc_y = 0.0;

        for (int j=start; j<end; j++) {
            const particle_t *pj = &local_data[j];

            if (pi == pj)
                continue;

            const float dx = pj->x - pi->x;
            const float dy = pj->y - pi->y;
            const float dist = hypotf(dx, dy) + EPS; // avoids division by zero later on

            if (dist < H) {
                const float norm_dx = dx / dist;
                const float norm_dy = dy / dist;
                // compute pressure force contribution
                fpress_x += -norm_dx * MASS * (pi->p + pj->p) / (2 * pj->rho) * SPIKY_GRAD * pow(H - dist, 3);
                fpress_y += -norm_dy * MASS * (pi->p + pj->p) / (2 * pj->rho) * SPIKY_GRAD * pow(H - dist, 3);
                // compute viscosity force contribution
                fvisc_x += VISC * MASS * (pj->vx - pi->vx) / pj->rho * VISC_LAP * (H - dist);
                fvisc_y += VISC * MASS * (pj->vy - pi->vy) / pj->rho * VISC_LAP * (H - dist);
            }
        }
        const float fgrav_x = Gx * MASS / pi->rho;
        const float fgrav_y = Gy * MASS / pi->rho;
        pi->fx = fpress_x + fvisc_x + fgrav_x;
        pi->fy = fpress_y + fvisc_y + fgrav_y;
    }
}

void integrate( particle_t *local_data, int start, int end )
{
    for (int i=start; i<end; i++) {
        particle_t *p = &local_data[i];
        // forward Euler integration
        p->vx += DT * p->fx / p->rho;
        p->vy += DT * p->fy / p->rho;
        p->x += DT * p->vx;
        p->y += DT * p->vy;

        // enforce boundary conditions
        if (p->x - EPS < 0.0) {
            p->vx *= BOUND_DAMPING;
            p->x = EPS;
        }
        if (p->x + EPS > VIEW_WIDTH) {
            p->vx *= BOUND_DAMPING;
            p->x = VIEW_WIDTH - EPS;
        }
        if (p->y - EPS < 0.0) {
            p->vy *= BOUND_DAMPING;
            p->y = EPS;
        }
        if (p->y + EPS > VIEW_HEIGHT) {
            p->vy *= BOUND_DAMPING;
            p->y = VIEW_HEIGHT - EPS;
        }
    }
}

float avg_velocities( particle_t *local_data, int local_start, int local_end )
{
    double result = 0.0;
    for (int i=local_start; i<local_end; i++) {
        /* the hypot(x,y) function is equivalent to sqrt(x*x +
           y*y); */
        result += hypot(local_data[i].vx, local_data[i].vy) / local_end;
    }
    return result;
}

void update( particle_t *local_data, int local_start, int local_end )
{
    compute_density_pressure(local_data, local_start, local_end);
    compute_forces(local_data, local_start, local_end);
    integrate(local_data, local_start, local_end);
}

#ifdef GUI
/**
 ** GUI-specific functions. You can enable the GUI by compiling this
 ** program with the -DGUI flag. Note, however, that the GUI version
 ** will NOT be evaluated, and therefore is not required to work with
 ** the parallel code. You are allowed to completely remove the block
 ** #ifdef GUI ... #endif from the source code.
 **/

/**
 * Place a ball with radius `r` centered at (cx, cy) into the frame.
 */
void place_ball( float cx, float cy, float r )
{
    for (float y = cy-r; y<cy+r; y += H) {
        for (float x = cx-r; x<cx+r; x += H) {
            if ((n_particles < MAX_PARTICLES) &&
                is_in_domain(x, y) &&
                ((x-cx)*(x-cx) + (y-cy)*(y-cy) <= r*r)) {
                /* Add a small random jitter to the points, so that
                   the result will be more realistic */
                const float jitterx = rand() / (float)RAND_MAX;
                const float jittery = rand() / (float)RAND_MAX;
                init_particle(particles + n_particles, x+jitterx, y+jittery);
                n_particles++;
            }
        }
    }
}

void init_gl( void )
{
    glClearColor(0.9, 0.9, 0.9, 1);
    glEnable(GL_POINT_SMOOTH);
    glPointSize(H / 2.0);
    glMatrixMode(GL_PROJECTION);
}

void render( void )
{
    static const int MAX_FRAMES = 100;
    static int frameno = 0;

    glClear(GL_COLOR_BUFFER_BIT);

    glLoadIdentity();
    glOrtho(0, VIEW_WIDTH, 0, VIEW_HEIGHT, 0, 1);

    glColor4f(0.2, 0.6, 1.0, 1);
    glBegin(GL_POINTS);
    for (int i=0; i<n_particles; i++) {
        glVertex2f(particles[i].x, particles[i].y);
    }
    glEnd();

    glutSwapBuffers();
    glutPostRedisplay();
    frameno++;
    if (frameno > MAX_FRAMES) {
        const float avg = avg_velocities();
        printf("avgV=%f\n", avg);
        frameno = 0;
    }
}

/**
 * The compiler might issue a warning due to parameters `x` and `y`
 * being unused; this warning can be ignored.
 */
void keyboard_handler(unsigned char c, int x, int y)
{
    if (c=='r' || c=='R')  {
        init_sph(DAM_PARTICLES);
    }
}

void mouse_handler(int button, int state, int x, int y)
{
    static const float RADIUS = 110.0;
    if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
        place_ball(1.5*x, VIEW_HEIGHT - 1.5*y, RADIUS);
        printf("n. particles/max particles: %d/%d\n", n_particles, MAX_PARTICLES);
    }
}

/**
 ** END of GUI-specific functions
 **/
#endif

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);
    int rank, nproc;

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nproc);

    int n_local;              /** numero di particelle locali */
    particle_t *local_data;   /** dati particelle locali */
    int *local_count;         /** conteggio delle particelle in ogni processo */
    int *local_offset;        /** offset dei dati delle particelle locali */
    
    /** 
     * define 
     * - an array with amount of every part of the struct particle_t 
     * - an array with the offset of every element in the struct
     * - an array with the types of the element of the struct
     * 
     * then create an MPI_Datatype of the struct
     * */
    int block_lengths[] = {1, 1, 1, 1, 1, 1, 1, 1};
    MPI_Aint displacements[8];
    MPI_Datatype types[] = {MPI_FLOAT, MPI_FLOAT, MPI_FLOAT, MPI_FLOAT, MPI_FLOAT, MPI_FLOAT, MPI_FLOAT, MPI_FLOAT};
    MPI_Datatype mpi_particle;
    
displacements[0] = offsetof(particle_t, x);
displacements[1] = offsetof(particle_t, y);
displacements[2] = offsetof(particle_t, vx);
displacements[3] = offsetof(particle_t, vy);
displacements[4] = offsetof(particle_t, fx);
displacements[5] = offsetof(particle_t, fy);
displacements[6] = offsetof(particle_t, rho);
displacements[7] = offsetof(particle_t, p);

    MPI_Type_create_struct(8, block_lengths, displacements, types, &mpi_particle);
    MPI_Type_commit(&mpi_particle);

    srand(1234);

    particles = (particle_t*)malloc(MAX_PARTICLES * sizeof(*particles));
    assert( particles != NULL );

#ifdef GUI
    glutInitWindowSize(WINDOW_WIDTH, WINDOW_HEIGHT);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInit(&argc, argv);
    glutCreateWindow("Muller SPH");
    glutDisplayFunc(render);
    glutIdleFunc(update);
    glutKeyboardFunc(keyboard_handler);
    glutMouseFunc(mouse_handler);

    init_gl();
    init_sph(DAM_PARTICLES);

    glutMainLoop();
#else
    int n = DAM_PARTICLES;
    int nsteps = 50;

    if (rank == 0) {
        if (argc > 3) {
            fprintf(stderr, "Usage: %s [nparticles [nsteps]]\n", argv[0]);
            return EXIT_FAILURE;
        }

        if (argc > 1) {
            n = atoi(argv[1]);
        }

        if (n > MAX_PARTICLES) {
            fprintf(stderr, "FATAL: the maximum number of particles is %d\n", MAX_PARTICLES);
            return EXIT_FAILURE;
        }

        init_sph(n);
    }

    if (argc > 2) {
        nsteps = atoi(argv[2]);
    }
        
    MPI_Bcast(&n_particles, 1, MPI_INT, 0, MPI_COMM_WORLD);

    n_local = n_particles / nproc;
    if (rank == nproc - 1) {
      n_local += n_particles % nproc;
    }

    local_data = (particle_t*)malloc(n_local * sizeof(particle_t));

    local_count = (int*)malloc(nproc * sizeof(int));
    local_offset = (int*)malloc(nproc *sizeof(int));

    for (int i = 0; i < nproc; i++) {
      local_count[i] = n_particles / nproc;
      if (i == nproc - 1) {
        local_count[i] += n_particles % nproc;
      }
      local_offset[i] = i* (n_particles / nproc);
    }

    printf("rank = %d, offset = %d, count = %d\n", rank, local_offset[rank], local_count[rank]);

    // change the type and the size of the sent and received type
    MPI_Scatterv(particles, local_count, local_offset, mpi_particle, local_data, n_local, mpi_particle, 0, MPI_COMM_WORLD);

    float global_avg = 0;

    for (int s=0; s<nsteps; s++) {
        update(local_data, 0, n_local);
        /* the average velocities MUST be computed at each step, even
           if it is not shown (to ensure constant workload per
           iteration) */
        const float avg = avg_velocities(local_data, 0, n_local);

        // printf("%d %f\n", rank, avg);

        MPI_Reduce(&avg, &global_avg, 1, MPI_FLOAT, MPI_SUM, 0, MPI_COMM_WORLD);
        
        if (rank == 0) {
          global_avg /= nproc;
          if (s % 10 == 0)
              printf("step %5d, avgV=%f\n", s, global_avg);
        }
    }
#endif
    free(particles);
    free(local_data);
    free(local_count);
    free(local_offset);
    MPI_Type_free(&mpi_particle);
    MPI_Finalize();
    return EXIT_SUCCESS;
}
