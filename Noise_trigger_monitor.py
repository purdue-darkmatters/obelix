#!/depot/darkmatter/etc/conda/env/asterix/bin/python3
import numpy as np
import scipy
from scipy.optimize import curve_fit
import pandas as pd

import matplotlib as mpl
import matplotlib.pyplot as plt
import json
import os
import sys

if len(sys.argv) != 2:
    print('Usage: %s path/to/noise/run' % sys.argv[0])
    sys.exit()
else:
    full_path = sys.argv[1]

if not os.path.exists(full_path):
    print('Can\'t find %s' % (full_path))
else:
    print('Found %s' % full_path)

if full_path[-1] == '/':
    run = full_path.split('/')[-2]
else:
    run = full_path.split('/')[-1]
with open(os.path.join(full_path,'pax_info.json'),'r') as f:
    run_metadata = json.load(f)
    if run_metadata['is_zle']:
        print('This is ZLE data, we can\' use it')
    else:
        print('Metadata loaded')

ast_event_header = np.dtype([
    ('word0', '<u4'), # event number and header tag
    ('word1', '<u4'), # channel mask
    ('word2', '<u4'), # bit[31] = zle, bits [0:30] = event size
    ('word3', '<u4'), # timestamp bits[32:63]
    ('word4', '<u4'), # timestamp bits[0:31]
])
events = []
with open(os.path.join(full_path, run + '_000000.ast'),'rb') as f:
    for event_size,file_loc in zip(run_metadata['event_size_bytes'],run_metadata['event_size_cum']):
        data = f.read(event_size)
        header = np.fromstring(data, dtype=ast_event_header, count=1)[0]
        data = np.fromstring(data[20:], dtype=np.uint16)

        channel_mask = header['word1']
        is_zle = header['word2'] & (0x80000000)
        #print('header ', header['word2'])
        event_size = header['word2'] & (0x7FFFFFFF)
        #print('size bytes ', event_size)
        trigger_timestamp = (header['word3'] << 32) | header['word4']
        channels = []
        for ch in range(32): # max 32 channels
            if channel_mask & (1 << ch):
                channels.append(ch)

        events.append(np.reshape(data, (len(channels), -1)))
events = np.array(events)
print('Loaded %i events' % len(events))
total_time = len(np.reshape(events[:,0,:]))
print('Looking at %f ms of data' % (total_time*1e-5))

plt.figure(figsize=(12,9))
bins = np.arange(0,2**14 + 1)
#bins = np.arange(0,450)
cols = ['blue', 'green', 'red', 'cyan', 'magenta']
hists = []
for i,ch in enumerate(channels):
    here = events[:,i,:]
    print('Ch %i: Mean %f, std %f' % (ch, np.mean(here),np.std(here)))
    n, _, _ = plt.hist(np.reshape(here, -1), bins=bins, histtype='step', label='channel %i' % ch, color=cols[i])
    quant = [sum(n[:j]) for j in range(1,len(n))]
    plt.plot(range(1,len(n)), quant, c=cols[i], linestyle=':')

plt.xlabel('Bin')
plt.ylabel('Count')
plt.yscale('log')
#plt.xlim(bins[0],bins[-1])
plt.xlim(15950,16010)
plt.ylim(0.8,)
plt.legend(loc='upper left')

plt.show()

