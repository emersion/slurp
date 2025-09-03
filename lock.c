#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>

#include "lock.h"

// Maximum path length for the lock file
#define MAX_PATH_SIZE 512

/**
 * Calculate the path to use for the lock file and store it in
 * path.
 *
 * Return false if no path could be determined.
 */
bool get_lockfile_path(char path[MAX_PATH_SIZE]) {
	char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir) {
		// Use the /tmp directory if we couldn't get a normal runtime dir
		runtime_dir = "/tmp";
	}
	char *display = getenv("WAYLAND_DISPLAY");
	if (!display) {
		fprintf(stderr, "No wayland session found\n");
		return false;
	}

	if (snprintf(path, MAX_PATH_SIZE, "%s/slurp-%s.lock", runtime_dir, display) >= MAX_PATH_SIZE) {
		fprintf(stderr, "lockfile path was too long\n");
		return false;
	}

	return true;
}

bool acquire_lock() {
	char lockfile[MAX_PATH_SIZE];
	if (!get_lockfile_path(lockfile)) {
		return false;
	}
	// Open the lock file for write, creating with user read/write if necessary
	int fd = open(lockfile, O_WRONLY|O_CREAT, 00600);
	if (fd == -1) {
		fprintf(stderr, "failed to open lock file\n");
		return false;
	}
	if (flock(fd, LOCK_EX|LOCK_NB)) {
		fprintf(stderr, "another slurp process is running for this wayland session\n");
		return false;
	}
	return true;
}


