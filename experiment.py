import subprocess
try:
    from subprocess import DEVNULL
except ImportError:
    import os
    DEVNULL = open(os.devnull, 'wb')

EXPERIMENT_TIMES = 100

with open('result.cvs', 'w') as file:
    file.write("move, score, max rank\n")
    for i in range(EXPERIMENT_TIMES):
        p = subprocess.Popen(
            './threes', stderr=subprocess.PIPE, stdout=DEVNULL)
        for line in p.stderr.readlines():
            file.write(line)

retval = p.wait()
