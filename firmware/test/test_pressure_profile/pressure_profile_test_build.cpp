// PlatformIO does not compile firmware/src for unit tests by default. Pull in
// only the unit under test; enabling test_build_src would also compile main.cpp
// and collide with the test's setup()/loop().
#include "../../src/pressure_profile.cpp"
