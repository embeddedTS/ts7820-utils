#!/bin/bash

eval "$(tshwctl -i)"
export model

if [ "$model" = "0x7840" ]; then
	idleinject \
		--maxtemp 115000 \
		--led /sys/class/leds/right-red-led/brightness
else
	idleinject --maxtemp 115000
fi
