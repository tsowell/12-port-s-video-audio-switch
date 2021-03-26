#!/usr/bin/env python
"""Convert font PNGs from renderpng.sh into a header with an array of font
bitmap data.
"""
import os
from PIL import Image

def main():

print('#ifndef FONT_H')
print('#define FONT_H')

chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZdhms"

# Pixel data is in VFD format - column-major order, each byte is 8 consecutive
# vertical pixels.
print('const uint8_t font[' + str(len(chars)) + '][5] = {')
for char in chars:
    with open(char + '.png', 'rb') as imagefile:
        print('\t{', end='')
        image = Image.open(imagefile)
        pixels = image.load()
        for x in range(5):
            pix = 0
            for y in range(8):
                if pixels[x, y+1] == 0:
                    pix |= 1 << (7 - y)
            print('0x{:02x},'.format(pix), end='')
        print('},')
print('};')

print('#endif')

if __name__ == '__main__':
    main()
