#!/usr/bin/env python
"""Generate a header with logo bitmap data and information about each input.
Inputs are configured in the _INPUTS list below.
"""
import os
import collections
from PIL import Image

Input = collections.namedtuple('Input', ['name', 'address', 'label', 'key'])
# name: Name of png file under logos/ to use for input logo
# address: Multiplexer address for input
# label: 8-character label for input, used in uptime display
# key: "Primary key" for input, used to index uptimes, so should never change

_INPUTS = [
    Input('info', '0xFF', 'UPTIME', 0),
    Input('genesis', '0x15', 'GENESIS', 1),
    Input('superfamicom', '0x14', 'SFC', 2),
    Input('threedo', '0x13', '3DO', 3),
    Input('saturn', '0x12', 'SATURN', 4),
    Input('playstation', '0x11', 'PSX', 5),
    Input('dreamcast', '0x10', 'DC', 6),
    Input('ps2', '0x0D', 'PS2', 7),
    Input('gamecube', '0x0C', 'GAMECUBE', 8),
    Input(None, '0x08', None, None),
    Input(None, '0x09', None, None),
    Input('vhs', '0x0A', 'VHS', 9),
    Input('aux', '0x0B', 'AUX', 10),
]

def main():
    total_width = 0

    print('#ifndef RIBBON_H')
    print('#define RIBBON_H')

    print('#include <stdint.h>')

    print('struct input {')
    print('\tuint8_t address;')
    print('\tchar abbrev[9];')
    print('\tuint16_t begin;')
    print('\tuint16_t center;')
    print('\tuint16_t end;')
    print('\tuint8_t id;')
    print('} inputs[12] = {')
    for i, input in enumerate(_INPUTS):
        if input.name is not None:
            print('\t{' + input.address + ', ', end='')
            print('"' + input.label + '", ', end='')
            # Loop through logos and calculate width info for each one.
            # This is to determine the total width of the entire ribbon of
            # logos, but it also the column indexes of the beginning, end, and
            # middle of each logo within the ribbon.
            with open('logos/' + input.name + '.png', 'rb') as imagefile:
                image = Image.open(imagefile)
                width, _ = image.size
                print(str(total_width) + ', ', end='') # begin
                print(str(total_width + int(width / 2)) + ', ', end='') # mid
                total_width += width
                total_width += 4
                print(str(total_width) + ', ', end='') # end
                print(str(input.key), end='') # key
                print('},')
    print('\t{0},')
    print('};')

    ribbon = Image.new('RGBA', (total_width, 32))
    ribbon_pixels = ribbon.load()

    # Current ribbon column
    x0 = 0

    # Load each logo PNG into the ribbon image in sequence.
    for input in _INPUTS:
        if input.name is not None:
            with open('logos/' + input.name + '.png', 'rb') as imagefile:
                image = Image.open(imagefile)
                width, _ = image.size
                pixels = image.load()
                # 2 column left margin
                x0 += 2
                for x in range(width):
                    for y in range(32):
                        ribbon_pixels[x0 + x, y] = pixels[x, y]
                x0 += width
                # 2 column right margin
                x0 += 2

    # Spit out the ribbon image data.  The pixel data is in VFD format -
    # column-major order, each byte is 8 consecutive vertical pixels.
    print('const uint16_t ribbon_width = ' + str(total_width) + ';')
    print('const uint8_t ribbon_height = ' + str(32) + ';')
    num_inputs = len([i for i in _INPUTS if i[0] is not None])
    print('#define NUM_INPUTS ' + str(num_inputs))
    ribbon_size = total_width * int(32 / 8)
    print('uint8_t ribbon_pixel[' + str(ribbon_size) + '] = {', end='')
    for x in range(total_width):
        if (x % 4) == 0:
            print('')
        for y in range(int(32 / 8)):
            pix = 0
            for b in range(8):
                r, _, _, a = ribbon_pixels[x, y * 8 + b]
                if (r == 255):
                    pix |= 1 << (7 - b)
            print('0x{:02x},'.format(pix), end='')
    print('')
    print('};')

    print('#endif')

if __name__ == '__main__':
    main()
