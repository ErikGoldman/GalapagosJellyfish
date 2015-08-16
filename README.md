# Welcome to the Camp Galapagos Jellyfish Theremin!

This repo has two folders: theremin_sensor and theremin_receiver.

## theremin_sensor

The theremin_sensor is intended to be connected to the output of an LC oscillator with an antenna in parallel.
Specifically, connect the output of that LC circuit to *pin 5* of these Arduinos.

For the LC circuit, I recommend building the circuit [here](interface.khm.de/index.php/lab/interfaces-advanced/theremin-as-a-capacitive-sensing-device)

If you connect the serial out of a theremin_sensor to the serial in of a second theremin_sensor,
the 2nd sensor will combine its own output with the output of the previous sensor and
send the combined values via its own serial out. This only works for 2 Arduinos, not an arbitrary number of them.
Sorry =(

## theremin_receiver

This receives the signal from a theremin_sensor (or two theremin_sensors daisy-chained to each other)
and does something useful with it. In this case, it emits a synth tone and controls some lights.

If the theremin_receiver is left alone for 10 seconds, it goes into screensaver mode and just makes
the lights dance around.
