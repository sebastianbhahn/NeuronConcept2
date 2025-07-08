I made this a few months ago, second attempt, or second variant, of a neuron/brain design. 
Never finished the first attempt, wanted to remake it from scratch, the old attempt is also on this github, named "experimentalMLThing".
Spent some time on this, didn't bother finishing it once I got the core concepts in place.
If it isn't in one of the "todo" files, the idea is to have more neuron types, specifically input and output types, 
tied to, on the input side, a individual pixels for "eyesight", and also frequency inputs for sound, for example, and vice versa as an output.
Not sure what base structure would be good for an initial starting point to start training the brain from.
Not sure what other neuron types might be useful, or not.
This model is supposed to learn to train itself eventually. For real time learning.
The system is meant to run in cycles, loops.
semi-asynchronous, but I think I was going to rework part of the reward system and neuron placement system 
(which isn't finished) to be synchronous and get rid of some taskthreads,
if I haven't already. It's been some time since I messed with this. Wanted a more finished version before uploading it but I lost interest.
At a glance, it looks like the most recent, up to date code is in new.cpp, but I haven't ported a lot of the functions from the original main.cpp.
So I was remaking this again before finishing it.
Probably part of why I lost interest.
