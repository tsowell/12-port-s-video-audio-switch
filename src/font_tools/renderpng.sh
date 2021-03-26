#!/bin/sh

# Render select characters to PNG files
for I in A B C D E F G H I J K L M N O P Q R S T U V W X Y Z \
	0 1 2 3 4 5 6 7 8 9 d h m s; do
   convert -font -misc-fixed-*-*-normal-*-7-*-*-*-*-*-*-* -pointsize 10 \
	   label:$I $I.png
done
