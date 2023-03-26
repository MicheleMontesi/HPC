import os
import subprocess
import pandas as pd
import re
import sys

args = sys.argv[1:]

num_particles_start = 500
num_particles_end = 2000

try:
  num_particles_start = int(args[0])
  num_particles_end = int(args[1])
except:
  pass

num_particles_step = 100
num_steps = 50
num_procs_start = 2
num_procs_end = 12
output_file = 'output.xlsx'

if os.path.isfile(output_file):
  n = 1
  while os.path.isfile(f"output_{n}.xlsx"):
    n += 1
  output_file = f"output_{n}.xlsx"

# Crea un DataFrame vuoto
df = pd.DataFrame(columns=['Num. Particles', 'Num. Processes', 'Time', 'Speedup'])

for num_particles in range(num_particles_start, num_particles_end + num_particles_step, num_particles_step):
    single_proc_command = f"OMP_NUM_THREADS=1 ./omp-sph.o {num_particles} {num_steps}"
    single_proc_result = subprocess.run(single_proc_command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if single_proc_result.returncode == 0:
        single_proc_time_line = [line for line in single_proc_result.stdout.strip().split('\n') if 'Total Time: ' in line]
        if len(single_proc_time_line) > 0:
            single_proc_time_str = re.search(r'\d+\.\d+', single_proc_time_line[0]).group(0)
            single_proc_time = float(single_proc_time_str)
            print(f"Tempo impiegato: {time}")
            speedup = 1.0
            print(f"Speedup: {speedup}")
            df = pd.concat([df, pd.DataFrame({'Num. Particles': [num_particles], 'Num. Processes': [num_procs], 'Time': [time], 'Speedup': [speedup]})], ignore_index=True)
        else:
            print("Errore: impossibile trovare il tempo di esecuzione")
    else:
        print(f"Errore durante l'esecuzione del comando: {result.stderr}")

    for num_procs in range(num_procs_start, num_procs_end + 1):
        command = f"OMP_NUM_THREADS={num_procs} ./omp-sph.o {num_particles} {num_steps}"
        print(f"Eseguo il comando: {command}")
        result = subprocess.run(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if result.returncode == 0:
            output_lines = result.stdout.strip().split('\n')
            time_line = [line for line in output_lines if 'Total Time: ' in line]
            if len(time_line) > 0:
                time_str = re.search(r'\d+\.\d+', time_line[0]).group(0)
                time = float(time_str)
                print(f"Tempo impiegato: {time}")
                speedup = single_proc_time / time
                print(f"Speedup: {speedup}")
                df = pd.concat([df, pd.DataFrame({'Num. Particles': [num_particles], 'Num. Processes': [num_procs], 'Time': [time], 'Speedup': [speedup]})], ignore_index=True)
            else:
                print("Errore: impossibile trovare il tempo di esecuzione")
        else:
            print(f"Errore durante l'esecuzione del comando: {result.stderr}")
    df.loc[len(df)] = ''



# Scrivi il file Excel
df.to_excel(output_file, index=False)
