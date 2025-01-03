import pandas as pd

with open('./output_mt.txt', 'r') as file:
    data = [float(line.strip()) for line in file]

bin_size = 0.005
bins = np.arange(min(data), )

