#include "touch_cyd.h"
// CYD touch wiring conflicts with the ST7796 reset pin and VSPI bus.
// Enable touch only after identifying and configuring the actual controller.
bool touchInit() { return false; }
bool touchRead(uint16_t& x, uint16_t& y) { return false; }