#pragma once
// Call first thing in main(): moves the previous boot's fault record aside.
void fault_boot_capture();
// Text of the previous boot's HardFault (empty string / 0 if there was none).
int fault_snapshot(char *buf, int n);
