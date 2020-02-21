#include <ncurses.h>
#include <fstream>

#include "box.hpp"

std::ofstream file("debug-test.log");

int main() {
	Box root = { "Test F (Test 5++)" };

	root.move(1, 1).horz_line(5, ACS_ULCORNER);
	root.move(3, 3).vert_line(5, ACS_LLCORNER);

	root.refresh();

	root.get_char();

	return 0;
}

