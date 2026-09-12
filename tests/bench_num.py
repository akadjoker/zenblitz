from time import perf_counter
def bench(n):
    a = 0.0
    b = 1.5
    c = 0.7
    for i in range(n):
        a = a + b * c - 0.3
        b = b * 0.999 + 0.001
        c = c + a * 0.0001
    return a + b + c
N = 1000000
t0 = perf_counter()
r = bench(N)
t1 = perf_counter()
print(f"seconds={t1-t0}")
print(f"result={r}")
