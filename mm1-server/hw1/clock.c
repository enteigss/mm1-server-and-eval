/*******************************************************************************
* CPU Clock Measurement Using RDTSC
*
* Description:
*     This C file provides functions to compute and measure the CPU clock using
*     the `rdtsc` instruction. The `rdtsc` instruction returns the Time Stamp
*     Counter, which can be used to measure CPU clock cycles.
*
* Author:
*     Renato Mancuso
*
* Affiliation:
*     Boston University
*
* Creation Date:
*     September 10, 2023
*
* Last Update:
*     September 9, 2024
*
* Notes:
*     Ensure that the platform supports the `rdtsc` instruction before using
*     these functions. Depending on the CPU architecture and power-saving
*     modes, the results might vary. Always refer to the CPU's official
*     documentation for accurate interpretations.
*
*******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "timelib.h"

int main (int argc, char ** argv)
{
	/* IMPLEMENT ME! */
    char *m = argv[3];
    uint64_t elapsed_cycles;
    double elapsed_time_sec;
    double clock_speed_mhz;
    long sec = strtol(argv[1], NULL, 10);
    long nsec = strtol(argv[2], NULL, 10);
    char method[20];
    if (strcmp(m, "s") == 0) {
        elapsed_cycles = get_elapsed_sleep(sec, nsec);
        strcpy(method, "SLEEP");
    } else if (strcmp(m, "b") == 0) {
        elapsed_cycles = get_elapsed_busywait(sec, nsec);
        strcpy(method, "BUSYWAIT");
    };
    elapsed_time_sec = sec + (nsec / 1e9);
    clock_speed_mhz = (elapsed_cycles / elapsed_time_sec) / 1000000;

    printf("WaitMethod: %s\n", method);
    printf("WaitTime: %lu %lu\n", sec, nsec);
    printf("ClocksElapsed: %ld\n", elapsed_cycles);
    printf("ClockSpeed: %.2f\n", clock_speed_mhz);

	return EXIT_SUCCESS;
}

