#include <stddef.h>
#include <cassert>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "canaan-burn.h"

using namespace std;

int main(int argc, char **argv) {
	assert(KBURN_ERROR_KIND_SERIAL > UINT32_MAX);
	cout << '\x1B' << 'c';
	flush(cout);

	KBMonCTX monitor = NULL;
	kburn_err_t err = kburnMonitorCreate(&monitor);
	if (err != KBurnNoErr) {
		cerr << "Failed Start: " << err << endl;
		return 1;
	}

	kburnMonitorStartWaitingDevices(monitor);
	getchar();

	kburnMonitorGlobalDestroy();

	return 0;
}
