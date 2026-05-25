import os

path = '/home/x/Work/File Manager/AetherFiles/src/presentation/views/window_views.c'
with open(path, 'r') as f:
    code = f.read()

idx = code.find("void on_item_right_clicked")
if idx != -1:
    code = code[:idx]

with open(path, 'w') as f:
    f.write(code)
