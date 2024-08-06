import matplotlib.pyplot as plt
import numpy as np
import seaborn as sn

from sklearn.metrics import confusion_matrix
from pandas import DataFrame


latency_gt_l1 = []
fail = []

for i in range(482):
    latency_gt_l1.append(1)
    fail.append(1)

for i in range(1000-482):
    latency_gt_l1.append(0)
    fail.append(0)

latency_gt_l1_np = np.array(latency_gt_l1)
fail_np = np.array(fail)

x_labels = ["fail = 0", "fail = 1"]
y_labels = ["latency <= L1_latency", "latency > L1_latency"]

confm = confusion_matrix(fail_np, latency_gt_l1_np)
df_cm = DataFrame(confm, index=x_labels, columns=y_labels)

ax = sn.heatmap(df_cm, cmap='Oranges', annot=True)





