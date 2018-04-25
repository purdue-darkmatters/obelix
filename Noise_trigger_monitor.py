#!/depot/darkmatter/etc/conda/env/asterix/bin/python3
import numpy as np

import matplotlib as mpl
import matplotlib.pyplot as plt
import json
import os
import sys
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('run', type=str, help='path to noise run')
args = parser.parse_args()

full_path = args.run

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
        print('This is ZLE data, not noise data')
        sys.exit()
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
ref_baseline = 16000
with open(os.path.join(full_path, run + '_000000.ast'),'rb') as f:
    for event_size,file_loc in zip(run_metadata['event_size_bytes'],run_metadata['event_size_cum']):
        data = f.read(event_size)
        header = np.fromstring(data, dtype=ast_event_header, count=1)[0]
        data = np.fromstring(data[20:], dtype=np.uint16)

        channel_mask = header['word1']
        is_zle = header['word2'] & (0x80000000)
        event_size = header['word2'] & (0x7FFFFFFF)
        trigger_timestamp = (header['word3'] << 32) | header['word4']
        channels = []
        for ch in range(32): # max 32 channels
            if channel_mask & (1 << ch):
                channels.append(ch)

        events.append(np.reshape(data, (len(channels), -1)))

events = np.array(events)
print('Loaded %i events' % len(events))
total_time = len(np.reshape(events[:,0,:],-1))
print('Looking at %f ms of data' % (total_time*1e-5))

plt.figure(figsize=(12,9))
bins = np.arange(0,2**14 + 1)
cols = ['black','blue', 'green', 'red', 'magenta', 'cyan', 'yellow']
ls = {'zle' : ':', 'trigger' : '--'}
zle_limit = 200
for i,ch in enumerate(channels):
    here = events[:,i,:]
    trigger_threshold = run_metadata['channel_settings'][ch]['trigger_threshold']
    zle_threshold = run_metadata['channel_settings'][ch]['trigger_threshold']
    print('Ch %i: Mean %f, trigger %i, zle %i' % (ch, np.mean(here), trigger_threshold, zle_threshold))
    n = np.zeros(2 ** 14)
    for v in np.reshape(here, -1):
        n[v] += 1
    quant = np.array([sum(n[:j]) for j in range(1, len(n))])/(total_time*1e-8)
    x = ref_baseline - np.arange(1,len(n))
    plt.plot(x, quant, c=cols[i], linestyle='-', label='channel %i' % ch)
    for j,v in enumerate(quant[1:]-quant[:-1]):
        if v >= zle_limit:
            print('Ch %i recommended ZLE threshold: %i' % (ch, ref_baseline - j))
            break
    plt.vlines(run_metadata['channel_settings'][ch]['trigger_threshold'], min(quant), max(quant), colors=cols[i], linestyle=ls['trigger'])
    plt.vlines(ref_baseline - j, min(quant), max(quant), colors=cols[i], linestyle=ls['zle'])

plt.xlabel('Bins below baseline (16000)')
plt.ylabel('Rate above threshold [Hz]')
plt.yscale('log')
plt.xlim(-10,110)
plt.legend(loc='upper right')

plt.show()

